// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include <nprpc/impl/http_file_watcher.hpp>
#include <nprpc/impl/http_file_cache.hpp>
#include <nprpc/impl/misc/thread_identity.hpp>

#include "../logging.hpp"

#ifdef __linux__
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <sys/poll.h>
#include <unistd.h>
#include <limits.h>

#include <cstring>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#endif

#include <memory>
#include <mutex>

namespace nprpc::impl {

// ─── Linux implementation ───────────────────────────────────────────────────
#ifdef __linux__

HttpFileWatcher::HttpFileWatcher(std::filesystem::path root)
    : root_(std::move(root))
{
  inotify_fd_ = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
  if (inotify_fd_ < 0)
    throw std::runtime_error("inotify_init1 failed");

  stop_fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (stop_fd_ < 0) {
    close(inotify_fd_);
    throw std::runtime_error("eventfd failed");
  }

  // Watch flags:
  //   IN_CLOSE_WRITE  – file was written and closed (build output, npm run build)
  //   IN_MOVED_TO     – atomic rename (webpack writes a temp file then renames)
  //   IN_CREATE       – new file or subdir created
  //   IN_DELETE       – file or subdir deleted
  constexpr uint32_t kFlags =
      IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_DELETE | IN_ONLYDIR;

  // Add a watch for every directory in the tree (inotify is NOT recursive).
  std::function<void(const std::filesystem::path&)> add_tree =
      [&](const std::filesystem::path& p) {
        inotify_add_watch(inotify_fd_, p.c_str(), kFlags & ~IN_ONLYDIR);
        std::error_code ec;
        for (auto& entry :
             std::filesystem::directory_iterator(p, ec)) {
          if (!ec && entry.is_directory()) {
            add_tree(entry.path());
          }
        }
      };

  std::error_code ec;
  if (std::filesystem::exists(root_, ec) && !ec) {
    add_tree(root_);
  }

  thread_ = std::thread([this] { 
    nprpc::impl::set_thread_name("file_watcher");
    run(); 
  });
  NPRPC_LOG_INFO("[FileWatcher] Watching {} for changes", root_.string());
}

HttpFileWatcher::~HttpFileWatcher()
{
  // Signal the background thread to stop.
  uint64_t one = 1;
  if (stop_fd_ >= 0) {
    (void)write(stop_fd_, &one, sizeof(one));
  }
  if (thread_.joinable()) {
    thread_.join();
  }
  if (inotify_fd_ >= 0) close(inotify_fd_);
  if (stop_fd_ >= 0)    close(stop_fd_);
}

void HttpFileWatcher::run()
{
  // wd → directory path mapping so we can reconstruct absolute paths.
  std::unordered_map<int, std::filesystem::path> wd_to_dir;

  constexpr uint32_t kFlags = IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_DELETE;
  std::function<void(const std::filesystem::path&)> watch_dir =
      [&](const std::filesystem::path& p) {
        int wd = inotify_add_watch(inotify_fd_, p.c_str(), kFlags);
        if (wd >= 0) {
          wd_to_dir[wd] = p;
        }
        std::error_code ec;
        for (auto& entry : std::filesystem::directory_iterator(p, ec)) {
          if (!ec && entry.is_directory()) {
            watch_dir(entry.path());
          }
        }
      };

  std::error_code ec;
  if (std::filesystem::exists(root_, ec) && !ec) {
    watch_dir(root_);
  }

  // Buffer large enough for ~64 typical events.
  constexpr size_t kBufSize = 64 * (sizeof(inotify_event) + NAME_MAX + 1);
  alignas(inotify_event) char buf[kBufSize];

  struct pollfd fds[2];
  fds[0] = {inotify_fd_, POLLIN, 0};
  fds[1] = {stop_fd_,    POLLIN, 0};

  auto& cache = get_file_cache();

  // Debounce: collect events and flush after kDebounceMs of quiet.
  // This prevents acting on a mid-build partial state (e.g. manifest.js
  // written before nodes/2.js exists).
  constexpr int kDebounceMs = 500;
  std::vector<std::filesystem::path> pending_client;

  for (;;) {
    fds[0].revents = fds[1].revents = 0;
    // While events are pending use a timeout so we detect the quiet period.
    int timeout = pending_client.empty() ? -1 : kDebounceMs;
    int n = poll(fds, 2, timeout);
    if (n < 0) {
      if (errno == EINTR) continue;
      break;
    }

    // Stop requested.
    if (fds[1].revents & POLLIN) break;

    if (n == 0) {
      // Debounce timeout — no events for kDebounceMs, flush pending work.
      for (auto& p : pending_client) {
        cache.invalidate(p);
        NPRPC_LOG_INFO("[FileWatcher] Invalidated: {}", p.string());
      }
      pending_client.clear();
      continue;
    }

    if (!(fds[0].revents & POLLIN)) continue;

    ssize_t len = read(inotify_fd_, buf, kBufSize);
    if (len < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
      break;
    }

    for (char* ptr = buf; ptr < buf + len; ) {
      auto* ev = reinterpret_cast<inotify_event*>(ptr);
      ptr += sizeof(inotify_event) + ev->len;

      if (ev->mask & IN_IGNORED) {
        wd_to_dir.erase(ev->wd); // watch removed — purge stale entry to avoid wd recycling bugs
        continue;
      }

      // If a new directory was created, start watching it too.
      if ((ev->mask & IN_CREATE) && (ev->mask & IN_ISDIR) && ev->len > 0) {
        auto parent_it = wd_to_dir.find(ev->wd);
        if (parent_it != wd_to_dir.end()) {
          watch_dir(parent_it->second / ev->name);
        }
        continue;
      }

      // File-level event.
      if (ev->len == 0) continue;       // anonymous inode
      if (ev->mask & IN_ISDIR) continue; // not a file

      auto dir_it = wd_to_dir.find(ev->wd);
      if (dir_it == wd_to_dir.end()) continue;

      auto file_path = dir_it->second / ev->name;

      // Written, renamed in, or removed — either way the cached copy is stale.
      if (ev->mask & (IN_CLOSE_WRITE | IN_MOVED_TO | IN_DELETE)) {
        pending_client.push_back(file_path);
      }
    }
  }
}

// ─── Non-Linux stub ──────────────────────────────────────────────────────────
#else

HttpFileWatcher::HttpFileWatcher(std::filesystem::path)
{
  NPRPC_LOG_INFO("[FileWatcher] inotify not available on this platform; "
                 "falling back to mtime polling.");
}

HttpFileWatcher::~HttpFileWatcher() = default;

#endif // __linux__

// ─── Global instance ─────────────────────────────────────────────────────────

namespace {
std::unique_ptr<HttpFileWatcher> g_watcher;
std::once_flag g_watcher_flag;
} // namespace

void start_file_watcher(const std::filesystem::path& client_root)
{
  std::call_once(g_watcher_flag, [&] {
    g_watcher = std::make_unique<HttpFileWatcher>(client_root);
  });
}

void stop_file_watcher()
{
  g_watcher.reset();
}

} // namespace nprpc::impl

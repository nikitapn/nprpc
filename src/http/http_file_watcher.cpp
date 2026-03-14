// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include <nprpc/impl/http_file_watcher.hpp>
#include <nprpc/impl/http_file_cache.hpp>

#include "../logging.hpp"

#ifdef __linux__
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <sys/poll.h>
#include <unistd.h>

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

HttpFileWatcher::HttpFileWatcher(std::filesystem::path root,
                                 std::filesystem::path ssr_server_root,
                                 std::function<void()> on_server_rebuilt)
    : root_(std::move(root))
    , ssr_server_root_(std::move(ssr_server_root))
    , on_server_rebuilt_(std::move(on_server_rebuilt))
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
  if (!ssr_server_root_.empty() &&
      std::filesystem::exists(ssr_server_root_, ec) && !ec) {
    add_tree(ssr_server_root_);
  }

  thread_ = std::thread([this] { run(); });
  NPRPC_LOG_INFO("[FileWatcher] Watching {} for changes", root_.string());
  if (!ssr_server_root_.empty()) {
    NPRPC_LOG_INFO("[FileWatcher] Watching {} for SSR restarts",
                   ssr_server_root_.string());
  }
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
  if (!ssr_server_root_.empty()) {
    if (std::filesystem::exists(ssr_server_root_, ec) && !ec) {
      watch_dir(ssr_server_root_);
    }
    // Watch the PARENT of ssr_server_root_ (e.g. the project dir containing
    // 'build/') with a single non-recursive watch.  This lets us detect when
    // ssr_server_root_ itself is deleted and recreated — which is exactly what
    // `npm run build` does via `builder.rimraf(out)` — so we can re-add the
    // recursive watches and arm a restart even when the old watches were lost.
    auto ssr_parent = ssr_server_root_.parent_path();
    if (!ssr_parent.empty() && std::filesystem::exists(ssr_parent, ec) && !ec) {
      int wd = inotify_add_watch(inotify_fd_, ssr_parent.c_str(), kFlags);
      if (wd >= 0) {
        wd_to_dir[wd] = ssr_parent;
      }
    }
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
  bool pending_server = false;

  // Helper: does `path` live under `ssr_server_root_` but NOT under `root_`
  // (client assets under ssr_server_root_/client/ must not trigger SSR restart).
  auto is_server_file = [this](const std::filesystem::path& p) -> bool {
    if (ssr_server_root_.empty()) return false;
    const auto& ps = p.string();
    const auto& ss = ssr_server_root_.string();
    if (!(ps.size() >= ss.size() && ps.compare(0, ss.size(), ss) == 0))
      return false; // not under ssr root
    // exclude paths that also live under the client static root
    const auto& rs = root_.string();
    if (!rs.empty() && ps.size() >= rs.size() &&
        ps.compare(0, rs.size(), rs) == 0)
      return false;
    return true;
  };

  for (;;) {
    fds[0].revents = fds[1].revents = 0;
    // While events are pending use a timeout so we detect the quiet period.
    int timeout = (pending_client.empty() && !pending_server) ? -1 : kDebounceMs;
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
      if (pending_server && on_server_rebuilt_) {
        NPRPC_LOG_INFO("[FileWatcher] Server files settled, restarting SSR...");
        on_server_rebuilt_();
      }
      pending_client.clear();
      pending_server = false;
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
          auto new_dir = parent_it->second / ev->name;
          watch_dir(new_dir);
          // ssr_server_root_ itself was (re)created (e.g. `build/` after rimraf).
          // Arm a restart so Node.js is reloaded once the build settles.
          if (!ssr_server_root_.empty() && new_dir == ssr_server_root_) {
            pending_server = true;
            NPRPC_LOG_INFO("[FileWatcher] SSR build dir recreated — restart armed: {}",
                           new_dir.string());
          }
        }
        continue;
      }

      // File-level event.
      if (ev->len == 0) continue;       // anonymous inode
      if (ev->mask & IN_ISDIR) continue; // not a file

      auto dir_it = wd_to_dir.find(ev->wd);
      if (dir_it == wd_to_dir.end()) continue;

      auto file_path = dir_it->second / ev->name;
      bool server    = is_server_file(file_path);

      if (ev->mask & (IN_CLOSE_WRITE | IN_MOVED_TO)) {
        // A file was written/renamed — record it in the appropriate bucket.
        if (server) {
          if (!pending_server) {
            NPRPC_LOG_INFO("[FileWatcher] SSR build activity detected: {}",
                           file_path.string());
          }
          pending_server = true;   // arm SSR restart after quiet period
        } else {
          pending_client.push_back(file_path);
        }
      } else if (ev->mask & IN_DELETE) {
        if (!server) {
          // Client static file removed — purge the cache entry immediately
          // (next request will get a 404 / reloaded file).
          pending_client.push_back(file_path);
        }
        // Server file DELETE means the build just started clearing old output.
        // Do NOT arm pending_server here; we only want to restart once the
        // new files have been written (IN_CLOSE_WRITE / IN_MOVED_TO above).
      }
    }
  }
}

// ─── Non-Linux stub ──────────────────────────────────────────────────────────
#else

HttpFileWatcher::HttpFileWatcher(std::filesystem::path,
                                 std::filesystem::path,
                                 std::function<void()>)
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

void start_file_watcher(const std::filesystem::path& client_root,
                        const std::filesystem::path& ssr_server_root,
                        std::function<void()>        on_server_rebuilt)
{
  std::call_once(g_watcher_flag, [&] {
    g_watcher = std::make_unique<HttpFileWatcher>(
        client_root, ssr_server_root, std::move(on_server_rebuilt));
  });
}

void stop_file_watcher()
{
  g_watcher.reset();
}

} // namespace nprpc::impl

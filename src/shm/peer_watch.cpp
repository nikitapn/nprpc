// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include <nprpc/impl/peer_watch.hpp>

#if defined(__linux__)

#include <cerrno>
#include <cstring>
#include <mutex>
#include <thread>
#include <unordered_map>

#include <sys/epoll.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "../logging.hpp"

#ifndef SYS_pidfd_open
# define SYS_pidfd_open 434 // the same number on every architecture
#endif

namespace nprpc::impl {

namespace {

uint64_t own_pid_ns() noexcept
{
  struct stat st {};
  if (::stat("/proc/self/ns/pid", &st) != 0)
    return 0;
  return static_cast<uint64_t>(st.st_ino);
}

class Watcher
{
public:
  // Leaked on purpose, thread and all: the watcher sleeps in epoll_wait for
  // the life of the process, and a static destructor that had to wake and
  // join it would run at exit in an order nothing here controls — after a
  // channel that still names it, say.
  static Watcher& instance()
  {
    static Watcher* w = new Watcher();
    return *w;
  }

  bool ok() const noexcept { return epfd_ >= 0; }

  uint64_t add(int pidfd, std::function<void()> on_exit)
  {
    std::lock_guard lock(mutex_);
    const uint64_t token = ++next_;
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.u64 = token;
    if (::epoll_ctl(epfd_, EPOLL_CTL_ADD, pidfd, &ev) != 0) {
      ::close(pidfd);
      return 0;
    }
    entries_.emplace(token, Entry{pidfd, std::move(on_exit)});
    return token;
  }

  void remove(uint64_t token)
  {
    std::lock_guard lock(mutex_);
    auto it = entries_.find(token);
    if (it == entries_.end())
      return; // already fired
    ::epoll_ctl(epfd_, EPOLL_CTL_DEL, it->second.pidfd, nullptr);
    ::close(it->second.pidfd);
    entries_.erase(it);
  }

private:
  struct Entry {
    int pidfd;
    std::function<void()> on_exit;
  };

  Watcher()
  {
    epfd_ = ::epoll_create1(EPOLL_CLOEXEC);
    if (epfd_ < 0) {
      NPRPC_LOG_WARN("PeerWatch: epoll_create1 failed ({}); peers will be "
                     "polled",
                     std::strerror(errno));
      return;
    }
    std::thread([this] { run(); }).detach();
  }

  void run()
  {
    epoll_event events[16];
    for (;;) {
      const int n = ::epoll_wait(epfd_, events, 16, -1);
      if (n < 0) {
        if (errno == EINTR)
          continue;
        NPRPC_LOG_ERROR("PeerWatch: epoll_wait failed: {}",
                        std::strerror(errno));
        return;
      }
      for (int i = 0; i < n; ++i)
        fire(events[i].data.u64);
    }
  }

  // Under the lock, so that remove() returning means the callback is not
  // running: the channel it wakes is about to unmap the ring it would touch.
  void fire(uint64_t token)
  {
    std::lock_guard lock(mutex_);
    auto it = entries_.find(token);
    if (it == entries_.end())
      return;
    ::epoll_ctl(epfd_, EPOLL_CTL_DEL, it->second.pidfd, nullptr);
    ::close(it->second.pidfd);
    auto on_exit = std::move(it->second.on_exit);
    entries_.erase(it);
    if (on_exit)
      on_exit();
  }

  int epfd_ = -1;
  std::mutex mutex_;
  uint64_t next_ = 0;
  std::unordered_map<uint64_t, Entry> entries_;
};

} // namespace

uint64_t PeerWatch::watch(const ProcessIdentity& id,
                          std::function<void()> on_exit)
{
  if (!id.valid())
    return 0;
  // Another pid namespace: its pid names some other process here, or none.
  static const uint64_t ns = own_pid_ns();
  if (id.pid_ns != 0 && ns != 0 && id.pid_ns != ns)
    return 0;

  Watcher& w = Watcher::instance();
  if (!w.ok())
    return 0;

  const int pidfd = static_cast<int>(
      ::syscall(SYS_pidfd_open, static_cast<pid_t>(id.pid), 0u));
  if (pidfd < 0) {
    // ESRCH: already gone, which is an answer rather than a failure — but
    // reporting it needs the same thread and ordering as every other exit,
    // so let the caller's first poll find it.  Anything else (ENOSYS on an
    // old kernel) means this process cannot be watched here.
    return 0;
  }

  const uint64_t token = w.add(pidfd, std::move(on_exit));
  // The pid may have been recycled between the peer naming itself and the
  // open above, in which case the pidfd watches a stranger.  The identity's
  // start time catches that; checked after the add so that an exit between
  // the two cannot slip past both.
  if (token != 0 && !process_alive(id)) {
    w.remove(token);
    return 0;
  }
  return token;
}

void PeerWatch::unwatch(uint64_t token)
{
  if (token != 0)
    Watcher::instance().remove(token);
}

} // namespace nprpc::impl

#else // !__linux__

namespace nprpc::impl {

uint64_t PeerWatch::watch(const ProcessIdentity&, std::function<void()>)
{
  return 0;
}

void PeerWatch::unwatch(uint64_t) {}

} // namespace nprpc::impl

#endif

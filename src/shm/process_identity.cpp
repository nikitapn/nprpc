// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include <nprpc/impl/process_identity.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
# include <windows.h>
#else
# include <cerrno>
# include <signal.h>
# include <unistd.h>
#endif

#if defined(__APPLE__)
# include <sys/sysctl.h>
# include <sys/types.h>
#endif

namespace nprpc::impl {

namespace {

// Start time of `pid` as an opaque token, or 0 when it cannot be read
// (process already gone, or no permission to look).
uint64_t read_start_token(uint32_t pid) noexcept
{
#if defined(__linux__)
  // /proc/<pid>/stat field 22 is starttime (clock ticks after boot).  Fields
  // are space separated, but field 2 (comm) is the executable name in
  // parentheses and may itself contain spaces *and* ')' — so anchor the scan
  // at the LAST ')' in the line, after which field 3 begins.
  char path[64];
  std::snprintf(path, sizeof(path), "/proc/%u/stat", pid);

  std::FILE* f = std::fopen(path, "re");
  if (!f)
    return 0;

  char line[1024];
  const size_t n = std::fread(line, 1, sizeof(line) - 1, f);
  std::fclose(f);
  if (n == 0)
    return 0;
  line[n] = '\0';

  const char* p = std::strrchr(line, ')');
  if (!p)
    return 0;
  ++p;

  // starttime is field 22, i.e. the 20th field after comm (field 2).
  uint64_t value = 0;
  for (int field = 3; field <= 22; ++field) {
    while (*p == ' ')
      ++p;
    if (*p == '\0')
      return 0;
    char* end = nullptr;
    value = std::strtoull(p, &end, 10);
    if (end == p) {
      // Non-numeric field (field 3 is the single-character state); skip it.
      while (*p && *p != ' ')
        ++p;
      value = 0;
      continue;
    }
    p = end;
  }
  return value;

#elif defined(__APPLE__)
  struct kinfo_proc kp;
  std::memset(&kp, 0, sizeof(kp));
  size_t len = sizeof(kp);
  int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, static_cast<int>(pid)};

  if (::sysctl(mib, 4, &kp, &len, nullptr, 0) != 0 || len == 0)
    return 0;

  return static_cast<uint64_t>(kp.kp_proc.p_starttime.tv_sec) * 1000000ull +
         static_cast<uint64_t>(kp.kp_proc.p_starttime.tv_usec);

#elif defined(_WIN32)
  HANDLE h = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                           static_cast<DWORD>(pid));
  if (!h)
    return 0;

  FILETIME creation{}, exit_time{}, kernel{}, user{};
  const BOOL ok = ::GetProcessTimes(h, &creation, &exit_time, &kernel, &user);
  ::CloseHandle(h);
  if (!ok)
    return 0;

  return (static_cast<uint64_t>(creation.dwHighDateTime) << 32) |
         creation.dwLowDateTime;

#else
  (void)pid;
  return 0; // No start time available: liveness degrades to a pid check.
#endif
}

} // namespace

ProcessIdentity current_process_identity() noexcept
{
#if defined(_WIN32)
  const uint32_t pid = static_cast<uint32_t>(::GetCurrentProcessId());
#else
  const uint32_t pid = static_cast<uint32_t>(::getpid());
#endif
  return ProcessIdentity{pid, read_start_token(pid)};
}

bool process_alive(const ProcessIdentity& id) noexcept
{
  if (!id.valid())
    return true; // Unknown peer — never reap on a guess.

#if defined(_WIN32)
  HANDLE h = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                           static_cast<DWORD>(id.pid));
  if (!h)
    return false;
  DWORD code = 0;
  const bool running =
      ::GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
  ::CloseHandle(h);
  if (!running)
    return false;
#else
  // kill(pid, 0) only probes existence.  EPERM means the process is there but
  // owned by somebody else — still alive; only ESRCH proves it is gone.
  if (::kill(static_cast<pid_t>(id.pid), 0) != 0 && errno == ESRCH)
    return false;
#endif

  if (id.start_token == 0)
    return true; // Peer reported no start time; the pid probe is all we have.

  const uint64_t current = read_start_token(id.pid);
  if (current == 0)
    return false; // Vanished between the two probes.

  // A different start time means the pid was recycled: our peer is dead and
  // an unrelated process now wears its number.
  return current == id.start_token;
}

} // namespace nprpc::impl

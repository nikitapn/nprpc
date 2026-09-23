// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <nprpc/export.hpp>

#include <cstdint>

namespace nprpc::impl {

/**
 * @brief Identity of a process on this machine, stable against PID reuse.
 *
 * A shared-memory peer has no connection that breaks when it dies: if the
 * process is killed the rings just go quiet, which is indistinguishable from
 * an idle peer.  The peer therefore names itself at handshake time and the
 * other side polls whether that process is still running.
 *
 * The pid alone is not enough — the kernel recycles pids, so a dead peer's
 * pid can come back as an unrelated process and keep a dead session looking
 * alive forever.  start_token is the process' start time as reported by the
 * OS (clock ticks since boot on Linux, start timeval on macOS, creation
 * FILETIME on Windows); the (pid, start_token) pair is unique for as long as
 * the machine stays up.
 *
 * Both are only meaningful inside one pid namespace.  Processes in different
 * containers can share a /dev/shm (a directory bind-mounted into each), and
 * there "pid 1, started at tick N" names a different process in every
 * container — probing it from the wrong one finds some other process, or
 * none, and calls a live peer dead.  pid_ns is the inode of the owner's
 * /proc/self/ns/pid; a reader in another namespace cannot judge, and says so
 * by treating the owner as alive.  0 means not recorded (a writer from before
 * this field, or no namespaces on this OS): judged by pid as before.
 */
struct ProcessIdentity {
  uint32_t pid{0};
  uint64_t start_token{0};
  uint64_t pid_ns{0};

  bool valid() const noexcept { return pid != 0; }
};

/**
 * @brief Identity of the calling process.
 *
 * start_token is 0 if the OS start time could not be read; liveness then
 * falls back to a plain pid check.
 */
NPRPC_API ProcessIdentity current_process_identity() noexcept;

/**
 * @brief Whether the process named by @p id is still running.
 *
 * An invalid identity (pid == 0 — peer never reported one) counts as alive:
 * an unknown peer must never be reaped, only a provably dead one.  So does
 * one from another pid namespace, for the same reason: its pid means nothing
 * here.
 */
NPRPC_API bool process_alive(const ProcessIdentity& id) noexcept;

} // namespace nprpc::impl

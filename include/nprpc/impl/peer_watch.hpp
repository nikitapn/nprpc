// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <nprpc/impl/process_identity.hpp>

#include <cstdint>
#include <functional>

namespace nprpc::impl {

/**
 * @brief Tells a shared-memory channel the moment its peer process ends.
 *
 * A killed peer leaves its rings mapped and silent, which looks exactly like
 * an idle one, so the channel used to find out by asking — waking its read
 * thread ten times a second, forever, on every connection, to check a pid.
 * On a desktop where every window is a client that is most of what an idle
 * process does.
 *
 * Linux can say it instead: a pidfd becomes readable when its process exits.
 * One thread for the whole process holds them all in an epoll set and sleeps
 * until one fires, so a quiet connection costs nothing at all.
 *
 * Elsewhere, on a kernel before 5.3, or for a peer in another pid namespace
 * (whose pid means nothing here), watch() returns 0 and the caller goes on
 * polling.
 */
class NPRPC_API PeerWatch
{
public:
  /**
   * @brief Calls @p on_exit once, on the watcher thread, when @p id ends.
   *
   * Also immediately (still on the watcher thread) if it has already ended,
   * including a pid that was recycled before it could be opened.  Must be
   * quick and must not call unwatch(): set a flag, wake whoever sleeps.
   *
   * @return A token for unwatch(), or 0 if this process cannot be watched
   * here and the caller has to poll.
   */
  static uint64_t watch(const ProcessIdentity& id, std::function<void()> on_exit);

  /**
   * @brief Stops watching.  Once it returns, on_exit is not running and never
   * will.  0 is ignored.  Never call from on_exit.
   */
  static void unwatch(uint64_t token);
};

} // namespace nprpc::impl

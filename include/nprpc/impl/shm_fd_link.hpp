// Copyright (c) 2021-2026, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <nprpc/export.hpp>
#include <nprpc/unique_fd.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace nprpc::impl {

/**
 * @brief The file-descriptor side channel of one shared-memory channel.
 *
 * Shared memory can carry bytes but not descriptors; only the kernel can
 * hand an open file from one process to another.  So a channel whose
 * messages may carry `fd` arguments also has a Unix socket to the peer, and
 * the descriptors of a message travel over it (SCM_RIGHTS) while the message
 * itself still goes through the ring.  Messages without descriptors never
 * touch the socket.
 *
 * Each socket record is tagged with the ring slot of the message it belongs
 * to, and the message is marked in the ring (kSlotHasFds).  The sender sends
 * the record before it commits the message, and a Unix socket queues it at
 * the receiver the moment sendmsg returns, so by the time a reader sees the
 * marked message its descriptors are already waiting.  Records can still
 * arrive out of ring order — two threads may send in one order and commit in
 * the other — so the reader keeps those that came early until their message
 * turns up.
 *
 * The socket is a SOCK_SEQPACKET, so one record is one sendmsg with its
 * descriptors attached, and nothing needs framing.
 *
 * Linux only, where the socket lives next to the rings in /dev/shm and so
 * reaches every process (or container) that can open them.  Elsewhere no
 * link is ever established and fd calls are refused.
 */
class NPRPC_API ShmFdLink
{
public:
  /// How many descriptors one message may carry (the kernel's SCM_MAX_FD).
  static constexpr size_t kMaxFdsPerMessage = 253;

  ShmFdLink() = default;
  ShmFdLink(const ShmFdLink&) = delete;
  ShmFdLink& operator=(const ShmFdLink&) = delete;

  /// Where the listener named `listener_name` accepts side-channel
  /// connections; empty where there is no such thing.
  static std::string socket_path(const std::string& listener_name);

  /// Client side: connects to the listener's socket and binds the
  /// connection to `channel_id`, which the listener must have accepted
  /// already.  Null if the server offers no side channel (an older one, or
  /// another OS) or refused this one; the channel then works as before,
  /// without descriptors.
  static std::shared_ptr<ShmFdLink> connect(const std::string& listener_name,
                                            const std::string& channel_id);

  /// Server side: gives the link its connected socket.  Once only; false if
  /// it already has one.
  bool attach(UniqueFd socket);

  /// Whether descriptors can go over this link.
  bool attached() const noexcept
  {
    return attached_.load(std::memory_order_acquire);
  }

  /// Sends `fds` for the message in ring slot `slot`, then empties `fds`:
  /// the peer has them now, and this process' copies are closed.  Any
  /// thread.  On failure `fds` are closed all the same.
  bool send(uint16_t slot, std::vector<UniqueFd>& fds);

  /// The descriptors sent for the message in ring slot `slot`.  Reader
  /// thread only.  Empty if they do not come, which only a peer that marks
  /// a message without sending them can cause.
  std::vector<UniqueFd> receive(uint16_t slot);

private:
  UniqueFd socket_;
  std::atomic<bool> attached_{false};

  // Reader thread only: records that arrived before their message.
  std::vector<std::pair<uint16_t, std::vector<UniqueFd>>> early_;

  // Waits for one record; false on timeout or a broken socket.
  bool receive_record(uint16_t& slot,
                      std::vector<UniqueFd>& fds,
                      std::chrono::milliseconds timeout);
};

/**
 * @brief First record on a side-channel connection: which channel it is for.
 *
 * The listener answers with one uint32_t, kAccepted or not, so a client
 * never offers descriptor passing on a link the server did not take.
 */
struct ShmFdHello {
  static constexpr uint32_t MAGIC = 0x46444C4B; // "FDLK"
  static constexpr uint32_t VERSION = 1;
  static constexpr uint32_t kAccepted = 1;

  uint32_t magic = MAGIC;
  uint32_t version = VERSION;
  char channel_id[64] = {};
};

} // namespace nprpc::impl

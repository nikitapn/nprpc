// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include <iostream>
#include <nprpc/common.hpp>
#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/impl/peer_watch.hpp>
#include <nprpc/impl/shared_memory_channel.hpp>

#include "../logging.hpp"

namespace nprpc::impl {

namespace {

// The configured sizes, made safe to create rings with.
//
// Two things have to hold and neither is worth a failed startup: a ring big
// enough to be worth having, and a message the ring can actually carry.  The
// claim reserves size + 1 bytes (see claim_slot_and_payload), so a message
// exactly the size of the ring never fits however empty the ring is — which
// would be a send that fails forever rather than a send that waits.
std::pair<size_t, size_t> validated_ring_sizes()
{
  constexpr size_t kMinRing = 64 * 1024;

  size_t ring = g_cfg.shm_ring_buffer_size;
  if (ring < kMinRing) {
    NPRPC_LOG_WARN("shm_ring_buffer_size {} is below the {} byte minimum; "
                   "using the minimum",
                   ring, kMinRing);
    ring = kMinRing;
  }

  size_t max_message = g_cfg.shm_max_message_size;
  if (max_message == 0 || max_message >= ring) {
    const size_t clamped = ring / 2;
    NPRPC_LOG_WARN("shm_max_message_size {} does not fit a {} byte ring; "
                   "using {}",
                   max_message, ring, clamped);
    max_message = clamped;
  }

  return {ring, max_message};
}

} // namespace

SharedMemoryChannel::SharedMemoryChannel(boost::asio::io_context& ioc,
                                         const std::string& channel_id,
                                         bool is_server,
                                         bool create_rings)
    : channel_id_(channel_id)
    , is_server_(is_server)
    , ioc_(ioc)
{
  // Server writes to s2c, reads from c2s
  // Client writes to c2s, reads from s2c
  send_ring_name_ = make_shm_name(channel_id, is_server ? "s2c" : "c2s");
  recv_ring_name_ = make_shm_name(channel_id, is_server ? "c2s" : "s2c");

  try {
    if (create_rings) {
      // Create new ring buffers (continuous, variable-sized)
      const auto [ring, max_message] = validated_ring_sizes();

      send_ring_ =
          LockFreeRingBuffer::create(send_ring_name_, ring, max_message);

      recv_ring_ =
          LockFreeRingBuffer::create(recv_ring_name_, ring, max_message);

      NPRPC_LOG_INFO("Created ring buffers: {} , {} ({} bytes each, messages "
                     "up to {} bytes)",
                     send_ring_name_, recv_ring_name_, ring, max_message);
    } else {
      // Open existing ring buffers
      send_ring_ = LockFreeRingBuffer::open(send_ring_name_);
      recv_ring_ = LockFreeRingBuffer::open(recv_ring_name_);

      NPRPC_LOG_INFO("Opened ring buffers: {} , {}", send_ring_name_, recv_ring_name_);
    }

    // Whoever created the rings decided this, and wrote it where both ends
    // can read it.  Taken from the ring we send into, since that is the one
    // this side's own writes have to fit.
    max_message_size_ = send_ring_->header()->max_message_size;

    exchange_identities();

    // NOTE: read_thread_ is NOT started here.
    // Call start_reading() after wiring up on_data_received[_view].

  } catch (const std::exception& e) {
    NPRPC_LOG_ERROR("Failed to create/open ring buffers: {}", e.what());
    cleanup_rings();
    throw std::runtime_error(
        std::string("SharedMemoryChannel initialization failed: ") + e.what());
  }
}

void SharedMemoryChannel::start_reading()
{
  std::lock_guard lk(read_thread_mut_);
  if (read_thread_) {
    return; // already started
  }
  read_thread_ = std::make_unique<std::thread>([this]() { read_loop(); });
}

bool SharedMemoryChannel::stop_reading()
{
  running_ = false;
  // Before anything else: the watcher's callback wakes recv_ring_, which the
  // destructor is about to let go of.
  unwatch_peer();

  std::unique_ptr<std::thread> thread;
  {
    std::lock_guard lk(read_thread_mut_);
    if (!read_thread_)
      return true; // never started, or already stopped by someone else
    if (read_thread_->get_id() == std::this_thread::get_id())
      return false; // a callback is stopping its own channel — cannot join
    thread = std::move(read_thread_);
  }

  // Wake the reader, which may be asleep with no timeout at all.  It cannot
  // miss this: it reads its wake token before it checks running_ (see
  // read_loop), so a wake that lands after that check still ends the sleep.
  if (recv_ring_) {
    try {
      recv_ring_->wake_readers();
    } catch (...) {
      // Ignore errors - shared memory might already be destroyed
    }
  }

  if (thread->joinable())
    thread->join();

  return true;
}

void SharedMemoryChannel::exchange_identities()
{
  const auto self = current_process_identity();

  if (send_ring_) {
    send_ring_->header()->publish_writer(self);
  }

  // Whoever already produces into the ring we read is our peer.  The server
  // creates the rings and publishes before the client can open them, so a
  // client always learns the server here; the server's own client is still
  // absent at this point and arrives via the handshake instead.
  if (recv_ring_ && !peer_process_.valid()) {
    const auto writer = recv_ring_->header()->writer();
    if (writer.valid()) {
      peer_process_ = writer;
      NPRPC_LOG_INFO("SharedMemoryChannel {}: peer process is pid {}",
                     channel_id_, writer.pid);
      watch_peer();
    }
  }
}

void SharedMemoryChannel::set_peer_process(const ProcessIdentity& id)
{
  peer_process_ = id;
  watch_peer();
}

void SharedMemoryChannel::watch_peer()
{
  if (!peer_process_.valid() || peer_watch_.load() != 0)
    return;
  const uint64_t token = PeerWatch::watch(peer_process_, [this] {
    peer_exited_.store(true, std::memory_order_seq_cst);
    if (recv_ring_)
      recv_ring_->wake_readers();
  });
  peer_watch_.store(token, std::memory_order_seq_cst);
  // The reader may already be asleep with a timeout chosen for polling; it
  // picks the longer one up on its next turn, which costs one more wake.
}

void SharedMemoryChannel::unwatch_peer()
{
  PeerWatch::unwatch(peer_watch_.exchange(0));
}

void SharedMemoryChannel::need_ticks()
{
  // Only the edge wakes anyone: while the tick is running this is one atomic
  // exchange.  The reader reads its wake token before it looks at
  // ticks_wanted_, so a wake sent after that look is not lost.
  if (!ticks_wanted_.exchange(true, std::memory_order_seq_cst) && recv_ring_)
    recv_ring_->wake_readers();
}

std::chrono::milliseconds SharedMemoryChannel::idle_timeout() const
{
#if defined(__linux__)
  if (ticks_wanted_.load(std::memory_order_seq_cst))
    return kPollInterval;
  // A peer nobody is told about has to be asked about.
  if (!peer_lost_ && on_peer_lost && peer_watch_.load() == 0)
    return kPollInterval;
  return LockFreeRingBuffer::kWaitForever;
#else
  // The condvar wait elsewhere can lose a wake sent before it sleeps, so it
  // never sleeps for long without looking again.
  return std::chrono::milliseconds(100);
#endif
}

void SharedMemoryChannel::poll_periodic()
{
  // A peer that is known to be gone — its process ended, or it detached —
  // is acted on at once, not on the next tick: nothing else may wake this
  // thread again.  Two loads, so the busy path does not notice.
  if (!peer_lost_ && on_peer_lost &&
      (peer_exited_.load(std::memory_order_acquire) ||
       (recv_ring_ && recv_ring_->header()->writer_detached.load(
                          std::memory_order_acquire) != 0))) {
    peer_lost_ = true;
    on_peer_lost();
  }

  // Runs once per read-loop iteration, so the gate is all the busy path
  // pays: one steady_clock read (vDSO, no syscall) per message.
  const auto now = std::chrono::steady_clock::now();
  if (now - last_poll_ < kPollInterval)
    return;
  last_poll_ = now;

  if (on_periodic_poll) {
    // Cleared before the poll and set again after it if there is still work:
    // a need_ticks() racing with this either lands after the clear, and
    // stands, or before it — and then its work was queued before the poll
    // looked, so the poll says so.
    ticks_wanted_.store(false, std::memory_order_seq_cst);
    if (on_periodic_poll())
      ticks_wanted_.store(true, std::memory_order_seq_cst);
  }

  if (peer_lost_ || !on_peer_lost)
    return;

  if (peer_alive())
    return;

  peer_lost_ = true;
  on_peer_lost();
}

bool SharedMemoryChannel::peer_alive() const
{
  if (!recv_ring_)
    return false;

  // Clean shutdown on the other side: it told us before going.
  if (recv_ring_->header()->writer_detached.load(std::memory_order_acquire) != 0)
    return false;

  // Unclean: the process is simply not there any more.
  return process_alive(peer_process_);
}

SharedMemoryChannel::~SharedMemoryChannel()
{
  // Announce the close to the peer before the mapping goes away, so it can
  // reap its side immediately instead of waiting for its next liveness poll.
  if (send_ring_) {
    try {
      send_ring_->header()->writer_detached.store(1u, std::memory_order_release);
      // And wake it: the peer's reader may be asleep with no timeout, and
      // nothing else is going to arrive on this ring to rouse it.
      send_ring_->wake_readers();
    } catch (...) {
      // Ignore - shared memory might already be gone
    }
  }

  stop_reading();
  unwatch_peer(); // stop_reading() returns early on the reader's own thread

  cleanup_rings();
}

bool SharedMemoryChannel::send(const void* data, uint32_t size)
{
  if (!send_ring_ || size > max_message_size_) {
    return false;
  }

  try {
    bool sent = send_ring_->try_write(data, size);

    if (!sent) {
      NPRPC_LOG_WARN("SharedMemoryChannel: Ring buffer full, message dropped");
    }

    return sent;

  } catch (const std::exception& e) {
    NPRPC_LOG_ERROR("SharedMemoryChannel send error: {}", e.what());
    return false;
  }
}

LockFreeRingBuffer::WriteReservation
SharedMemoryChannel::reserve_write(size_t max_size)
{
  if (!send_ring_) {
    return LockFreeRingBuffer::WriteReservation{nullptr, 0, 0, false};
  }
  return send_ring_->try_reserve_write(max_size);
}

void SharedMemoryChannel::commit_write(
    const LockFreeRingBuffer::WriteReservation& reservation, size_t actual_size)
{
  if (send_ring_) {
    send_ring_->commit_write(reservation, actual_size);
  }
}

void SharedMemoryChannel::commit_write(
    const LockFreeRingBuffer::WriteReservation& reservation,
    size_t actual_size,
    std::vector<UniqueFd>& fds)
{
  if (fds.empty()) {
    commit_write(reservation, actual_size);
    return;
  }

  // The record goes first: once the message is visible the reader expects
  // its descriptors to be waiting.
  const bool sent =
      fd_link_ &&
      fd_link_->send(static_cast<uint16_t>(reservation.slot_idx), fds);
  if (!sent) {
    NPRPC_LOG_ERROR("SharedMemoryChannel {}: could not send file descriptors",
                    channel_id_);
    fds.clear();
  }

  if (send_ring_)
    send_ring_->commit_write(reservation, actual_size, sent);
}

void SharedMemoryChannel::abort_write(
    const LockFreeRingBuffer::WriteReservation& reservation)
{
  if (send_ring_) {
    send_ring_->abort_write(reservation);
  }
}

void SharedMemoryChannel::commit_read(const LockFreeRingBuffer::ReadView& view)
{
  if (recv_ring_) {
    recv_ring_->commit_read(view);
  }
}

void SharedMemoryChannel::read_loop()
{
  while (running_) {
    try {
      // First, before looking at anything a wake could be about: running_,
      // the peer flags, ticks_wanted_.  Sleeping against this token, rather
      // than whatever the counter says when the sleep begins, is what lets
      // the sleep below have no timeout — a wake sent after any of those
      // checks moved the counter past it, and the futex returns at once.
      const uint32_t token = recv_ring_->wake_token();
      if (!running_)
        break;

      // Housekeeping (peer liveness, request expiry).  Rate-limited inside,
      // and deliberately not confined to the idle branches: a peer that
      // answers other requests while stuck on one still has to be noticed.
      poll_periodic();

      const auto timeout = idle_timeout();

      // Try zero-copy read first if callback is set
      if (on_data_received_view) {
        recv_ring_->wait_for_readable(timeout, token);

        auto view = recv_ring_->try_read_view();
        if (view) {
          if (view.has_fds && fd_link_)
            received_fds_ =
                fd_link_->receive(static_cast<uint16_t>(view.slot_idx));
          // Callback copies the data; commit the read immediately so that
          // the next try_read_view() call sees fresh data, not this message
          // again.
          on_data_received_view(view);
          received_fds_.clear();
          this->commit_read(view);
        }
        continue;
      }

      // Fallback to copy-based read.  The bound is the ring we read from,
      // which the peer sized; allocated here rather than in the constructor
      // because a channel with a zero-copy consumer never reaches this line
      // and should not carry the buffer for a path it does not take.
      const size_t recv_max = recv_ring_->header()->max_message_size;
      if (recv_buffer_.size() < recv_max)
        recv_buffer_.resize(recv_max);

      recv_ring_->wait_for_readable(timeout, token);
      size_t bytes_read =
          recv_ring_->try_read(recv_buffer_.data(), recv_buffer_.size());

      if (bytes_read > 0) {
        // Validate message size (security check)
        if (bytes_read > recv_max) {
          NPRPC_LOG_ERROR("SharedMemoryChannel: Rejected oversized message: {} bytes (max: {} bytes)", bytes_read, recv_max);
          continue;
        }

        // Message received, post to io_context
        std::vector<char> data(recv_buffer_.begin(),
                               recv_buffer_.begin() + bytes_read);

        boost::asio::post(ioc_, [this, data = std::move(data)]() mutable {
          if (on_data_received) {
            on_data_received(std::move(data));
          }
        });
      }
    } catch (const std::exception& e) {
      if (running_) {
        NPRPC_LOG_ERROR("SharedMemoryChannel read error: {}", e.what());
      }
      break;
    }
  }
}

void SharedMemoryChannel::cleanup_rings()
{
  send_ring_.reset();
  recv_ring_.reset();

  // Only the creator (server) should remove the shared memory
  if (is_server_) {
    try {
      NPRPC_LOG_INFO("Cleaning up ring buffers: {} and {}", send_ring_name_, recv_ring_name_);
    } catch (const std::exception& e) {
      // Ignore cleanup errors
      NPRPC_LOG_WARN("Cleanup error (ignored): {}", e.what());
    }
  }
}

} // namespace nprpc::impl

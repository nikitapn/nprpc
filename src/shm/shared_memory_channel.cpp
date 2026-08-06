// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include <iostream>
#include <nprpc/common.hpp>
#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/impl/shared_memory_channel.hpp>

#include "../logging.hpp"

namespace nprpc::impl {

SharedMemoryChannel::SharedMemoryChannel(boost::asio::io_context& ioc,
                                         const std::string& channel_id,
                                         bool is_server,
                                         bool create_rings)
    : channel_id_(channel_id)
    , is_server_(is_server)
    , ioc_(ioc)
    , recv_buffer_(MAX_MESSAGE_SIZE)
{
  // Server writes to s2c, reads from c2s
  // Client writes to c2s, reads from s2c
  send_ring_name_ = make_shm_name(channel_id, is_server ? "s2c" : "c2s");
  recv_ring_name_ = make_shm_name(channel_id, is_server ? "c2s" : "s2c");

  try {
    if (create_rings) {
      // Create new ring buffers (continuous, variable-sized)
      send_ring_ =
          LockFreeRingBuffer::create(send_ring_name_, RING_BUFFER_SIZE);

      recv_ring_ =
          LockFreeRingBuffer::create(recv_ring_name_, RING_BUFFER_SIZE);

      NPRPC_LOG_INFO("Created ring buffers: {} , {} ({} bytes each)", send_ring_name_, recv_ring_name_, RING_BUFFER_SIZE);
    } else {
      // Open existing ring buffers
      send_ring_ = LockFreeRingBuffer::open(send_ring_name_);
      recv_ring_ = LockFreeRingBuffer::open(recv_ring_name_);

      NPRPC_LOG_INFO("Opened ring buffers: {} , {}", send_ring_name_, recv_ring_name_);
    }

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

  std::unique_ptr<std::thread> thread;
  {
    std::lock_guard lk(read_thread_mut_);
    if (!read_thread_)
      return true; // never started, or already stopped by someone else
    if (read_thread_->get_id() == std::this_thread::get_id())
      return false; // a callback is stopping its own channel — cannot join
    thread = std::move(read_thread_);
  }

  // Nudge the reader in case it is asleep on the condvar.  Even if it misses
  // the notification the wait times out after 100 ms and the loop rechecks
  // running_, so the join below is bounded either way.
  if (recv_ring_) {
    try {
      recv_ring_->header()->data_available.notify_all();
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
    auto* header = send_ring_->header();
    header->writer_start_token.store(self.start_token,
                                     std::memory_order_relaxed);
    // Release: a non-zero pid tells the consumer both fields are readable.
    header->writer_pid.store(self.pid, std::memory_order_release);
  }

  // Whoever already produces into the ring we read is our peer.  The server
  // creates the rings and publishes before the client can open them, so a
  // client always learns the server here; the server's own client is still
  // absent at this point and arrives via the handshake instead.
  if (recv_ring_ && !peer_process_.valid()) {
    auto* header = recv_ring_->header();
    const uint32_t pid = header->writer_pid.load(std::memory_order_acquire);
    if (pid != 0) {
      peer_process_ = ProcessIdentity{
          pid, header->writer_start_token.load(std::memory_order_relaxed)};
      NPRPC_LOG_INFO("SharedMemoryChannel {}: peer process is pid {}",
                     channel_id_, pid);
    }
  }
}

void SharedMemoryChannel::poll_periodic()
{
  // Runs once per read-loop iteration, so the gate is all the busy path
  // pays: one steady_clock read (vDSO, no syscall) per message.
  const auto now = std::chrono::steady_clock::now();
  if (now - last_poll_ < kPollInterval)
    return;
  last_poll_ = now;

  if (on_periodic_poll)
    on_periodic_poll();

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
    } catch (...) {
      // Ignore - shared memory might already be gone
    }
  }

  stop_reading();

  cleanup_rings();
}

bool SharedMemoryChannel::send(const void* data, uint32_t size)
{
  if (!send_ring_ || size > MAX_MESSAGE_SIZE) {
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
  // std::cout << "[nprpc][D] reserve_write on ring: " << send_ring_name_ <<
  // std::endl;
  return send_ring_->try_reserve_write(max_size);
}

void SharedMemoryChannel::commit_write(
    const LockFreeRingBuffer::WriteReservation& reservation, size_t actual_size)
{
  if (send_ring_) {
    // std::cout << "[nprpc][D] commit_write on ring: " << send_ring_name_
    //           << " size=" << actual_size << std::endl;
    send_ring_->commit_write(reservation, actual_size);
  }
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
    // std::string side = is_server_ ? "SERVER" : "CLIENT";
    // std::cout << "[nprpc][D] " << side << " commit_read on ring: " <<
    // recv_ring_name_ << std::endl;
    recv_ring_->commit_read(view);
  }
}

void SharedMemoryChannel::read_loop()
{
  // std::string side = is_server_ ? "SERVER" : "CLIENT";
  // std::cout << "[nprpc][D] " << side << " read_loop starting for recv_ring:
  // "
  // << recv_ring_name_ << std::endl;
  while (running_) {
    try {
      // Housekeeping (peer liveness, request expiry).  Rate-limited inside,
      // and deliberately not confined to the idle branches: a peer that
      // answers other requests while stuck on one still has to be noticed.
      poll_periodic();

      // Try zero-copy read first if callback is set
      if (on_data_received_view) {
        recv_ring_->wait_for_readable(std::chrono::milliseconds(100));

        auto view = recv_ring_->try_read_view();
        // std::cout << "Zero-copy read view attempt returned valid=" <<
        // view.valid << std::endl;
        if (view) {
          // Callback copies the data; commit the read immediately so that
          // the next try_read_view() call sees fresh data, not this message
          // again.
          on_data_received_view(view);
          this->commit_read(view);
        }
        continue;
      }

      // std::cout << "Falling back to copy-based read" << std::endl;

      // Fallback to copy-based read
      // Blocking read with timeout (allows checking running_ flag)
      size_t bytes_read = recv_ring_->read_with_timeout(
          recv_buffer_.data(), recv_buffer_.size(),
          std::chrono::milliseconds(100));

      if (bytes_read > 0) {
        // Validate message size (security check)
        if (bytes_read > MAX_MESSAGE_SIZE) {
          NPRPC_LOG_ERROR("SharedMemoryChannel: Rejected oversized message: {} bytes (max: {} bytes)", bytes_read, MAX_MESSAGE_SIZE);
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

  std::cout << "SharedMemoryChannel read thread exiting" << std::endl;
}

void SharedMemoryChannel::cleanup_rings()
{
  std::cout << "Cleaning up SharedMemoryChannel rings for channel: "
            << channel_id_ << std::endl;
  send_ring_.reset();
  recv_ring_.reset();

  // Only the creator (server) should remove the shared memory
  if (is_server_) {
    try {
      std::cout << "Cleaned up ring buffers: " << send_ring_name_ << ", "
                << recv_ring_name_ << std::endl;
    } catch (const std::exception& e) {
      // Ignore cleanup errors
      std::cout << "Cleanup error (ignored): " << e.what() << std::endl;
    }
  }
}

} // namespace nprpc::impl

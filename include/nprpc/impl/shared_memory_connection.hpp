// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <memory>
#include <mutex>

#include <nprpc/impl/misc/mutex.hpp>
#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/impl/shared_memory_channel.hpp>

namespace nprpc::impl {

/**
 * @brief RPC connection using shared memory transport (Boost.Interprocess)
 *
 * Similar to SocketConnection but uses message queues instead of TCP.
 * Provides same security guarantees (message size limits, pending request
 * limits).
 */
class SharedMemoryConnection
    : public Session,
      public CommonConnection<SharedMemoryConnection>,
      public std::enable_shared_from_this<SharedMemoryConnection>
{
  boost::asio::io_context& ioc_;
  std::unique_ptr<SharedMemoryChannel> channel_;
  // SpinMutex mutex_;
  // AdaptiveSpinMutex mutex_;
  std::mutex mutex_;
  uint32_t pending_requests_ = 0;

  // For zero-copy writes: stores the active reservation
  LockFreeRingBuffer::WriteReservation active_reservation_;

protected:
  virtual void timeout_action() final;

public:
  auto get_executor() noexcept { return ioc_.get_executor(); }

  void add_work(std::shared_ptr<IOWork> w);

  void send_receive(flat_buffer& buffer, uint32_t timeout_ms) override;

  void send_receive_async(
      flat_buffer&& buffer,
      std::optional<std::function<void(const boost::system::error_code&,
                                       flat_buffer&)>>&& completion_handler,
      uint32_t timeout_ms) override;

  //--------------------------------------------------------------------------
  // Zero-copy API
  //--------------------------------------------------------------------------

  /**
   * @brief Reserve space in send ring buffer for zero-copy write
   *
   * @param buffer Buffer to set up in view mode
   * @param max_size Maximum message size
   * @param endpoint Optional endpoint pointer for buffer growth fallback
   * @return true if reservation succeeded, false if buffer full
   */
  bool prepare_write_buffer(flat_buffer& buffer,
                            size_t max_size,
                            const EndPoint* endpoint = nullptr);

  /**
   * @brief Get the underlying channel for advanced operations
   */
  SharedMemoryChannel* channel() { return channel_.get(); }

  SharedMemoryConnection(const EndPoint& endpoint,
                         boost::asio::io_context& ioc);
  ~SharedMemoryConnection();
};

} // namespace nprpc::impl

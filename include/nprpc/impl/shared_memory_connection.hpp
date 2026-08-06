// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <atomic>
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
  // Guards wq_ from CommonConnection
  std::mutex mutex_;

  std::atomic<bool> server_dead_{false};

  // Insert work into wq_ ordered by slot_idx (wrap-aware 16-bit).  Must be
  // called with mutex_ held.  Arms the front work's timeout if the new
  // entry is at the front.
  void enqueue_ordered(std::shared_ptr<IOWork> w);

  // The server process went away.  Runs on the channel's read thread: fails
  // everything that is waiting on a reply that can no longer come, then
  // hands the part that needs to join that thread to the io_context.
  void on_server_dead();

  // Fail every queued request with `ec`.  Callers blocked in send_receive()
  // wake up and throw ExceptionCommFailure, as they do on a broken socket.
  void fail_all_pending(const boost::system::error_code& ec);

  // Time out requests whose reply never came.  Driven by the channel's
  // periodic poll instead of a timer per request, which on this transport
  // cost about a fifth of the round-trip latency; the deadline itself is a
  // clock read at enqueue.  Granularity is the poll interval (500 ms).
  void sweep_expired_requests();

  // mutex_ held: drop a reply whose caller already timed out.  Returns true
  // if the reply was consumed and must not be delivered.
  bool consume_reply_for_abandoned();

protected:
  virtual void timeout_action() final;

public:
  void shutdown() override;

  // False once the server process died or closed the channel.
  bool server_alive() const noexcept { return !server_dead_.load(); }

  auto get_executor() noexcept { return ioc_.get_executor(); }

  void send_receive(flat_buffer& buffer, uint32_t timeout_ms) override;

  nprpc::Task<> send_receive_coro(flat_buffer& buffer,
                                          uint32_t timeout_ms,
                                          std::stop_token st = {}) override
  {
    // FIXME: true async implementation that doesn't block the thread
    send_receive(buffer, timeout_ms);
    co_return;
  }

  void send_receive_async(
      flat_buffer&& buffer,
      std::optional<std::function<void(const boost::system::error_code&,
                                       flat_buffer&)>>&& completion_handler,
      uint32_t timeout_ms) override;

  // Fire-and-forget stream frames (must not wait for a reply / touch wq_).
  void send_stream_message(flat_buffer&& buffer) override;

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

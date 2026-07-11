// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/system_timer.hpp>
#include <boost/date_time/posix_time/ptime.hpp>

#include <chrono>
#include <memory>
#include <stop_token>

#include <nprpc/basic.hpp>
#include <nprpc/common.hpp>
#include <nprpc/flat_buffer.hpp>
#include <nprpc/session_context.h>
#include <nprpc/task.hpp>

namespace nprpc::impl {

class Session
{
protected:
  SessionContext ctx_;

  // Indirection cell shared with StreamManager's send callbacks (set up in
  // Session::Session(), before any shared_ptr owns *this — shared_from_this()
  // is not usable there). bind_self() fills in the real weak_ptr once the
  // caller has wrapped the concrete session in a shared_ptr; the callbacks
  // capture this cell (not `this`) so they can safely detect — via
  // weak_ptr::lock(), with no use-after-free/TOCTOU window — that the
  // session has since been destroyed, instead of dispatching a virtual call
  // through a dangling pointer. See nprpc issue: pure-virtual-method-called
  // crash when writing to a stream after its session died uncleanly.
  std::shared_ptr<std::weak_ptr<Session>> self_cell_ =
      std::make_shared<std::weak_ptr<Session>>();

  // Owns the StreamManager ctx_.stream_manager points at. External holders
  // of that raw pointer (the Swift NPRPCStreamWriter/NPRPCStreamReader, via
  // the C bridge) take their own share via StreamManager::external_retain()/
  // external_release() — see nprpc issue about the StreamManager leak this
  // replaces. Session dropping its own share here does NOT free the object
  // out from under a still-alive Swift-side writer/reader; it just stops
  // being one of the owners. self_cell_ above is what tells StreamManager
  // the *session* has died, independent of who still owns the object itself.
  std::shared_ptr<StreamManager> stream_manager_owner_;

  boost::asio::system_timer timeout_timer_;
  boost::asio::system_timer inactive_timer_;
  std::chrono::system_clock::duration timeout_ =
      std::chrono::milliseconds(1000);

  std::atomic<bool> closed_{false};

  void close();
  bool is_closed() { return closed_.load(); }

  virtual void timeout_action() = 0;

public:
  virtual void send_receive(flat_buffer& buffer, uint32_t timeout_ms) = 0;

  // Coroutine variant — suspends the caller until the response arrives.
  // Default implementation wraps the blocking send_receive(); concrete
  // transports (UringClientConnection) override with a true suspension.
  // Pass a stop_token to enable cancellation; throws OperationCancelled.
  virtual nprpc::Task<> send_receive_coro(flat_buffer& buffer,
                                          uint32_t timeout_ms,
                                          std::stop_token st = {}) = 0;

  virtual void send_receive_async(
      flat_buffer&& buffer,
      std::optional<std::function<void(const boost::system::error_code&,
                                       flat_buffer&)>>&& completion_handler,
      uint32_t timeout_ms) = 0;

  /**
   * @brief Send unreliable datagram (fire-and-forget, no response expected)
   *
   * Default implementation falls back to send_receive_async with no handler.
   * QUIC sessions override this to use DATAGRAM extension.
   *
   * @param buffer Message buffer to send (moved)
   * @return true if datagram was sent successfully
   */
  virtual bool send_datagram(flat_buffer&& buffer)
  {
    // Default: fall back to async send (fire-and-forget)
    send_receive_async(std::move(buffer), std::nullopt, 0);
    return true;
  }

  void set_timeout(uint32_t timeout_ms)
  {
    timeout_ = std::chrono::milliseconds(timeout_ms);
    timeout_timer_.expires_after(timeout_);
  }

  // monitors every asynchronous operation
  // and calls timeout_action() if the operation is not completed
  // within the specified timeout
  void start_timeout_timer() noexcept
  {
    if (is_closed())
      return;

    const auto now = std::chrono::system_clock::now();
    if (timeout_timer_.expiry() <= now) {
      timeout_action();

      try {
        timeout_timer_.expires_after(
            std::chrono::system_clock::duration::max());
      } catch (boost::system::system_error& ec) {
        // nothing we can do here
      }
    }
    timeout_timer_.async_wait(std::bind(&Session::start_timeout_timer, this));
  }
  const EndPoint& remote_endpoint() const noexcept
  {
    return ctx_.remote_endpoint;
  }
  SessionContext& ctx() noexcept { return ctx_; }

  // Must be called once by the creator, right after wrapping the concrete
  // session in a shared_ptr (e.g. `session->bind_self(session);`), and
  // before the session is exposed to any I/O. Cannot be done from within
  // Session's own constructor — no shared_ptr owns *this yet at that point.
  void bind_self(std::weak_ptr<Session> self) noexcept
  {
    *self_cell_ = std::move(self);
  }
  // Handles incoming request in rx_buffer and prepares response in tx_buffer
  // in some cases rx_buffer and tx_buffer can be the same buffer
  // Returns true if a reply should be sent, false for fire-and-forget messages
  bool handle_request(nprpc::flat_buffer& rx_buffer,
                      nprpc::flat_buffer& tx_buffer);

  // Send a message for streaming (async, fire-and-forget)
  // Default implementation uses send_receive_async with no handler
  virtual void send_stream_message(flat_buffer&& buffer)
  {
    send_receive_async(std::move(buffer), std::nullopt, 0);
  }

  // Send a control message on main stream (for stream control: complete/error/cancel)
  // Default implementation is same as send_stream_message
  // QUIC sessions override to send on main bidirectional stream, not native data streams
  virtual void send_main_stream_message(flat_buffer&& buffer)
  {
    send_stream_message(std::move(buffer));
  }

  virtual void shutdown()
  {
    closed_.store(true);
    try {
      timeout_timer_.cancel();
    } catch (...) {
    }
    try {
      inactive_timer_.cancel();
    } catch (...) {
    }
  }

  Session(boost::asio::any_io_executor executor);

  virtual ~Session() = default;
};

} // namespace nprpc::impl

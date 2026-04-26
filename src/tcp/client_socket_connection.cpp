// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT
//
// Coroutine-based ASIO TCP client connection.
//
// Design (fully-multiplexed, request-ID based):
//
//  write_loop()    — awaitable<void> that drains write_queue_ one message at a
//                    time.  Restarted whenever a new message is enqueued while
//                    it is not already running.
//
//  read_loop()     — awaitable<void> that runs forever, reading one complete
//                    length-prefixed message per iteration and dispatching it to
//                    the matching pending_request via its request_id.  A stray
//                    reply (unknown id) is logged and discarded.
//
//  send_receive()  — synchronous bridge: calls send_receive_async() then waits
//                    on an atomic_bool until the response arrives.
//
//  send_receive_coro() — coroutine bridge via a custom awaiter that calls
//                    send_receive_async() and suspends until the response.
//
//  send_receive_async() — ALL outgoing RPCs go through here.  Even fire-and-
//                    forget calls (nullopt handler) register an empty handler so
//                    the server's reply is silently consumed rather than logged
//                    as unexpected.  A per-request steady_timer enforces the
//                    caller's timeout.
//
//  This mirrors the WebSocketSession pattern and eliminates all the races that
//  existed with the old single-slot pending_rpc_ design.

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/asio/write.hpp>

#include <nprpc/common.hpp>
#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/impl/stream_manager.hpp>

#include "helper.hpp"
#include "../logging.hpp"

namespace nprpc::impl {

namespace net = boost::asio;

// ---------------------------------------------------------------------------
// Helpers: inject/extract request_id from the flat::Header at offset 0.
// ---------------------------------------------------------------------------
void SocketConnection::inject_request_id(flat_buffer& buf, uint32_t id)
{
  if (buf.size() >= sizeof(impl::flat::Header)) {
    impl::flat::Header_Direct hdr(buf, 0);
    hdr.request_id() = id;
  }
}

uint32_t SocketConnection::extract_request_id(const flat_buffer& buf)
{
  if (buf.size() >= sizeof(impl::flat::Header)) {
    impl::flat::Header_Direct hdr(const_cast<flat_buffer&>(buf), 0);
    return hdr.request_id();
  }
  return 0;
}

// ---------------------------------------------------------------------------
// fail_all_pending — notify every in-flight caller with the given error.
// Must be called on the socket strand.
// ---------------------------------------------------------------------------
void SocketConnection::fail_all_pending(boost::system::error_code ec)
{
  for (auto& [id, req] : pending_requests_) {
    if (req.timeout_timer)
      req.timeout_timer->cancel();
    flat_buffer empty{};
    req.completion_handler(ec, empty);
  }
  pending_requests_.clear();
}

// ---------------------------------------------------------------------------
// enqueue_write — append to write_queue_ and kick write_loop if idle.
// Must be called on the socket strand.
// ---------------------------------------------------------------------------
void SocketConnection::enqueue_write(
    flat_buffer&& buf,
    std::function<void(const boost::system::error_code&)>&& completion_handler)
{
  write_queue_.emplace_back(std::move(buf), std::move(completion_handler));
  if (!write_loop_running_) {
    write_loop_running_ = true;
    net::co_spawn(
        socket_.get_executor(),
        [self = shared_from_this()]() -> net::awaitable<void> {
          co_await self->write_loop();
        },
        net::detached);
  }
}

// ---------------------------------------------------------------------------
// write_loop — drain write_queue_ one message at a time.
// ---------------------------------------------------------------------------
net::awaitable<void> SocketConnection::write_loop()
{
  while (!write_queue_.empty()) {
    auto msg = std::move(write_queue_.front());
    write_queue_.pop_front();

    auto [ec, _] = co_await net::async_write(
        socket_, msg.buffer.cdata(), net::as_tuple(net::use_awaitable));

    if (msg.completion_handler)
      msg.completion_handler(ec);

    if (ec) {
      fail(ec, "tcp client: write_loop");
      fail_all_pending(ec);
      co_return;
    }
  }
  write_loop_running_ = false;
}

// ---------------------------------------------------------------------------
// read_loop — continuous read; dispatches each reply to its pending_request.
//
// Key invariant: rx_buffer_ may contain leftover bytes from a previous
// iteration (when async_read_some fetched more than one message at once).
// We never discard rx_buffer_ at the top of the loop; instead we consume
// exactly msg_size bytes per message and leave the rest for the next pass.
// ---------------------------------------------------------------------------
net::awaitable<void> SocketConnection::read_loop()
{
  for (;;) {
    // Ensure we have at least 4 bytes to read msg_size.
    while (rx_buffer_.size() < sizeof(uint32_t)) {
      auto [ec, n] = co_await socket_.async_read_some(
          rx_buffer_.prepare(4096), net::as_tuple(net::use_awaitable));
      if (ec || n == 0) {
        fail_all_pending(ec ? ec : boost::asio::error::eof);
        co_return;
      }
      rx_buffer_.commit(n);
    }

    uint32_t msg_size;
    std::memcpy(&msg_size, rx_buffer_.data_ptr(), sizeof(uint32_t));
    if (msg_size > max_message_size) {
      NPRPC_LOG_ERROR("tcp client: oversized message {} bytes, closing", msg_size);
      fail_all_pending(boost::asio::error::message_size);
      co_return;
    }

    // Read the remainder of the message body if not yet buffered.
    if (rx_buffer_.size() < msg_size) {
      auto [ec, n] = co_await net::async_read(
          socket_,
          rx_buffer_.prepare(msg_size - rx_buffer_.size()),
          net::as_tuple(net::use_awaitable));
      if (ec) {
        fail_all_pending(ec);
        co_return;
      }
      rx_buffer_.commit(n);
    }

    const uint32_t request_id = extract_request_id(rx_buffer_);

    auto it = pending_requests_.find(request_id);
    if (it != pending_requests_.end()) {
      if (it->second.timeout_timer)
        it->second.timeout_timer->cancel();

      // Copy exactly msg_size bytes into a fresh response buffer so that
      // any subsequent message bytes stay in rx_buffer_ for the next pass.
      flat_buffer response;
      auto dst = response.prepare(msg_size);
      std::memcpy(dst.data(), rx_buffer_.data_ptr(), msg_size);
      response.commit(msg_size);
      rx_buffer_.consume(msg_size);

      auto handler = std::move(it->second.completion_handler);
      pending_requests_.erase(it);
      // Dispatch handler via post so any exception it throws (or any
      // resumption of a suspended coroutine that then throws) does not
      // propagate back into read_loop and terminate the process.
      net::post(socket_.get_executor(),
                [h = std::move(handler), resp = std::move(response)]() mutable {
                  h(boost::system::error_code{}, resp);
                });
    } else {
      NPRPC_LOG_WARN("tcp client: received unexpected response (request_id={}), discarding", request_id);
      rx_buffer_.consume(msg_size);  // skip unknown message, keep the loop alive
    }
  }
}

// ---------------------------------------------------------------------------
// send_receive — synchronous bridge via atomic_bool.
// ---------------------------------------------------------------------------
void SocketConnection::send_receive(flat_buffer& buffer, uint32_t timeout_ms)
{
  std::atomic_bool done{false};
  boost::system::error_code result_ec;

  send_receive_async(
      std::move(buffer),
      [&](const boost::system::error_code& ec, flat_buffer& response) {
        result_ec = ec;
        if (!ec)
          buffer = std::move(response);
        done.store(true, std::memory_order_release);
        done.notify_one();
      },
      timeout_ms);

  done.wait(false);

  if (result_ec)
    throw nprpc::ExceptionCommFailure();
}

// ---------------------------------------------------------------------------
// send_receive_coro — suspends the nprpc::Task<> caller until the response.
// ---------------------------------------------------------------------------
nprpc::Task<> SocketConnection::send_receive_coro(flat_buffer&   buffer,
                                                    uint32_t       timeout_ms,
                                                    std::stop_token /*st*/)
{
  struct Awaiter {
    SocketConnection&          self;
    flat_buffer&               buf;
    uint32_t                   timeout_ms;
    boost::system::error_code  result_ec;

    bool await_ready() noexcept { return false; }

    void await_suspend(std::coroutine_handle<> h) noexcept
    {
      self.send_receive_async(
          std::move(buf),
          [this, h](const boost::system::error_code& ec,
                    flat_buffer& response) mutable {
            result_ec = ec;
            if (!ec)
              buf = std::move(response);
            h.resume();
          },
          timeout_ms);
    }

    void await_resume()
    {
      if (result_ec)
        throw nprpc::ExceptionCommFailure();
    }
  };

  co_await Awaiter{*this, buffer, timeout_ms};
}

// ---------------------------------------------------------------------------
// send_receive_async — the single entry point for ALL outgoing RPCs.
//
// Even fire-and-forget (nullopt handler) goes through the queue with an empty
// handler so the server's reply is consumed gracefully rather than triggering
// "unexpected response" in the read loop.
// ---------------------------------------------------------------------------
void SocketConnection::send_receive_async(
    flat_buffer&& buffer,
    std::optional<std::function<void(const boost::system::error_code&,
                                     flat_buffer&)>>&& completion_handler,
    uint32_t timeout_ms)
{
  const uint32_t request_id = generate_request_id();
  inject_request_id(buffer, request_id);

  net::post(
      socket_.get_executor(),
      [self = shared_from_this(),
       buf = std::move(buffer),
       completion_handler = std::move(completion_handler),
       request_id,
       timeout_ms]() mutable {
        // Register the pending request before enqueuing the write so the
        // response can never arrive before we're ready to handle it.
        auto handler = completion_handler
                           ? std::move(*completion_handler)
                           : pending_request::empty_handler();

        auto& req = self->pending_requests_
                        .emplace(request_id, pending_request{std::move(handler)})
                        .first->second;

        // Optional per-request timeout timer.
        if (timeout_ms > 0) {
          auto timer = std::make_shared<net::steady_timer>(
              self->socket_.get_executor());
          timer->expires_after(std::chrono::milliseconds(timeout_ms));
          req.timeout_timer = timer;
          timer->async_wait(
              [self, request_id, timer](boost::system::error_code ec) {
                if (!ec) { // natural expiry (not cancelled)
                  auto it = self->pending_requests_.find(request_id);
                  if (it != self->pending_requests_.end()) {
                    flat_buffer empty{};
                    it->second.completion_handler(
                        boost::asio::error::timed_out, empty);
                    self->pending_requests_.erase(it);
                  }
                }
              });
        }

        // Write-completion callback: fail the pending request if the write
        // itself fails (e.g. connection dropped before server received it).
        auto write_completion =
            [self, request_id](const boost::system::error_code& ec) {
              if (ec) {
                auto it = self->pending_requests_.find(request_id);
                if (it != self->pending_requests_.end()) {
                  if (it->second.timeout_timer)
                    it->second.timeout_timer->cancel();
                  flat_buffer empty{};
                  it->second.completion_handler(ec, empty);
                  self->pending_requests_.erase(it);
                }
              }
            };

        self->enqueue_write(std::move(buf), std::move(write_completion));
      });
}

// ---------------------------------------------------------------------------
// Constructor — sets socket options and starts the read/write loops.
// ---------------------------------------------------------------------------
SocketConnection::SocketConnection(const EndPoint&                endpoint,
                                   boost::asio::ip::tcp::socket&& socket)
    : Session(socket.get_executor())
    , socket_{std::move(socket)}
{
  ctx_.remote_endpoint = endpoint;
  endpoint_ = sync_socket_connect(endpoint, socket_);

  constexpr int kLargeBuf = 4 * 1024 * 1024;
  boost::system::error_code ec;
  socket_.set_option(boost::asio::socket_base::receive_buffer_size(kLargeBuf), ec);
  socket_.set_option(boost::asio::socket_base::send_buffer_size(kLargeBuf), ec);

}

void SocketConnection::start()
{
  net::co_spawn(
      socket_.get_executor(),
      [self = shared_from_this()]() -> net::awaitable<void> {
        co_await self->read_loop();
      },
      net::detached);
}

} // namespace nprpc::impl

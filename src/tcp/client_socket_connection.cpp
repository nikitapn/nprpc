// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT
//
// Coroutine-based ASIO TCP client connection.
//
// Design (half-duplex: client sends request, then reads exactly one reply):
//
//  do_rpc()     — awaitable<void> that (1) async_writes the request, arms an
//                 optional timeout timer, then (2) co_awaits read_response().
//                 On timeout the timer callback sets timed_out=true and cancels
//                 the socket; do_rpc() throws ExceptionTimeout afterward.
//
//  read_response() — reads exactly one message off the socket and places it in
//                 pending_rpc_.buf_out.  Sets pending_rpc_.error on any failure.
//
//  send_receive()        — bridges the blocking caller via co_spawn+use_future.
//  send_receive_coro()   — custom awaiter that co_spawns do_rpc and resumes the
//                          nprpc::Task<> continuation when it finishes.
//  send_receive_async()  — fire-and-forget: posts async_write; with a handler,
//                          spawns do_rpc and invokes the handler on completion.

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
// do_rpc — write request, then park until read_loop delivers the response.
// ---------------------------------------------------------------------------
net::awaitable<void> SocketConnection::do_rpc(flat_buffer& buf,
                                               uint32_t     timeout_ms)
{
  auto [ec1, _] = co_await net::async_write(
      socket_, buf.cdata(), net::as_tuple(net::use_awaitable));
  if (ec1) {
    fail(ec1, "tcp client: do_rpc write");
    throw nprpc::ExceptionCommFailure();
  }

  pending_rpc_.buf_out   = &buf;
  pending_rpc_.error     = false;
  pending_rpc_.timed_out = false;

  // Arm timeout: when the timer fires on the strand it sets timed_out and
  // cancels the pending async_read_some inside read_response, which then
  // returns with operation_aborted.  The lambda only acts when !ec (natural
  // expiry); a cancel() of the timer itself arrives with operation_aborted
  // and is ignored — so no dangling-reference issue on success path.
  net::steady_timer timer(socket_.get_executor());
  if (timeout_ms) {
    timer.expires_after(std::chrono::milliseconds(timeout_ms));
    timer.async_wait([this](boost::system::error_code ec) {
      if (!ec) {
        pending_rpc_.timed_out = true;
        boost::system::error_code sec;
        socket_.cancel(sec);
      }
    });
  }

  co_await read_response();

  // Disarm the timer (no-op if it already fired).
  timer.cancel();

  const bool timed_out = pending_rpc_.timed_out;
  const bool had_error = pending_rpc_.error;
  pending_rpc_ = {};

  if (timed_out)   throw nprpc::ExceptionTimeout();
  if (had_error)   throw nprpc::ExceptionCommFailure();
}

// ---------------------------------------------------------------------------
// read_response — reads one complete RPC reply off the socket.
// Stream messages are routed to ctx_.stream_manager and the read continues
// until an actual RPC response or a comm error is found.
// ---------------------------------------------------------------------------
net::awaitable<void> SocketConnection::read_response()
{
  flat_buffer buf{flat_buffer::default_initial_size()};

  // Read at least the 4-byte size prefix (often the full small header).
  auto [ec, n] = co_await socket_.async_read_some(
      buf.prepare(1024), net::as_tuple(net::use_awaitable));
  if (ec || n < 4) {
    pending_rpc_.error = true;
    co_return;
  }
  buf.commit(n);

  uint32_t msg_size;
  std::memcpy(&msg_size, buf.data_ptr(), sizeof(uint32_t));
  if (msg_size > max_message_size) {
    NPRPC_LOG_ERROR("tcp client: oversized message {} bytes, closing", msg_size);
    pending_rpc_.error = true;
    co_return;
  }

  // Read the remainder of the body if it hasn't arrived yet.
  if (buf.size() < msg_size) {
    auto [ec2, n2] = co_await net::async_read(
        socket_,
        buf.prepare(msg_size - buf.size()),
        net::as_tuple(net::use_awaitable));
    if (ec2) {
      fail(ec2, "tcp client: read_response body");
      pending_rpc_.error = true;
      co_return;
    }
    buf.commit(n2);
  }

  if (pending_rpc_.buf_out) {
    *pending_rpc_.buf_out = std::move(buf);
  } else {
    NPRPC_LOG_WARN("tcp client: received unexpected response (no pending RPC)");
  }
}

// ---------------------------------------------------------------------------
// send_receive — blocks the calling (application) thread via co_spawn+use_future.
// ---------------------------------------------------------------------------
void SocketConnection::send_receive(flat_buffer& buffer, uint32_t timeout_ms)
{
  std::unique_lock<std::mutex> lk(rpc_mutex_);
  net::co_spawn(socket_.get_executor(), do_rpc(buffer, timeout_ms), net::use_future)
    .get();
}

// ---------------------------------------------------------------------------
// send_receive_coro — true coroutine suspension via a custom nprpc::Task<>
// awaiter that bridges into the Asio awaitable<void> do_rpc().
// ---------------------------------------------------------------------------
nprpc::Task<> SocketConnection::send_receive_coro(flat_buffer&   buffer,
                                                    uint32_t       timeout_ms,
                                                    std::stop_token /*st*/)
{
  struct Awaiter {
    SocketConnection& self;
    flat_buffer&      buf;
    uint32_t          timeout_ms;
    std::exception_ptr ep;

    bool await_ready() noexcept { return false; }

    void await_suspend(std::coroutine_handle<> h) noexcept
    {
      net::co_spawn(
          self.socket_.get_executor(),
          self.do_rpc(buf, timeout_ms),
          [h, &ep = ep](std::exception_ptr e) mutable {
            ep = std::move(e);
            h.resume();
          });
    }

    void await_resume()
    {
      if (ep) std::rethrow_exception(ep);
    }
  };

  co_await Awaiter{*this, buffer, timeout_ms};
}

// ---------------------------------------------------------------------------
// send_receive_async
// ---------------------------------------------------------------------------
void SocketConnection::send_receive_async(
    flat_buffer&& buffer,
    std::optional<std::function<void(const boost::system::error_code&,
                                     flat_buffer&)>>&& completion_handler,
    uint32_t timeout_ms)
{
  if (!completion_handler) {
    // Fire-and-forget: write and ignore the outcome.
    auto buf = std::make_shared<flat_buffer>(std::move(buffer));
    net::post(socket_.get_executor(),
              [self = shared_from_this(), buf]() {
                net::async_write(
                    self->socket_, buf->cdata(),
                    [self, buf](boost::system::error_code ec, std::size_t) {
                      if (ec)
                        fail(ec, "tcp client: fire-and-forget write");
                    });
              });
    return;
  }

  // Async RPC with a response handler: spawn do_rpc and invoke the handler.
  auto buf = std::make_shared<flat_buffer>(std::move(buffer));
  auto hdl = std::make_shared<
      std::function<void(const boost::system::error_code&, flat_buffer&)>>(
      std::move(*completion_handler));

  net::co_spawn(
      socket_.get_executor(),
      [self = shared_from_this(), buf, hdl, timeout_ms]() -> net::awaitable<void> {
        try {
          co_await self->do_rpc(*buf, timeout_ms);
          (*hdl)(boost::system::error_code{}, *buf);
        } catch (...) {
          (*hdl)(boost::asio::error::connection_reset, *buf);
        }
      },
      net::detached);
}

// ---------------------------------------------------------------------------
// Constructor — sets socket options.
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

} // namespace nprpc::impl

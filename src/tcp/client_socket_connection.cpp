// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT
//
// Coroutine-based ASIO TCP client connection.
//
// Design:
//  read_loop()  — spawned with detached on construction; runs forever on the
//                 socket strand, routing each message either to the single
//                 pending_rpc_ slot (RPC responses) or to ctx_.stream_manager
//                 (streaming messages).
//
//  do_rpc()     — awaitable<void> that (1) async_writes the request, then
//                 (2) parks on a strand-local steady_timer until read_loop()
//                 cancels it after placing the response in pending_rpc_.buf_out.
//                 All operations run on the socket strand, so pending_rpc_ is
//                 accessed without any mutex.
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

  // Park on a timer; read_loop() cancels it once the response arrives.
  // timeout_ms == 0 → wait forever (time_point::max()).
  auto expiry = timeout_ms
      ? net::steady_timer::clock_type::now() +
            std::chrono::milliseconds(timeout_ms)
      : net::steady_timer::time_point::max();

  net::steady_timer timer(socket_.get_executor(), expiry);
  pending_rpc_.buf_out = &buf;
  pending_rpc_.wakeup  = &timer;
  pending_rpc_.error   = false;

  co_await timer.async_wait(net::as_tuple(net::use_awaitable));

  // read_loop clears pending_rpc_.wakeup when it delivers a response or
  // signals a comm error.  If wakeup is still set here the timer fired
  // naturally → timeout.  Cancel the socket so read_loop exits cleanly.
  if (pending_rpc_.wakeup) {
    pending_rpc_ = {};
    boost::system::error_code ec;
    socket_.cancel(ec);
    throw nprpc::ExceptionTimeout();
  }

  const bool had_error = pending_rpc_.error;
  pending_rpc_ = {};

  if (had_error) throw nprpc::ExceptionCommFailure();
}

// ---------------------------------------------------------------------------
// read_loop — continuous message reader on the socket strand.
// ---------------------------------------------------------------------------
net::awaitable<void> SocketConnection::read_loop()
{
  flat_buffer buf{flat_buffer::default_initial_size()};

  for (;;) {
    buf.consume(buf.size());

    // Read at least the 4-byte size prefix (often the full small header).
    auto [ec, n] = co_await socket_.async_read_some(
        buf.prepare(1024), net::as_tuple(net::use_awaitable));
    if (ec || n < 4) break;
    buf.commit(n);

    uint32_t msg_size;
    std::memcpy(&msg_size, buf.data_ptr(), sizeof(uint32_t));
    if (msg_size > max_message_size) {
      NPRPC_LOG_ERROR("tcp client: oversized message {} bytes, closing",
                      msg_size);
      break;
    }

    // Read the remainder of the body if it hasn't arrived yet.
    if (buf.size() < msg_size) {
      auto [ec2, n2] = co_await net::async_read(
          socket_,
          buf.prepare(msg_size - buf.size()),
          net::as_tuple(net::use_awaitable));
      if (ec2) {
        fail(ec2, "tcp client: read_loop body");
        break;
      }
      buf.commit(n2);
    }

    auto* header = reinterpret_cast<const flat::Header*>(buf.data_ptr());
    const auto msg_id = header->msg_id;

    // Stream messages from the server: route to stream_manager.
    if (msg_id == MessageId::StreamDataChunk    ||
        msg_id == MessageId::StreamCompletion   ||
        msg_id == MessageId::StreamError        ||
        msg_id == MessageId::StreamCancellation ||
        msg_id == MessageId::StreamWindowUpdate) {
      if (ctx_.stream_manager) {
        if (msg_id == MessageId::StreamDataChunk) {
          flat_buffer chunk;
          chunk.prepare(buf.size());
          chunk.commit(buf.size());
          std::memcpy(chunk.data().data(), buf.data().data(), buf.size());
          ctx_.stream_manager->on_chunk_received(std::move(chunk));
        } else if (msg_id == MessageId::StreamCompletion) {
          flat::StreamComplete_Direct msg(buf, sizeof(flat::Header));
          ctx_.stream_manager->on_stream_complete(msg.stream_id(),
                                                   msg.final_sequence());
        } else if (msg_id == MessageId::StreamError) {
          flat::StreamError_Direct msg(buf, sizeof(flat::Header));
          flat_buffer empty;
          ctx_.stream_manager->on_stream_error(msg.stream_id(),
                                                msg.error_code(),
                                                std::move(empty));
        } else if (msg_id == MessageId::StreamCancellation) {
          flat::StreamCancel_Direct msg(buf, sizeof(flat::Header));
          ctx_.stream_manager->on_stream_cancel(msg.stream_id());
        } else {  // StreamWindowUpdate
          flat::StreamWindowUpdate_Direct msg(buf, sizeof(flat::Header));
          ctx_.stream_manager->on_window_update(msg.stream_id(), msg.credits());
        }
      }
      continue;
    }

    // RPC response: deliver to the pending slot.
    if (pending_rpc_.buf_out && pending_rpc_.wakeup) {
      *pending_rpc_.buf_out = std::move(buf);
      pending_rpc_.wakeup->cancel();
      pending_rpc_.wakeup = nullptr;
      buf = flat_buffer{flat_buffer::default_initial_size()};
    } else {
      NPRPC_LOG_WARN("tcp client: received unexpected response (no pending RPC)");
    }
  }

  // Signal any waiting RPC with an error on connection loss.
  if (pending_rpc_.wakeup) {
    pending_rpc_.error = true;
    pending_rpc_.wakeup->cancel();
    pending_rpc_.wakeup = nullptr;
  }
}

// ---------------------------------------------------------------------------
// send_receive — blocks the calling (application) thread via co_spawn+use_future.
// ---------------------------------------------------------------------------
void SocketConnection::send_receive(flat_buffer& buffer, uint32_t timeout_ms)
{
  std::unique_lock<std::mutex> lk(rpc_mutex_);
  try {
    net::co_spawn(socket_.get_executor(), do_rpc(buffer, timeout_ms), net::use_future)
        .get();
  } catch (const nprpc::ExceptionCommFailure&) {
    throw;
  } catch (...) {
    throw nprpc::ExceptionCommFailure();
  }
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
// Constructor — sets socket options and spawns the read loop.
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
  socket_.set_option(boost::asio::socket_base::receive_buffer_size(kLargeBuf),
                     ec);
  socket_.set_option(boost::asio::socket_base::send_buffer_size(kLargeBuf),
                     ec);

  // Spawn the continuous read loop on the socket strand.
  // Note: start_timeout_timer() is intentionally NOT called here.  The new
  // coroutine design manages each RPC's lifetime inside do_rpc() via a
  // per-call steady_timer.  Calling start_timeout_timer() would invoke
  // expires_after(system_clock::duration::max()) whose arithmetic overflows
  // the system_clock time_point, resulting in an immediate timeout_action()
  // that cancels the socket and kills read_loop before any RPC is issued.
  net::co_spawn(socket_.get_executor(), read_loop(), net::detached);
}

} // namespace nprpc::impl

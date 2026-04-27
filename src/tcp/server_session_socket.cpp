// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/core/bind_handler.hpp>
#include <cassert>
#include <deque>
#include <functional>
#include <memory>
#include <utility>

#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/impl/session.hpp>
#include <nprpc/nprpc.hpp>

#include "../logging.hpp"
#include <nprpc/common.hpp>

namespace nprpc::impl {

namespace net = boost::asio;
using tcp     = net::ip::tcp;

// ---------------------------------------------------------------------------
// Session_Socket — coroutine-based server TCP session.
//
// read_loop() and write_loop() run as Asio coroutines on the socket strand
// (every Session_Socket gets its own strand via make_strand in the Acceptor).
// Because both loops execute exclusively on that strand, write_queue_ and
// write_loop_running_ need no mutex.
//
// Stream messages from the client (uploads, window updates, cancellations) are
// handled entirely inside handle_request() / session.cpp — they return
// needs_reply = false, so no response is enqueued.
//
// Stream messages from the server (chunks sent to the client) arrive via
// send_stream_message(), which posts enqueue_write() to the strand.
// ---------------------------------------------------------------------------
class Session_Socket : public nprpc::impl::Session,
                       public std::enable_shared_from_this<Session_Socket>
{
  tcp::socket socket_;
  flat_buffer rx_buffer_{flat_buffer::default_initial_size()};
  flat_buffer tx_buffer_{flat_buffer::default_initial_size()};

  // Write queue — operated exclusively on the socket strand; no mutex needed.
  std::deque<flat_buffer> write_queue_;
  bool                    write_loop_running_ = false;

  // ---- helpers -------------------------------------------------------------

  // Enqueue a buffer and (re)start the write loop if it is not already running.
  // Must be called on the socket strand.
  void enqueue_write(flat_buffer&& buf)
  {
    write_queue_.push_back(std::move(buf));
    if (!write_loop_running_) {
      write_loop_running_ = true;
      net::co_spawn(socket_.get_executor(),
        [self = shared_from_this()]() -> net::awaitable<void> {
          co_await self->write_loop();
        },
        net::detached);
    }
  }

  // ---- coroutines ----------------------------------------------------------

  // Drains write_queue_ one buffer at a time.  Resets write_loop_running_ when
  // the queue is exhausted so the next enqueue_write() re-arms it.
  net::awaitable<void> write_loop()
  {
    while (!write_queue_.empty()) {
      flat_buffer fb = std::move(write_queue_.front());
      write_queue_.pop_front();
      auto [ec, _] = co_await net::async_write(
          socket_, fb.cdata(), net::as_tuple(net::use_awaitable));
      if (ec) {
        fail(ec, "tcp server: write_loop");
        break;
      }
    }
    write_loop_running_ = false;
  }

  // Main read loop: reads one complete message per iteration and dispatches it.
  //
  // Key invariant: rx_buffer_ may hold leftover bytes from a previous
  // async_read_some that fetched more than one message.  We consume exactly
  // msg_size bytes per iteration so those bytes are preserved for the next pass.
  net::awaitable<void> read_loop()
  {
    for (;;) {
      // Ensure we have at least the 4-byte size prefix.
      while (rx_buffer_.size() < sizeof(uint32_t)) {
        auto [ec, n] = co_await socket_.async_read_some(
            rx_buffer_.prepare(4096), net::as_tuple(net::use_awaitable));
        if (ec || n == 0) co_return;
        rx_buffer_.commit(n);
      }

      uint32_t msg_size;
      std::memcpy(&msg_size, rx_buffer_.data_ptr(), sizeof(uint32_t));
      if (msg_size > max_message_size) {
        NPRPC_LOG_ERROR("tcp server: oversized message {} bytes, closing",
                        msg_size);
        co_return;
      }

      // Read the remainder of the message body if it hasn't arrived yet.
      if (rx_buffer_.size() < msg_size) {
        auto [ec, n] = co_await net::async_read(
            socket_,
            rx_buffer_.prepare(msg_size - rx_buffer_.size()),
            net::as_tuple(net::use_awaitable));
        if (ec) {
          fail(ec, "tcp server: read_loop body");
          co_return;
        }
        rx_buffer_.commit(n);
      }

      // Extract exactly msg_size bytes into a fresh buffer so that:
      //  1. handle_request sees the right size (not size + next messages)
      //  2. StreamDataChunk can safely std::move() the buffer without
      //     stealing bytes that belong to subsequent messages.
      flat_buffer msg_buf;
      auto dst = msg_buf.prepare(msg_size);
      std::memcpy(dst.data(), rx_buffer_.data_ptr(), msg_size);
      msg_buf.commit(msg_size);
      rx_buffer_.consume(msg_size);

      tx_buffer_.consume(tx_buffer_.size());
      bool needs_reply = handle_request(msg_buf, tx_buffer_);
      if (needs_reply)
        enqueue_write(std::move(tx_buffer_));
    }
  }

  // ---- Session interface stubs (server-side sessions never initiate RPCs) --

  void timeout_action() final { /* no-op for TCP server sessions */ }

  void send_receive(flat_buffer&, uint32_t) override { assert(false); }
  nprpc::Task<> send_receive_coro(flat_buffer& buffer,
                                          uint32_t timeout_ms,
                                          std::stop_token st = {}) override
  {
    send_receive(buffer, timeout_ms);
    co_return;
  }

  void send_receive_async(
      flat_buffer&&,
      std::optional<std::function<void(const boost::system::error_code&,
                                       flat_buffer&)>>&&,
      uint32_t) override
  {
    assert(false);
  }

  // Called by the stream_manager send callback when the server produces stream
  // chunks destined for this client.  The post() ensures the enqueue runs on
  // the socket strand even if called from the stream executor thread.
  void send_stream_message(flat_buffer&& buffer) override
  {
    net::post(
        socket_.get_executor(),
        [self = shared_from_this(), buf = std::move(buffer)]() mutable {
          self->enqueue_write(std::move(buf));
        });
  }

public:
  void run()
  {
    net::co_spawn(socket_.get_executor(),
      [self = shared_from_this()]() -> net::awaitable<void> {
        co_await self->read_loop();
      },
      net::detached);
  }

  explicit Session_Socket(tcp::socket&& socket)
      : Session(socket.get_executor())
      , socket_(std::move(socket))
  {
    socket_.set_option(net::ip::tcp::no_delay(true));

    constexpr int kLargeBuf = 4 * 1024 * 1024;
    boost::system::error_code ec;
    socket_.set_option(net::socket_base::receive_buffer_size(kLargeBuf), ec);
    socket_.set_option(net::socket_base::send_buffer_size(kLargeBuf), ec);

    auto ep = socket_.remote_endpoint();
    ctx_.remote_endpoint =
        EndPoint(EndPointType::TcpPrivate,
                 ep.address().to_v4().to_string(), ep.port());
  }
};

class Acceptor : public std::enable_shared_from_this<Acceptor>
{
  net::io_context& ioc_;
  tcp::acceptor acceptor_;
  bool running_ = true;

public:
  void on_accept(const boost::system::error_code& ec, tcp::socket socket)
  {
    if (ec) {
      if (ec != boost::asio::error::operation_aborted)
        fail(ec, "accept");
      return;
    }
    if (!running_)
      return;
    std::make_shared<Session_Socket>(std::move(socket))->run();
    do_accept();
  }

  void do_accept()
  {
    if (!running_)
      return;
    acceptor_.async_accept(net::make_strand(ioc_),
                           boost::beast::bind_front_handler(
                               &Acceptor::on_accept, shared_from_this()));
  }

  void stop()
  {
    running_ = false;
    boost::system::error_code ec;
    acceptor_.cancel(ec);
    acceptor_.close(ec);
  }

  Acceptor(net::io_context& ioc, unsigned short port)
      : ioc_(ioc)
      , acceptor_(ioc, tcp::endpoint(tcp::v4(), port))
  {
  }
};

static std::shared_ptr<Acceptor> g_tcp_acceptor;

// Forward declarations for the raw-epoll alternative (server_epoll.cpp)
extern void init_socket_epoll(unsigned short port);
extern void stop_socket_epoll();

// Forward declarations for the io_uring alternative (server_uring.cpp)
extern void init_socket_uring(unsigned short port);
extern void stop_socket_uring();

void init_socket(net::io_context& ioc)
{
  NPRPC_LOG_INFO("init_socket called with g_cfg.listen_tcp_port={}", g_cfg.listen_tcp_port);
  if (g_cfg.listen_tcp_port == 0) {
    NPRPC_LOG_INFO("TCP listen port is not set, skipping socket server "
                   "initialization.");
    return;
  }
  if (g_cfg.use_uring_tcp) {
    init_socket_uring(g_cfg.listen_tcp_port);
    return;
  }
  if (g_cfg.use_epoll_tcp) {
    init_socket_epoll(g_cfg.listen_tcp_port);
    return;
  }
  g_tcp_acceptor = std::make_shared<Acceptor>(ioc, g_cfg.listen_tcp_port);
  g_tcp_acceptor->do_accept();
  NPRPC_LOG_INFO("TCP listener started on port {}", g_cfg.listen_tcp_port);
}

void stop_socket_listener()
{
  if (g_cfg.use_uring_tcp) {
    stop_socket_uring();
    return;
  }
  if (g_cfg.use_epoll_tcp) {
    stop_socket_epoll();
    return;
  }
  if (g_tcp_acceptor) {
    g_tcp_acceptor->stop();
    g_tcp_acceptor.reset();
  }
}

} // namespace nprpc::impl

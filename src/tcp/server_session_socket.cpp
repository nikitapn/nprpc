// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/core/bind_handler.hpp>
#include <cassert>
#include <cstdlib>
#include <deque>
#include <functional>
#include <iostream>
#include <memory>
#include <string_view>
#include <thread>
#include <utility>

#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/impl/session.hpp>
#include <nprpc/nprpc.hpp>

#include "../logging.hpp"
#include <nprpc/common.hpp>

namespace nprpc::impl {
class Session_Socket : public nprpc::impl::Session,
                       public std::enable_shared_from_this<Session_Socket>
{
  struct work {
    virtual void operator()() = 0;
    virtual ~work() = default;
  };

  tcp::socket socket_;
  std::deque<std::unique_ptr<work>> write_queue_;

  flat_buffer rx_buffer_{flat_buffer::default_initial_size()};
  flat_buffer tx_buffer_{flat_buffer::default_initial_size()};
  std::uint32_t size_to_read_ = 0;
public:
  // Simple socket sessions are not duplex, it's assumed that client is also
  // can accept new connetions from the server, so we don't need to handle
  // duplex communication
  virtual void timeout_action() final { assert(false); }

  virtual void send_receive(flat_buffer&, uint32_t) override { assert(false); }

  virtual void send_receive_async(
      flat_buffer&&,
      std::optional<
          std::function<void(const boost::system::error_code&,
                             flat_buffer&)>>&& /* completion_handler */,
      uint32_t) override
  {
    assert(false);
  }

  // Override for streaming: queue chunk to be sent asynchronously
  virtual void send_stream_message(flat_buffer&& buffer) override
  {
    // Capture buffer and self in a work item
    auto self = shared_from_this();
    auto buf = std::make_shared<flat_buffer>(std::move(buffer));
    
    // Post to the socket's executor to ensure thread safety
    boost::asio::post(socket_.get_executor(), [self, buf]() {
      struct stream_work : work {
        std::shared_ptr<Session_Socket> session;
        std::shared_ptr<flat_buffer> buffer;
        
        stream_work(std::shared_ptr<Session_Socket> s, std::shared_ptr<flat_buffer> b)
          : session(std::move(s)), buffer(std::move(b)) {}
        
        void operator()() override {
          boost::asio::async_write(
              session->socket_, buffer->cdata(),
              [self = session, buf = buffer](boost::system::error_code ec, std::size_t) {
                if (ec) {
                  fail(ec, "stream write");
                  return;
                }
                self->write_queue_.pop_front();
                if (!self->write_queue_.empty()) {
                  (*self->write_queue_.front())();
                }
              });
        }
      };
      
      bool was_empty = self->write_queue_.empty();
      self->write_queue_.push_back(std::make_unique<stream_work>(self, buf));
      
      // If queue was empty, start writing
      if (was_empty) {
        (*self->write_queue_.front())();
      }
    });
  }

  void on_write(boost::system::error_code ec,
                [[maybe_unused]] std::size_t bytes_transferred)
  {
    if (ec) {
      fail(ec, "write");
      return;
    }

    // std::cout << "server_session_socket: on_write: bytes_transferred = "
    //           << bytes_transferred << std::endl;

    assert(write_queue_.size() >= 1);
    write_queue_.pop_front();

    if (write_queue_.size()) {
      (*write_queue_.front())();
    } else {
      do_read_size();
    }
  }

  void on_read_body(const boost::system::error_code& ec, size_t len)
  {
    if (ec) {
      fail(ec, "server_session_socket: on_read_body");
      return;
    }

    rx_buffer_.commit(len);
    size_to_read_ -= static_cast<uint32_t>(len);

    if (size_to_read_ != 0) {
      do_read_body();
      return;
    }

    bool needs_reply = handle_request(rx_buffer_, tx_buffer_);

    if (needs_reply) {
      // Create a work item that writes the tx_buffer
      struct reply_work : work {
        std::shared_ptr<Session_Socket> session;
        reply_work(std::shared_ptr<Session_Socket> s) : session(std::move(s)) {}
        void operator()() override {
          boost::asio::async_write(
              session->socket_, session->tx_buffer_.cdata(),
              std::bind(&Session_Socket::on_write, session,
                        std::placeholders::_1, std::placeholders::_2));
        }
      };
      
      write_queue_.push_front(std::make_unique<reply_work>(shared_from_this()));
      (*write_queue_.front())();
    } else {
      // No reply needed (fire-and-forget like StreamInit) - go back to reading
      do_read_size();
    }
  }

  void do_read_body()
  {
    // async_read reads exactly size_to_read_ bytes in one composed operation,
    // avoiding repeated timer cancel/reschedule and io_context dispatches.
    boost::asio::async_read(
        socket_, rx_buffer_.prepare(size_to_read_),
        std::bind(&Session_Socket::on_read_body,
                  shared_from_this(), std::placeholders::_1,
                  std::placeholders::_2));
  }

  void on_read_size(const boost::system::error_code& ec, size_t len)
  {
    if (ec) {
      fail(ec, "server_session_socket: on_read_size");
      return;
    }

    if (len < 16) {
      fail(boost::asio::error::invalid_argument, "read size header");
      return;
    }
    
    auto body_len = *(uint32_t*)rx_buffer_.data().data();

    if (body_len > max_message_size) {
      NPRPC_LOG_ERROR("Rejected oversized message: {} bytes (max: {} bytes)",
                      body_len, max_message_size);
      return;
    }

    if (body_len == len - 4) {
      // No more data to read, process immediately
      rx_buffer_.commit(len);
      size_to_read_ = 0;
      on_read_body(boost::system::error_code(), 0);
    } else {
      size_to_read_ = body_len;
      rx_buffer_.commit(4);
      on_read_body(boost::system::error_code(), len - 4);
    }
  }

  void do_read_size()
  {
    rx_buffer_.consume(rx_buffer_.size());
    socket_.async_read_some(rx_buffer_.prepare(1024),
                            std::bind(&Session_Socket::on_read_size,
                                      shared_from_this(), std::placeholders::_1,
                                      std::placeholders::_2));
  }

  void run() { do_read_size(); }

  Session_Socket(tcp::socket&& socket)
      : Session(socket.get_executor())
      , socket_(std::move(socket))
  {
    socket_.set_option(net::ip::tcp::no_delay(true));

    // Large socket buffers: same rationale as client — avoids stop-and-go
    // window cycling for large payloads.
    constexpr int kLargeBuf = 4 * 1024 * 1024;
    boost::system::error_code ec;
    socket_.set_option(boost::asio::socket_base::receive_buffer_size(kLargeBuf), ec);
    socket_.set_option(boost::asio::socket_base::send_buffer_size(kLargeBuf), ec);

    auto endpoint = socket_.remote_endpoint();
    ctx_.remote_endpoint =
        EndPoint(EndPointType::TcpTethered,
                 endpoint.address().to_v4().to_string(), endpoint.port());
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

void init_socket(net::io_context& ioc)
{
  NPRPC_LOG_INFO("init_socket called with g_cfg.listen_tcp_port={}", g_cfg.listen_tcp_port);
  if (g_cfg.listen_tcp_port == 0) {
    NPRPC_LOG_INFO("TCP listen port is not set, skipping socket server "
                   "initialization.");
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

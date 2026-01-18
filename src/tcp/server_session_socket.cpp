// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

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
  uint32_t size_to_read_ = 0;
  std::deque<std::unique_ptr<work>> write_queue_;

  flat_buffer rx_buffer_{flat_buffer::default_initial_size()};
  flat_buffer tx_buffer_{flat_buffer::default_initial_size()};

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

    handle_request(rx_buffer_, tx_buffer_);

    write_queue_.push_front({});

    boost::asio::async_write(
        socket_, tx_buffer_.cdata(),
        std::bind(&Session_Socket::on_write, shared_from_this(),
                  std::placeholders::_1, std::placeholders::_2));
  }

  void do_read_body()
  {
    socket_.async_read_some(rx_buffer_.prepare(size_to_read_),
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

    assert(len == 4);

    if (size_to_read_ > max_message_size) {
      NPRPC_LOG_ERROR("Rejected oversized message: {} bytes (max: {} bytes)",
                      size_to_read_, max_message_size);
      return;
    }

    *(uint32_t*)rx_buffer_.data().data() = size_to_read_;
    rx_buffer_.commit(4);

    do_read_body();
  }

  void do_read_size()
  {
    rx_buffer_.consume(rx_buffer_.size());
    rx_buffer_.prepare(4);
    socket_.async_read_some(net::mutable_buffer(&size_to_read_, 4),
                            std::bind(&Session_Socket::on_read_size,
                                      shared_from_this(), std::placeholders::_1,
                                      std::placeholders::_2));
  }

  void run() { do_read_size(); }

  Session_Socket(tcp::socket&& socket)
      : Session(socket.get_executor())
      , socket_(std::move(socket))
  {
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

void init_socket(net::io_context& ioc)
{
  if (g_cfg.listen_tcp_port == 0) {
    NPRPC_LOG_INFO("TCP listen port is not set, skipping socket server "
                   "initialization.");
    return;
  }
  g_tcp_acceptor = std::make_shared<Acceptor>(ioc, g_cfg.listen_tcp_port);
  g_tcp_acceptor->do_accept();
}

void stop_socket_listener()
{
  if (g_tcp_acceptor) {
    g_tcp_acceptor->stop();
    g_tcp_acceptor.reset();
  }
}

} // namespace nprpc::impl

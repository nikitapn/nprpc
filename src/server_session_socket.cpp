// Copyright (c) 2021 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

#include <cstdlib>
#include <cassert>
#include <iostream>
#include <thread>
#include <utility>
#include <memory>
#include <boost/asio.hpp>
#include <functional>
#include <string_view>
#include <deque>
#include <boost/beast/core/bind_handler.hpp>
#include <boost/beast/core/flat_buffer.hpp>

#include <nprpc/nprpc.hpp>
#include <nprpc/nprpc_impl.hpp>
#include <nprpc/session.hpp>

#include <nprpc/asio.hpp>

namespace nprpc::impl {
class Session_Socket
  : public nprpc::impl::Session
  , public std::enable_shared_from_this<Session_Socket> {

  struct work {
    virtual void operator()() = 0;
    virtual ~work() = default;
  };

  tcp::socket socket_;
  uint32_t size_to_read_ = 0;
  std::deque<std::unique_ptr<work>> write_queue_;
public:
  virtual void timeout_action() final {
    assert(false);
  }
  virtual void send_receive(
		boost::beast::flat_buffer& buffer,
		uint32_t timeout_ms
	) {
    assert(false);
	}

	virtual void send_receive_async(
		boost::beast::flat_buffer&& buffer,
		std::function<void(const boost::system::error_code&, boost::beast::flat_buffer&)>&& completion_handler,
		uint32_t timeout_ms
	) {
    assert(false);
	}

  void on_write(boost::system::error_code ec, std::size_t /*bytes_transferred*/) {
    if (ec) return fail(ec, "write");

    assert(write_queue_.size() >= 1);
    write_queue_.pop_front();

    if (write_queue_.size()) {
      (*write_queue_.front())();
    } else {
      do_read_size();
    }
  }

  void on_read_body(const boost::system::error_code& ec, size_t len) {
    if (ec) {
      fail(ec, "read");
      return;
    }

    rx_buffer_().commit(len);
    size_to_read_ -= static_cast<uint32_t>(len);

    if (size_to_read_ != 0) {
      do_read_body();
      return;
    }

    // readed the whole message

    handle_request();

    write_queue_.push_front({});

    socket_.async_send(rx_buffer_().cdata(),
      std::bind(&Session_Socket::on_write, shared_from_this(),
        std::placeholders::_1, std::placeholders::_2)
    );
  }

  void do_read_body() {
    socket_.async_read_some(rx_buffer_().prepare(size_to_read_),
      std::bind(&Session_Socket::on_read_body, shared_from_this(),
        std::placeholders::_1, std::placeholders::_2)
    );
  }

  void on_read_size(const boost::system::error_code& ec, size_t len) {
    if (ec) {
      fail(ec, "read");
      return;
    }

    assert(len == 4);

    if (size_to_read_ > max_message_size) return;

    *(uint32_t*)rx_buffer_().data().data() = size_to_read_;
    rx_buffer_().commit(4);

    do_read_body();
  }

  void do_read_size() {
    rx_buffer_().consume(rx_buffer_().size());
    rx_buffer_().prepare(4);
    socket_.async_read_some(net::mutable_buffer(&size_to_read_, 4),
      std::bind(&Session_Socket::on_read_size, shared_from_this(),
        std::placeholders::_1, std::placeholders::_2)
    );
  }

  void run() {
    do_read_size();
  }

  Session_Socket(tcp::socket&& socket)
    : Session(socket.get_executor())
    , socket_(std::move(socket))
  {
    auto remote_endpoint = socket_.remote_endpoint();
    remote_endpoint_.ip4 = remote_endpoint.address().to_v4().to_uint();
    remote_endpoint_.port = remote_endpoint.port();
  }
};


class Acceptor : public std::enable_shared_from_this<Acceptor> {
  io_context& ioc_;
  tcp::acceptor acceptor_;
public:
  void on_accept(const boost::system::error_code& ec, tcp::socket socket) {
    if (ec) {
      fail(ec, "accept");
    }
    std::make_shared<Session_Socket>(std::move(socket))->run();
    do_accept();
  }

  void do_accept() {
    acceptor_.async_accept(net::make_strand(ioc_),
      boost::beast::bind_front_handler(&Acceptor::on_accept, shared_from_this())
    );
  }

  Acceptor(io_context& ioc, unsigned short port)
    : ioc_(ioc)
    , acceptor_(ioc, tcp::endpoint(tcp::v4(), port))
  {
  }
};

void init_socket(net::io_context& ioc) {
  std::make_shared<Acceptor>(ioc, nprpc::impl::g_cfg.port)->do_accept();
}

} // namespace nprpc::impl
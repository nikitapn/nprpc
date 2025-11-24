// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/common.hpp>

#include <iostream>
#include <variant>
#include <boost/asio/write.hpp>

#include "helper.hpp"

namespace nprpc::impl {



// Explicit instantiation for the CommonConnection class template
// template class CommonConnection<AcceptingPlainWebSocketSession>;

void SocketConnection::send_receive(flat_buffer& buffer, uint32_t timeout_ms) {
  assert(*(uint32_t*)buffer.data().data() == buffer.size() - 4);

  if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryMessageContent) {
    dump_message(buffer, false);
  }

  std::mutex mtx;
  std::condition_variable cv;
  bool done = false;
  boost::system::error_code result;

  auto post_work_and_wait = [&]() -> boost::system::error_code {
    SocketConnectionWork w{buffer, *this, timeout_ms, mtx, cv, done, result};
    add_work(std::move(w));
    
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, [&done]{ return done; });
    return result;
  };

  auto ec = post_work_and_wait();
  
  if (!ec) {
    if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryMessageContent) {
      dump_message(buffer, true);
    }
    return;
  }

  if (ec == boost::asio::error::connection_reset || ec == boost::asio::error::broken_pipe) {
    reconnect();
    auto ec = post_work_and_wait();
    if (ec) close();
  } else {
    fail(ec, "send_receive");
    close();
    throw nprpc::ExceptionCommFailure();
  }
}

void SocketConnection::send_receive_async(
  flat_buffer&& buffer,
  std::optional<std::function<void(const boost::system::error_code&, flat_buffer&)>>&& completion_handler,
  uint32_t timeout_ms
) {
  assert(*(uint32_t*)buffer.data().data() == buffer.size() - 4);

  add_work(SocketConnectionWork{
    std::move(buffer),
    shared_from_this(),
    timeout_ms,
    std::move(completion_handler)
  });
}

void SocketConnection::reconnect() {
  socket_ = std::move(net::ip::tcp::socket(socket_.get_executor()));

  boost::system::error_code ec;
  socket_.connect(endpoint_, ec);

  if (ec) {
    close();
    throw nprpc::ExceptionCommFailure();
  }
}

void SocketConnection::do_read_size() {
  auto& buf = current_rx_buffer();
  buf.consume(buf.size());
  buf.prepare(4);

  timeout_timer_.expires_from_now(timeout_);
  socket_.async_read_some(
    net::mutable_buffer(&rx_size_, 4),
    std::bind(&SocketConnection::on_read_size, 
      shared_from_this(),
      std::placeholders::_1, std::placeholders::_2));
}

void SocketConnection::do_read_body() {
  timeout_timer_.expires_from_now(timeout_);
  socket_.async_read_some(
    current_rx_buffer().prepare(rx_size_),
    std::bind(&SocketConnection::on_read_body,
      shared_from_this(),
      std::placeholders::_1, std::placeholders::_2)
  );
}

void SocketConnection::on_read_size(const boost::system::error_code& ec, size_t len) {
  timeout_timer_.expires_at(boost::posix_time::pos_infin);
  
  if (ec) {
    fail(ec, "client_socket_session: on_read_size");
    wq_.front().on_failed(ec);
    pop_and_execute_next_task();
    return;
  }

  assert(len == 4);

  if (rx_size_ > max_message_size) {
    fail(boost::asio::error::no_buffer_space, "rx_size_ > max_message_size");
    wq_.front().on_failed(ec);
    pop_and_execute_next_task();
    return;
  }

  auto& buf = current_rx_buffer();
  *static_cast<uint32_t*>(buf.data().data()) = rx_size_;
  buf.commit(4);

  do_read_body();
}

void SocketConnection::on_read_body(const boost::system::error_code& ec, size_t len) {
  timeout_timer_.expires_at(boost::posix_time::pos_infin);

  if (ec) {
    fail(ec, "client_socket_session: on_read_body");
    wq_.front().on_failed(ec);
    pop_and_execute_next_task();
    return;
  }

  auto& buf = current_rx_buffer();

  buf.commit(len);
  rx_size_ -= static_cast<uint32_t>(len);

  if (rx_size_ != 0) {
    do_read_body();
  } else {
    wq_.front().on_executed();
    pop_and_execute_next_task();
  }
}



SocketConnection::SocketConnection(
  const EndPoint& endpoint, 
  boost::asio::ip::tcp::socket&& socket)
  : Session(socket.get_executor())
  , socket_{std::move(socket)}
{
  ctx_.remote_endpoint = endpoint;
  timeout_timer_.expires_at(boost::posix_time::pos_infin);
  endpoint_ = sync_socket_connect(endpoint, socket_);
  
  start_timeout_timer();
}

} // namespace nprpc::impl
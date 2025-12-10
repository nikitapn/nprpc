// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include <nprpc/common.hpp>
#include <nprpc/impl/nprpc_impl.hpp>

#include <boost/asio/write.hpp>
#include <future>
#include <iostream>

#include "helper.hpp"

namespace nprpc::impl {

void SocketConnection::send_receive(flat_buffer& buffer, uint32_t timeout_ms)
{
  assert(*(uint32_t*)buffer.data().data() == buffer.size() - 4);

  // dump_message(buffer, false);

  struct WorkImpl : IOWork {
    flat_buffer& buf;
    SocketConnection& this_;
    uint32_t timeout_ms;

    std::mutex mtx;
    std::condition_variable cv;
    bool done = false;
    boost::system::error_code result;

    void operator()() noexcept override
    {
      this_.set_timeout(timeout_ms);
      this_.write_async(buf, [&](const boost::system::error_code& ec,
                                 size_t bytes_transferred) {
        boost::ignore_unused(bytes_transferred);
        if (ec) {
          on_failed(ec);
          this_.pop_and_execute_next_task();
          return;
        }
        this_.do_read_size();
      });
    }

    void on_failed(const boost::system::error_code& ec) noexcept override
    {
      {
        std::lock_guard<std::mutex> lock(mtx);
        result = ec;
        done = true;
      }
      cv.notify_one();
    }

    void on_executed() noexcept override
    {
      {
        std::lock_guard<std::mutex> lock(mtx);
        result = boost::system::error_code{};
        done = true;
      }
      cv.notify_one();
    }

    flat_buffer& buffer() noexcept override { return buf; };

    boost::system::error_code wait()
    {
      std::unique_lock<std::mutex> lock(mtx);
      cv.wait(lock, [this] { return done; });
      return result;
    }

    WorkImpl(flat_buffer& _buf, SocketConnection& _this_, uint32_t _timeout_ms)
        : buf(_buf)
        , this_(_this_)
        , timeout_ms(_timeout_ms)
    {
    }
  };

  // Post work and wait for completion
  auto w = std::make_shared<WorkImpl>(buffer, *this, timeout_ms);
  add_work(w);
  auto ec = w->wait();

  if (!ec) {
    // dump_message(buffer, true);
    return;
  }

  if (ec == boost::asio::error::connection_reset ||
      ec == boost::asio::error::broken_pipe) {
    reconnect();
    auto w = std::make_shared<WorkImpl>(buffer, *this, timeout_ms);
    add_work(w);
    auto ec = w->wait();
    if (ec)
      close();
  } else {
    fail(ec, "send_receive");
    close();
    throw nprpc::ExceptionCommFailure();
  }
}

void SocketConnection::send_receive_async(
    flat_buffer&& buffer,
    std::optional<std::function<void(const boost::system::error_code&,
                                     flat_buffer&)>>&& completion_handler,
    uint32_t timeout_ms)
{
  assert(*(uint32_t*)buffer.data().data() == buffer.size() - 4);

  struct WorkImpl : IOWork {
    flat_buffer buf;
    std::shared_ptr<SocketConnection> this_;
    uint32_t timeout_ms;
    std::optional<
        std::function<void(const boost::system::error_code&, flat_buffer&)>>
        handler;

    void operator()() noexcept override
    {
      this_->set_timeout(timeout_ms);
      this_->write_async(buf, [&](const boost::system::error_code& ec,
                                  size_t bytes_transferred) {
        boost::ignore_unused(bytes_transferred);
        if (ec) {
          on_failed(ec);
          this_->pop_and_execute_next_task();
          return;
        }
        this_->do_read_size();
      });
    }

    void on_failed(const boost::system::error_code& ec) noexcept override
    {
      if (handler)
        handler.value()(ec, buf);
    }

    void on_executed() noexcept override
    {
      if (handler)
        handler.value()(boost::system::error_code{}, buf);
    }

    flat_buffer& buffer() noexcept override { return buf; };

    WorkImpl(flat_buffer&& _buf,
             std::shared_ptr<SocketConnection> _this_,
             std::optional<std::function<void(const boost::system::error_code&,
                                              flat_buffer&)>>&& _handler,
             uint32_t _timeout_ms)
        : buf(std::move(_buf))
        , this_(_this_)
        , timeout_ms(_timeout_ms)
        , handler(std::move(_handler))
    {
    }
  };

  add_work(std::make_shared<WorkImpl>(std::move(buffer), shared_from_this(),
                                      std::move(completion_handler),
                                      timeout_ms));
}

void SocketConnection::reconnect()
{
  socket_ = std::move(net::ip::tcp::socket(socket_.get_executor()));

  boost::system::error_code ec;
  socket_.connect(endpoint_, ec);

  if (ec) {
    close();
    throw nprpc::ExceptionCommFailure();
  }
}

void SocketConnection::do_read_size()
{
  auto& buf = current_rx_buffer();
  buf.consume(buf.size());
  buf.prepare(4);

  timeout_timer_.expires_from_now(timeout_);
  socket_.async_read_some(net::mutable_buffer(&rx_size_, 4),
                          std::bind(&SocketConnection::on_read_size,
                                    shared_from_this(), std::placeholders::_1,
                                    std::placeholders::_2));
}

void SocketConnection::do_read_body()
{
  timeout_timer_.expires_from_now(timeout_);
  socket_.async_read_some(current_rx_buffer().prepare(rx_size_),
                          std::bind(&SocketConnection::on_read_body,
                                    shared_from_this(), std::placeholders::_1,
                                    std::placeholders::_2));
}

void SocketConnection::on_read_size(const boost::system::error_code& ec,
                                    size_t len)
{
  timeout_timer_.expires_at(boost::posix_time::pos_infin);

  if (ec) {
    fail(ec, "client_socket_session: on_read_size");
    (*wq_.front()).on_failed(ec);
    pop_and_execute_next_task();
    return;
  }

  assert(len == 4);

  if (rx_size_ > max_message_size) {
    fail(boost::asio::error::no_buffer_space, "rx_size_ > max_message_size");
    (*wq_.front()).on_failed(ec);
    pop_and_execute_next_task();
    return;
  }

  auto& buf = current_rx_buffer();
  *static_cast<uint32_t*>(buf.data().data()) = rx_size_;
  buf.commit(4);

  do_read_body();
}

void SocketConnection::on_read_body(const boost::system::error_code& ec,
                                    size_t len)
{
  timeout_timer_.expires_at(boost::posix_time::pos_infin);

  if (ec) {
    fail(ec, "client_socket_session: on_read_body");
    (*wq_.front()).on_failed(ec);
    pop_and_execute_next_task();
    return;
  }

  auto& buf = current_rx_buffer();

  buf.commit(len);
  rx_size_ -= static_cast<uint32_t>(len);

  if (rx_size_ != 0) {
    do_read_body();
  } else {
    (*wq_.front()).on_executed();
    pop_and_execute_next_task();
  }
}

SocketConnection::SocketConnection(const EndPoint& endpoint,
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
// Copyright (c) 2021-2025 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by
// LICENSING file in the topmost directory

#pragma once

#include <nprpc/common.hpp>
#include <boost/date_time/posix_time/ptime.hpp>
#include <nprpc/basic.hpp>
#include <nprpc/buffer.hpp>
#include <nprpc/session_context.h>
#include <boost/asio/deadline_timer.hpp>

namespace nprpc::impl {

class Session {
public:
  bool closed_ = false;
protected:
  Buffers rx_buffer_;
  EndPoint remote_endpoint_;
  SessionContext ctx_;

  boost::asio::deadline_timer timeout_timer_;
  boost::asio::deadline_timer inactive_timer_;
  boost::posix_time::time_duration timeout_ = boost::posix_time::milliseconds(1000);

  void close();
  bool is_closed() { return closed_; }

  virtual void timeout_action() = 0;
public:
  virtual void send_receive(
    flat_buffer& buffer,
    uint32_t timeout_ms) = 0;

  virtual void send_receive_async(
    flat_buffer&& buffer,
    std::optional<std::function<void(const boost::system::error_code&, flat_buffer&)>>&& completion_handler,
    uint32_t timeout_ms) = 0;

  void set_timeout(uint32_t timeout_ms) {
    timeout_ = boost::posix_time::milliseconds(timeout_ms);
    timeout_timer_.expires_from_now(timeout_);
  }

  // monitors every asynchronous operation
  // and calls timeout_action() if the operation is not completed
  // within the specified timeout
  void start_timeout_timer() noexcept 
  {
    if (is_closed())
      return;

    const auto now = boost::asio::deadline_timer::traits_type::now();
    if (timeout_timer_.expires_at() <= now) {

      timeout_action();

      boost::system::error_code ec;
      timeout_timer_.expires_at(boost::posix_time::pos_infin, ec);
      if (ec) fail(ec, "timeout_timer::expires_at()");
    }
    timeout_timer_.async_wait(std::bind(&Session::start_timeout_timer, this));
  }
  const EndPoint& remote_endpoint() const noexcept { return remote_endpoint_; }
  SessionContext& ctx() noexcept { return ctx_; }
  void handle_request();

  Session(boost::asio::any_io_executor executor)
    : timeout_timer_{executor}
    , inactive_timer_{executor}
  {
  }

  virtual ~Session() = default;
};


} // namespace nprpc

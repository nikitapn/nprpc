// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <boost/asio/deadline_timer.hpp>
#include <boost/date_time/posix_time/ptime.hpp>

#include <nprpc/basic.hpp>
#include <nprpc/common.hpp>
#include <nprpc/flat_buffer.hpp>
#include <nprpc/session_context.h>

namespace nprpc::impl {

class Session
{
protected:
  SessionContext ctx_;

  boost::asio::deadline_timer timeout_timer_;
  boost::asio::deadline_timer inactive_timer_;
  boost::posix_time::time_duration timeout_ =
      boost::posix_time::milliseconds(1000);

  std::atomic<bool> closed_{false};

  void close();
  bool is_closed() { return closed_.load(); }

  virtual void timeout_action() = 0;

public:
  virtual void send_receive(flat_buffer& buffer, uint32_t timeout_ms) = 0;

  virtual void send_receive_async(
      flat_buffer&& buffer,
      std::optional<std::function<void(const boost::system::error_code&,
                                       flat_buffer&)>>&& completion_handler,
      uint32_t timeout_ms) = 0;

  /**
   * @brief Send unreliable datagram (fire-and-forget, no response expected)
   *
   * Default implementation falls back to send_receive_async with no handler.
   * QUIC sessions override this to use DATAGRAM extension.
   *
   * @param buffer Message buffer to send (moved)
   * @return true if datagram was sent successfully
   */
  virtual bool send_datagram(flat_buffer&& buffer)
  {
    // Default: fall back to async send (fire-and-forget)
    send_receive_async(std::move(buffer), std::nullopt, 0);
    return true;
  }

  void set_timeout(uint32_t timeout_ms)
  {
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
      if (ec)
        fail(ec, "timeout_timer::expires_at()");
    }
    timeout_timer_.async_wait(std::bind(&Session::start_timeout_timer, this));
  }
  const EndPoint& remote_endpoint() const noexcept
  {
    return ctx_.remote_endpoint;
  }
  SessionContext& ctx() noexcept { return ctx_; }
  // Handles incoming request in rx_buffer and prepares response in tx_buffer
  // in some cases rx_buffer and tx_buffer can be the same buffer
  void handle_request(nprpc::flat_buffer& rx_buffer,
                      nprpc::flat_buffer& tx_buffer);

  virtual void shutdown()
  {
    closed_.store(true);
    boost::system::error_code ec;
    timeout_timer_.cancel(ec);
    inactive_timer_.cancel(ec);
  }

  Session(boost::asio::any_io_executor executor)
      : timeout_timer_{executor}, inactive_timer_{executor}
  {
  }

  virtual ~Session() = default;
};

} // namespace nprpc::impl

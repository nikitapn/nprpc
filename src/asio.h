#pragma once

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>

namespace net = boost::asio;
using net::ip::tcp;
using net::io_context;

static constexpr uint32_t max_message_size = 1024 * 1024 * 1024;

inline void fail(const boost::system::error_code& ec, const char* what) {
  std::cerr << what << ": " << ec.message() << '\n';
}
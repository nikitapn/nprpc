// Copyright (c) 2021 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

#pragma once

#include <iostream>
#include <boost/asio/io_context.hpp>
#include <boost/asio/basic_stream_socket.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/asio/ssl/context.hpp>

namespace nprpc::impl {

namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
namespace beast = boost::beast;

using tcp = net::ip::tcp;
using error_code = boost::system::error_code;

namespace http = boost::beast::http;
namespace websocket = boost::beast::websocket;

using tcp_stream_strand = net::basic_stream_socket<
  net::ip::tcp, net::strand<net::io_context::executor_type>>;

using beast_tcp_stream_strand = beast::basic_stream<
  net::ip::tcp, net::strand<net::io_context::executor_type>>;

using plain_stream = beast_tcp_stream_strand;

using ssl_stream = beast::ssl_stream<
  beast_tcp_stream_strand>;

using plain_ws = websocket::stream<
  beast_tcp_stream_strand>;

using ssl_ws = websocket::stream<
  beast::ssl_stream<beast_tcp_stream_strand>>;

// Report a failure
void fail(beast::error_code ec, char const *what);

static constexpr uint32_t max_message_size = 1024 * 1024 * 1024;

} // namespace nprpc::impl
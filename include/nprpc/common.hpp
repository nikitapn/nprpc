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

// Maximum allowed message size to prevent memory exhaustion attacks
// This limit is enforced at the transport level before allocating memory
// Adjust this value based on your application's needs
static constexpr uint32_t max_message_size = 32 * 1024 * 1024; // 32 MB

// Maximum number of pending (in-flight) requests per WebSocket session
// Prevents memory exhaustion from async request flooding
static constexpr size_t max_pending_requests = 1000;

// Maximum number of queued outgoing messages per session
// Prevents memory exhaustion from slow clients
static constexpr size_t max_write_queue_size = 100;

// Maximum number of object references per session
// Prevents reference count exhaustion attacks
static constexpr size_t max_references_per_session = 10000;

} // namespace nprpc::impl
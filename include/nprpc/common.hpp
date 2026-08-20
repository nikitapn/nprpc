// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <nprpc/flat_buffer.hpp>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address_v4.hpp>
#include <boost/system/error_code.hpp>

#if defined(NPRPC_TCP_ENABLED) || defined(NPRPC_HTTP_ENABLED) || \
    defined(NPRPC_WEBSOCKET_ENABLED)
#include <boost/asio/basic_stream_socket.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#endif

#if defined(NPRPC_HTTP_ENABLED) || defined(NPRPC_WEBSOCKET_ENABLED)
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#endif

namespace nprpc::impl {

namespace net = boost::asio;
using error_code = boost::system::error_code;

#if defined(NPRPC_HTTP_ENABLED) || defined(NPRPC_WEBSOCKET_ENABLED)
namespace beast = boost::beast;
#endif

#if defined(NPRPC_TCP_ENABLED) || defined(NPRPC_HTTP_ENABLED) || \
    defined(NPRPC_WEBSOCKET_ENABLED)
using tcp = net::ip::tcp;

using tcp_stream_strand =
    net::basic_stream_socket<net::ip::tcp,
                             net::strand<net::io_context::executor_type>>;
#endif

#if defined(NPRPC_HTTP_ENABLED) || defined(NPRPC_WEBSOCKET_ENABLED)
using beast_tcp_stream_strand =
    beast::basic_stream<net::ip::tcp,
                        net::strand<net::io_context::executor_type>>;

using plain_stream = beast_tcp_stream_strand;

using plain_ws = beast::websocket::stream<beast_tcp_stream_strand>;
#endif

// Report a failure. Defined even when HTTP/WebSocket/SSL are compiled out
// because shared-memory and TCP paths call it too.
void fail(boost::system::error_code ec, char const* what);

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
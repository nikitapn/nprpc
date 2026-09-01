// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#ifdef NPRPC_SSL_ENABLED

#include <nprpc/common.hpp>

#include <boost/asio/ssl/context.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket/ssl.hpp>

#include <memory>

namespace nprpc::impl {

using ssl_stream = beast::ssl_stream<beast_tcp_stream_strand>;
using ssl_ws = beast::websocket::stream<beast::ssl_stream<beast_tcp_stream_strand>>;

// Server-side TLS context for HTTP/1.1 and WebSocket.
//
// Reachable only through a shared_ptr, so that certificate reload can publish
// a freshly built context without racing the accept path.  The listener reads
// the pointer once per accepted connection; a handshake already in flight
// keeps running against the context it started with (SSL_new takes its own
// reference on the SSL_CTX, so the old context outlives the shared_ptr that
// created it).  Returns nullptr when HTTP TLS is not configured.
NPRPC_API std::shared_ptr<net::ssl::context> server_ssl_context();

// Builds a server context from g_cfg's http_cert_file / http_key_file /
// http_dhparams_file.  Throws on a missing or malformed file.
NPRPC_API std::shared_ptr<net::ssl::context> build_server_ssl_context();

// Replaces the context returned by server_ssl_context() with a newly built
// one.  Throws (leaving the current context in place) if the new files cannot
// be read or do not parse.
NPRPC_API void reload_server_ssl_context();

} // namespace nprpc::impl

#endif // NPRPC_SSL_ENABLED
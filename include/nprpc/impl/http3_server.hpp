// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#ifdef NPRPC_HTTP3_ENABLED

#include <boost/asio/io_context.hpp>
#include <nprpc/export.hpp>

namespace nprpc::impl {

/**
 * @brief Initialize the HTTP/3 server
 *
 * Uses nghttp3 library on top of MsQuic for HTTP/3 support.
 * Shares certificates with boost beast http/1.1 transport configuration.
 *
 * @param ioc The io_context for async operations
 */
NPRPC_API void init_http3_server(boost::asio::io_context& ioc);

/**
 * @brief Re-read the configured certificate/key files on every HTTP/3 worker
 *
 * Each worker reloads on its own io_context thread, so this never races
 * connection setup.  Existing connections keep the certificate they
 * handshook with; new ones get the reloaded pair.
 *
 * @return true if every worker reloaded successfully (also true when the
 *         HTTP/3 server is not running)
 */
NPRPC_API bool reload_http3_certificates();

/**
 * @brief Stop the HTTP/3 server and cleanup resources
 */
NPRPC_API void stop_http3_server();

} // namespace nprpc::impl

#endif // NPRPC_HTTP3_ENABLED

// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#ifdef NPRPC_HTTP3_ENABLED

#include <boost/asio/io_context.hpp>
#include <memory>
#include <nprpc/export.hpp>
#include <string>

namespace nprpc::impl {

/**
 * @brief Initialize the HTTP/3 server
 *
 * Uses msh3 library on top of MsQuic for HTTP/3 support.
 * Shares certificates with QUIC transport configuration.
 *
 * @param ioc The io_context for async operations
 */
NPRPC_API void init_http3_server(boost::asio::io_context& ioc);

/**
 * @brief Stop the HTTP/3 server and cleanup resources
 */
NPRPC_API void stop_http3_server();

} // namespace nprpc::impl

#endif // NPRPC_HTTP3_ENABLED

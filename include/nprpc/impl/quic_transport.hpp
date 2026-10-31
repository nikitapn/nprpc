// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#ifdef NPRPC_QUIC_ENABLED

#include <boost/asio/io_context.hpp>
#include <memory>
#include <nprpc/endpoint.hpp>
#include <nprpc/export.hpp>

namespace nprpc { struct QuicClientTlsConfig; }

namespace nprpc::impl {

class Session;

void set_quic_client_tls(const QuicClientTlsConfig& config);
void validate_quic_server_tls();

// Initialise the ngtcp2-based QUIC listener on the configured port.
// No-op if listen_quic_port == 0.
NPRPC_API void init_quic(boost::asio::io_context& ioc);

// Stop the QUIC listener and free all connections.
NPRPC_API void stop_quic_listener();
// Ends the client transport thread, after the sessions have been shut down.
NPRPC_API void stop_quic_client();

// Reload certificates for new connections, retaining the current context on failure.
NPRPC_API bool reload_quic_certificates();

// Create a client-side QUIC session for the given endpoint.
NPRPC_API std::shared_ptr<Session>
make_quic_client_session(const EndPoint& endpoint,
                         boost::asio::io_context& ioc);

} // namespace nprpc::impl

#endif // NPRPC_QUIC_ENABLED

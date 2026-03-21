// Copyright (c) 2021-2025 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by
// LICENSING file in the topmost directory

#include <nprpc/impl/async_connect.hpp>
#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/impl/websocket_session.hpp>

namespace nprpc::impl {

std::shared_ptr<ClientPlainWebSocketSession>
make_client_plain_websocket_session(const EndPoint& endpoint,
                                    net::io_context& ioc)
{
  assert(endpoint.type() == EndPointType::WebSocket);
  auto promise =
      async_connect_ws(endpoint, ioc, std::chrono::milliseconds(5000));
  auto future = promise->get_future();
  auto ws = future.get();
  auto session = std::make_shared<ClientPlainWebSocketSession>(
      std::move(*ws.release()), endpoint);
  session->ws().read_message_max(g_cfg.http_websocket_max_message_size);
  // Mirror the server-side performance settings.
  session->ws().auto_fragment(false);
  session->ws().write_buffer_bytes(4 * 1024 * 1024);
  {
    auto& sock = beast::get_lowest_layer(session->ws()).socket();
    sock.set_option(net::ip::tcp::no_delay(true));
    sock.set_option(net::socket_base::send_buffer_size(4 * 1024 * 1024));
    sock.set_option(net::socket_base::receive_buffer_size(4 * 1024 * 1024));
  }
  session->start_read_loop();
  return session;
}

std::shared_ptr<ClientSSLWebSocketSession>
make_client_ssl_websocket_session(const EndPoint& endpoint,
                                  net::io_context& ioc)
{
  assert(endpoint.type() == EndPointType::SecuredWebSocket);
  auto promise =
      async_connect_wss(endpoint, ioc, std::chrono::milliseconds(5000));
  auto future = promise->get_future();
  auto ws = future.get();
  auto session = std::make_shared<ClientSSLWebSocketSession>(
      std::move(*ws.release()), endpoint);
  session->ws().read_message_max(g_cfg.http_websocket_max_message_size);
  // Mirror the server-side performance settings (SSL layer wraps TCP).
  session->ws().auto_fragment(false);
  session->ws().write_buffer_bytes(4 * 1024 * 1024);
  session->start_read_loop();
  return session;
}

} // namespace nprpc::impl

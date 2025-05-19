// Copyright (c) 2021-2025 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by
// LICENSING file in the topmost directory

#include <nprpc/impl/websocket_session.hpp>
#include <nprpc/impl/async_connect.hpp>

namespace nprpc::impl {

std::shared_ptr<ClientPlainWebSocketSession>
  make_client_plain_websocket_session(
    const EndPoint& endpoint, net::io_context& ioc)
{
  auto promise = async_connect_ws(
    endpoint, ioc, std::chrono::milliseconds(5000));
  auto future = promise->get_future();
  auto ws = future.get();
  return std::make_shared<ClientPlainWebSocketSession>(std::move(ws));
}

std::shared_ptr<ClientSSLWebSocketSession>
  make_client_ssl_websocket_session(
    const EndPoint& endpoint, net::io_context& ioc)
{
  auto promise = async_connect_wss(
    endpoint, ioc, std::chrono::milliseconds(5000));
  auto future = promise->get_future();
  auto ws = future.get();
  return std::make_shared<ClientSSLWebSocketSession>(std::move(ws));
}

} // namespace nprpc::impl

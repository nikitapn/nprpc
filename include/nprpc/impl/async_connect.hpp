// Copyright (c) 2021-2025 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by
// LICENSING file in the topmost directory

#pragma once

#include <nprpc/common.hpp>
#include <nprpc/endpoint.hpp>

#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/ssl/stream.hpp>

#include <future>
#include <memory>

namespace nprpc::impl {

struct type_tcp {
  using stream_t = beast_tcp_stream_strand;
  static constexpr bool is_websocket = false;
};

struct type_ws {
  using stream_t = plain_ws;
  static constexpr bool is_websocket = true;
};

struct type_wss {
  using stream_t = ssl_ws;
  static constexpr bool is_websocket = true;
};


// Helper class for resolver/socket async operations 
template<typename Type>
class AsyncConnector 
  : public std::enable_shared_from_this<AsyncConnector<Type>>
{
  using Self = AsyncConnector<Type>;
  using executor_type = net::strand<net::io_context::executor_type>;
  using StreamType = typename Type::stream_t;

  net::io_context& ioc_;
  tcp::resolver resolver_;
  // net::steady_timer timer_;
  std::string host_;
  std::uint16_t port_;
  std::chrono::steady_clock::time_point deadline_;
  std::shared_ptr<std::promise<std::unique_ptr<StreamType>>> stream_promise_;

  void on_resolve(
    const beast::error_code& ec,
    tcp::resolver::results_type results);

  void on_connect(
    const beast::error_code& ec,
    tcp::resolver::results_type::endpoint_type endpoint,
    std::unique_ptr<beast_tcp_stream_strand> stream);
    
  void on_ws_handshake(
    const beast::error_code& ec,
    std::unique_ptr<plain_ws>&& ws);

  void on_ssl_handshake(
    const beast::error_code& ec,
    std::unique_ptr<beast::ssl_stream<beast_tcp_stream_strand>>&& ssl_stream);
    
  void on_ssl_ws_handshake(
    const beast::error_code& ec,
    std::unique_ptr<ssl_ws>&& ws);
  
  void do_timeout();
  void cleanup();

public:
  AsyncConnector(
    net::io_context& ioc,
    std::string host,
    std::uint16_t port,
    std::chrono::milliseconds timeout)
    : ioc_(ioc)
    , resolver_(ioc)
    // , timer_(ioc)
    , host_(std::move(host))
    , port_(port)
    , deadline_(std::chrono::steady_clock::now() + timeout)
  {
    // timer_.expires_at(deadline_);
  }

  // Start the asynchronous operation
  std::shared_ptr<std::promise<std::unique_ptr<StreamType>>> run();
};

// Factory functions for different connection types
inline std::shared_ptr<std::promise<std::unique_ptr<typename type_tcp::stream_t>>> 
async_connect_tcp(
  net::io_context& ioc,
  const EndPoint& endpoint,
  std::chrono::milliseconds timeout)
{
  return std::make_shared<AsyncConnector<type_tcp>>(
    ioc,
    std::string(endpoint.hostname()),
    endpoint.port(),
    timeout)->run();
}

inline std::shared_ptr<std::promise<std::unique_ptr<plain_ws>>>
async_connect_ws(
  const EndPoint& endpoint,
  net::io_context& ioc,
  std::chrono::milliseconds timeout)
{
  return std::make_shared<AsyncConnector<type_ws>>(
    ioc,
    std::string(endpoint.hostname()),
    endpoint.port(),
    timeout
  )->run();
}

inline std::shared_ptr<std::promise<std::unique_ptr<ssl_ws>>>
async_connect_wss(
  const EndPoint& endpoint,
  net::io_context& ioc,
  std::chrono::milliseconds timeout)
{
  return std::make_shared<AsyncConnector<type_wss>>(
      ioc,
      std::string(endpoint.hostname()),
      endpoint.port(),
      timeout
  )->run();
}

} // namespace nprpc::impl

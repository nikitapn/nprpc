// Copyright (c) 2021-2025 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by
// LICENSING file in the topmost directory

#pragma once

#include <nprpc/common.hpp>
#include <nprpc/impl/session.hpp>

#include <deque>

namespace nprpc::impl {

template <class Derived>
class WebSocketSession
  : public Session
{
  struct work {
    virtual void operator()() = 0;
    virtual void on_failed(const boost::system::error_code &ec) noexcept = 0;
    virtual void on_executed(flat_buffer &&rx_buffer) noexcept = 0;
    virtual ~work() = default;
  };

  std::deque<std::shared_ptr<work>> requests_;
  std::deque<std::unique_ptr<work>> answers_;

  bool reading_ = false;
  bool writing_ = false;
  bool request_sended_waiting_for_answer_ = false;

  Derived &derived() { return static_cast<Derived &>(*this); }

  void on_read(beast::error_code ec, std::size_t bytes_transferred);
  void on_write(
    beast::error_code ec,
    std::size_t bytes_transferred,
    bool is_answer);
  void add_request(std::shared_ptr<work> w);

  void reconnect() {
    // This method should be implemented in the derived class
    // to handle reconnection logic if needed.
  }
protected:

  void do_read();
  void close();
  virtual void timeout_action() override final;
public:

  virtual void send_receive(
      flat_buffer &buffer,
      uint32_t timeout_ms);

  virtual void send_receive_async(
      flat_buffer &&buffer,
      std::optional<std::function<
        void(
          const boost::system::error_code &,
          flat_buffer &)>> &&completion_handler,
      uint32_t timeout_ms);

  WebSocketSession(net::any_io_executor executor)
    : Session(executor) {}
};

template <class Derived>
class PlainWebSocketSessionT
  : public WebSocketSession<Derived>
{
  using base = WebSocketSession<Derived>;
public:
  using websocket_t = plain_ws;
  using stream_t = plain_stream;
  // Create the session
  explicit PlainWebSocketSessionT(plain_ws&& _ws_)
    : base(_ws_.get_executor())
    , ws_(std::move(_ws_)) {}

  // Called by the base class
  websocket_t &ws() { return ws_; }
private:
  websocket_t ws_;
};

template <typename Derived>
class SSLWebSocketSessionT
  : public WebSocketSession<Derived>
{
  using base = WebSocketSession<Derived>;
public:
  using websocket_t = ssl_ws;
  using stream_t = ssl_stream;
  // Create the session
  explicit SSLWebSocketSessionT(ssl_ws&& _ws_)
    : base(_ws_.get_executor())
    , ws_(std::move(_ws_)) {}

  // Called by the base class
  websocket_t &ws() { return ws_; }
private:
  websocket_t ws_;
};

class AcceptingPlainWebSocketSession
  : public PlainWebSocketSessionT<AcceptingPlainWebSocketSession>
  , public std::enable_shared_from_this<AcceptingPlainWebSocketSession>
{
public:
  explicit AcceptingPlainWebSocketSession(plain_ws&& ws)
    : PlainWebSocketSessionT<AcceptingPlainWebSocketSession>(std::move(ws))
  {
    auto endpoint = this->ws().next_layer().socket().remote_endpoint();
    this->ctx_.remote_endpoint = EndPoint(
      EndPointType::TcpTethered,
      endpoint.address().to_v4().to_string(), 
      endpoint.port());
  }
};

class AcceptingSSLWebSocketSession
  : public SSLWebSocketSessionT<AcceptingSSLWebSocketSession>
  , public std::enable_shared_from_this<AcceptingSSLWebSocketSession>
{
public:
  explicit AcceptingSSLWebSocketSession(ssl_ws&& ws)
    : SSLWebSocketSessionT<AcceptingSSLWebSocketSession>(std::move(ws))
  {
    auto endpoint = this->ws().next_layer().next_layer().socket().remote_endpoint();
    this->ctx_.remote_endpoint = EndPoint(
      EndPointType::TcpTethered,
      endpoint.address().to_v4().to_string(), 
      endpoint.port());
  }
};

template <class Body, class Allocator>
void make_accepting_websocket_session(
  plain_stream ws,
  http::request<Body, http::basic_fields<Allocator>> req);

template <class Body, class Allocator>
void make_accepting_websocket_session(
  ssl_stream stream,
  http::request<Body, http::basic_fields<Allocator>> req);

class ClientPlainWebSocketSession
  : public PlainWebSocketSessionT<ClientPlainWebSocketSession>
  , public std::enable_shared_from_this<ClientPlainWebSocketSession>
{
  // this resolved endpoint is used to reconnect
  // the session if the connection is lost
  // in client implementation
  boost::asio::ip::tcp::endpoint endpoint_;
public:
  explicit ClientPlainWebSocketSession(plain_ws&& ws, const EndPoint& ep)
    : PlainWebSocketSessionT<ClientPlainWebSocketSession>(std::move(ws))
  {
    this->ctx_.remote_endpoint = ep;
  }

  // TODO: Implement reconnect logic
  void reconnect() {}
};

class ClientSSLWebSocketSession
  : public SSLWebSocketSessionT<ClientSSLWebSocketSession>
  , public std::enable_shared_from_this<ClientSSLWebSocketSession>
{
  // this resolved endpoint is used to reconnect
  // the session if the connection is lost
  // in client implementation
  boost::asio::ip::tcp::endpoint endpoint_;
public:
  explicit ClientSSLWebSocketSession(ssl_ws&& ws, const EndPoint& ep)
    : SSLWebSocketSessionT<ClientSSLWebSocketSession>(std::move(ws)) 
  {
    this->ctx_.remote_endpoint = ep;
  }

  // TODO: Implement reconnect logic
  void reconnect() {}
};

// Factory functions to create client WebSocket sessions
// These functions are used to create client sessions
// via asynchronous connect operations
std::shared_ptr<ClientPlainWebSocketSession>
  make_client_plain_websocket_session(
    const EndPoint& endpoint, net::io_context& ioc);

std::shared_ptr<ClientSSLWebSocketSession>
  make_client_ssl_websocket_session(
    const EndPoint& endpoint, net::io_context& ioc);

} // namespace nprpc::impl

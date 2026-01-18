// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <nprpc/common.hpp>
#include <nprpc/impl/session.hpp>

#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <unordered_map>

namespace nprpc::impl {

template <class Derived> class WebSocketSession : public Session
{
  struct outgoing_message {
    flat_buffer buffer;
    std::function<void(const boost::system::error_code&)> completion_handler;

    outgoing_message(
        flat_buffer&& buf,
        std::function<void(const boost::system::error_code&)>&& handler)
        : buffer(std::move(buf))
        , completion_handler(std::move(handler))
    {
    }
  };

  struct pending_request {
    std::function<void(const boost::system::error_code&, flat_buffer&)>
        completion_handler;
    std::chrono::steady_clock::time_point timeout_point;

    static constexpr std::function<void(const boost::system::error_code&,
                                        flat_buffer&)>
    empty_handler()
    {
      return [](const boost::system::error_code&, flat_buffer&) {};
    }

    pending_request(std::function<void(const boost::system::error_code&,
                                       flat_buffer&)>&& handler,
                    std::chrono::milliseconds timeout)
        : completion_handler(std::move(handler))
        , timeout_point(std::chrono::steady_clock::now() + timeout)
    {
    }
  };

  std::deque<outgoing_message> write_queue_;
  std::unordered_map<uint32_t, pending_request> pending_requests_;

  std::atomic<uint32_t> next_request_id_{1};
  std::atomic<bool> reading_{false};
  std::atomic<bool> writing_{false};

  flat_buffer rx_buffer_{flat_buffer::default_initial_size()};
  flat_buffer tx_buffer_{flat_buffer::default_initial_size()};

  Derived& derived() { return static_cast<Derived&>(*this); }

  void on_read(beast::error_code ec, std::size_t bytes_transferred);
  void on_write(beast::error_code ec, std::size_t bytes_transferred);
  void do_write();

  uint32_t generate_request_id() { return next_request_id_.fetch_add(1); }

  // Helper methods for request ID correlation
  void inject_request_id(flat_buffer& buffer, uint32_t request_id);
  uint32_t extract_request_id(const flat_buffer& buffer);

protected:
  void do_read();
  void close();
  virtual void timeout_action() override final;

public:
  void start_read_loop();
  void start_write_loop();

  virtual void send_receive(flat_buffer& buffer, uint32_t timeout_ms) override;

  virtual void send_receive_async(
      flat_buffer&& buffer,
      std::optional<std::function<void(const boost::system::error_code&,
                                       flat_buffer&)>>&& completion_handler,
      uint32_t timeout_ms) override;

  // Override for streaming - just queue the message for writing, no response expected
  virtual void send_stream_message(flat_buffer&& buffer) override;

  WebSocketSession(net::any_io_executor executor)
      : Session(executor)
  {
  }
};

template <class Derived>
class PlainWebSocketSessionT : public WebSocketSession<Derived>
{
  using base = WebSocketSession<Derived>;

public:
  using websocket_t = plain_ws;
  using stream_t = plain_stream;
  // Create the session
  explicit PlainWebSocketSessionT(plain_ws&& _ws_)
      : base(_ws_.get_executor())
      , ws_(std::move(_ws_))
  {
  }

  // Called by the base class
  websocket_t& ws() { return ws_; }

private:
  websocket_t ws_;
};

template <typename Derived>
class SSLWebSocketSessionT : public WebSocketSession<Derived>
{
  using base = WebSocketSession<Derived>;

public:
  using websocket_t = ssl_ws;
  using stream_t = ssl_stream;
  // Create the session
  explicit SSLWebSocketSessionT(ssl_ws&& _ws_)
      : base(_ws_.get_executor())
      , ws_(std::move(_ws_))
  {
  }

  // Called by the base class
  websocket_t& ws() { return ws_; }

private:
  websocket_t ws_;
};

class AcceptingPlainWebSocketSession
    : public PlainWebSocketSessionT<AcceptingPlainWebSocketSession>,
      public std::enable_shared_from_this<AcceptingPlainWebSocketSession>
{
public:
  explicit AcceptingPlainWebSocketSession(plain_ws&& ws)
      : PlainWebSocketSessionT<AcceptingPlainWebSocketSession>(std::move(ws))
  {
    auto endpoint = this->ws().next_layer().socket().remote_endpoint();
    this->ctx_.remote_endpoint =
        EndPoint(EndPointType::TcpTethered,
                 endpoint.address().to_v4().to_string(), endpoint.port());
  }
};

class AcceptingSSLWebSocketSession
    : public SSLWebSocketSessionT<AcceptingSSLWebSocketSession>,
      public std::enable_shared_from_this<AcceptingSSLWebSocketSession>
{
public:
  explicit AcceptingSSLWebSocketSession(ssl_ws&& ws)
      : SSLWebSocketSessionT<AcceptingSSLWebSocketSession>(std::move(ws))
  {
    auto endpoint =
        this->ws().next_layer().next_layer().socket().remote_endpoint();
    this->ctx_.remote_endpoint =
        EndPoint(EndPointType::TcpTethered,
                 endpoint.address().to_v4().to_string(), endpoint.port());
  }
};

template <class Body, class Allocator>
void make_accepting_websocket_session(
    plain_stream ws, http::request<Body, http::basic_fields<Allocator>> req);

template <class Body, class Allocator>
void make_accepting_websocket_session(
    ssl_stream stream, http::request<Body, http::basic_fields<Allocator>> req);

class ClientPlainWebSocketSession
    : public PlainWebSocketSessionT<ClientPlainWebSocketSession>,
      public std::enable_shared_from_this<ClientPlainWebSocketSession>
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
    : public SSLWebSocketSessionT<ClientSSLWebSocketSession>,
      public std::enable_shared_from_this<ClientSSLWebSocketSession>
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
make_client_plain_websocket_session(const EndPoint& endpoint,
                                    net::io_context& ioc);

std::shared_ptr<ClientSSLWebSocketSession>
make_client_ssl_websocket_session(const EndPoint& endpoint,
                                  net::io_context& ioc);

} // namespace nprpc::impl

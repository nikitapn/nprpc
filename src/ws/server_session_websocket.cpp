// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

// #define BOOST_ASIO_NO_DEPRECATED

#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/impl/session.hpp>
#include <nprpc/impl/websocket_session.hpp>
#include <nprpc_base.hpp>

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/version.hpp>

#include <algorithm>
#include <cassert>
#include <memory>
#include <string>

namespace nprpc::impl {

template <class T>
concept AnyWebSocketSession =
    std::is_base_of_v<AcceptingPlainWebSocketSession, T> ||
    std::is_base_of_v<AcceptingSSLWebSocketSession, T>;

template <AnyWebSocketSession Derived>
class websocket_session_with_acceptor : public Derived
{
  // Owns the cookie string from the HTTP Upgrade request so that
  // ctx_.cookies (a string_view) remains valid for the session lifetime.
  std::string upgrade_cookies_;

  // Start accepting handshake for the WebSocket session.
  template <class Body, class Allocator>
  void do_accept(http::request<Body, http::basic_fields<Allocator>> req)
  {
    // Set suggested timeout settings for the websocket
    this->ws().set_option(
        websocket::stream_base::timeout::suggested(beast::role_type::server));

    // Set maximum message size to prevent memory exhaustion attacks
    this->ws().read_message_max(max_message_size);

    // Set a decorator to change the Server of the handshake
    this->ws().set_option(
        websocket::stream_base::decorator([](websocket::response_type& res) {
          res.set(http::field::server, std::string(BOOST_BEAST_VERSION_STRING));
        }));

    // permessage_deflate is disabled: NPRPC sends binary flatbuffer data that
    // does not benefit from compression, and zlib overhead (~200 MB/s) makes
    // large-message latency 5-10x worse with no size win.
    // websocket::permessage_deflate opt;
    // opt.client_enable = true;
    // opt.server_enable = true;
    // this->ws().set_option(opt);

    // Instead of Beast's per-read/write timeouts (expensive under io_uring),
    // rely on session-level timers
    websocket::stream_base::timeout ws_timeout;
    ws_timeout.handshake_timeout = std::chrono::seconds(30);
    ws_timeout.idle_timeout = std::chrono::seconds(300);
    ws_timeout.keep_alive_pings = true;
    this->ws().set_option(ws_timeout);

    // Accept the websocket handshake
    this->ws().async_accept(
        req,
        beast::bind_front_handler(
            &websocket_session_with_acceptor::on_accept,
            std::static_pointer_cast<websocket_session_with_acceptor<Derived>>(
                this->shared_from_this())));
  }

  void on_accept(beast::error_code ec)
  {
    if (ec) {
      this->close();
      return fail(ec, "accept");
    }
    beast::get_lowest_layer(this->ws()).expires_never();
    this->start_read_loop();
  }

public:
  // Start the asynchronous operation
  template <class Body, class Allocator>
  void run(http::request<Body, http::basic_fields<Allocator>> req)
  {
    // Capture cookies from the HTTP Upgrade request so servants can call
    // nprpc::http::get_cookie() on any call dispatched over this WS session.
    auto cookie_it = req.find(http::field::cookie);
    if (cookie_it != req.end()) {
      upgrade_cookies_ = std::string(cookie_it->value());
      this->ctx_.cookies = upgrade_cookies_;
    }

    g_rpc->add_connection(this->shared_from_this());
    // Accept the WebSocket upgrade request
    do_accept(std::move(req));
  }

  explicit websocket_session_with_acceptor(typename Derived::websocket_t&& ws)
      : Derived(std::move(ws))
  {
  }
};

template <>
void make_accepting_websocket_session(
    plain_stream stream,
    http::request<http::string_body, http::basic_fields<std::allocator<char>>>
        req)
{
  std::make_shared<
      websocket_session_with_acceptor<AcceptingPlainWebSocketSession>>(
      plain_ws(std::move(stream)))
      ->run(std::move(req));
}

template <>
void make_accepting_websocket_session(
    ssl_stream stream,
    http::request<http::string_body, http::basic_fields<std::allocator<char>>>
        req)
{
  std::make_shared<
      websocket_session_with_acceptor<AcceptingSSLWebSocketSession>>(
      ssl_ws(std::move(stream)))
      ->run(std::move(req));
}

} // namespace nprpc::impl

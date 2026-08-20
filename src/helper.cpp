// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include "helper.hpp"

#include <nprpc/common.hpp>
#include <nprpc/exception.hpp>

#ifdef NPRPC_SSL_ENABLED
#include <boost/asio/ssl.hpp>
#endif
#if defined(NPRPC_HTTP_ENABLED) || defined(NPRPC_WEBSOCKET_ENABLED)
#include <boost/beast/core.hpp>
#endif

namespace nprpc::impl {

void fail(boost::system::error_code ec, char const* what)
{
  // ssl::error::stream_truncated, also known as an SSL "short read",
  // indicates the peer closed the connection without performing the
  // required closing handshake (for example, Google does this to
  // improve performance). Generally this can be a security issue,
  // but if your communication protocol is self-terminated (as
  // it is with both HTTP and WebSocket) then you may simply
  // ignore the lack of close_notify.
  //
  // https://github.com/boostorg/beast/issues/38
  //
  // https://security.stackexchange.com/questions/91435/how-to-handle-a-malicious-ssl-tls-shutdown
  //
  // When a short read would cut off the end of an HTTP message,
  // Beast returns the error beast::http::error::partial_message.
  // Therefore, if we see a short read here, it has occurred
  // after the message has been completed, so it is safe to ignore it.
#ifdef NPRPC_SSL_ENABLED
  if (ec == net::ssl::error::stream_truncated)
    return;
#endif
#if defined(NPRPC_HTTP_ENABLED) || defined(NPRPC_WEBSOCKET_ENABLED)
  if (ec == beast::error::timeout)
    return;
#endif
  (void)ec;
  (void)what;
}

boost::asio::ip::tcp::endpoint
sync_socket_connect(const EndPoint& endpoint,
                    boost::asio::ip::tcp::socket& socket)
{
  namespace net = boost::asio;
  using tcp = net::ip::tcp;
  // try to create address from hostname
  // if it fails, try to resolve the hostname
  boost::system::error_code ec;
  tcp::endpoint selected_endpoint;
  auto ipv4_addr = net::ip::make_address_v4(endpoint.hostname(), ec);

  if (ec) {
    // Hostname resolution needed - try all resolved endpoints (IPv4/IPv6) until one succeeds
    tcp::resolver resolver(socket.get_executor());
    auto endpoints =
        resolver.resolve(endpoint.hostname(), std::to_string(endpoint.port()));
    if (endpoints.empty()) {
      throw nprpc::Exception(
          ("Could not resolve the hostname: " + ec.message()).c_str());
    }

    // Use Boost.Asio's connect() which automatically tries all endpoints
    // and handles socket state correctly between attempts
    selected_endpoint = net::connect(socket, endpoints, ec);
    
    if (ec) {
      throw nprpc::Exception(
          ("Could not connect to any resolved address for " + 
           std::string(endpoint.hostname()) + ":" + std::to_string(endpoint.port()) + 
           ": " + ec.message()).c_str());
    }
  } else {
    // if the address is valid, set the port
    selected_endpoint = tcp::endpoint(ipv4_addr, endpoint.port());
    socket.connect(selected_endpoint, ec);

    if (ec) {
      throw nprpc::Exception(
          ("Could not connect to the socket (ep=" + selected_endpoint.address().to_string() + 
           ":" + std::to_string(selected_endpoint.port()) + "): " + ec.message()).c_str());
    }

    socket.set_option(net::ip::tcp::no_delay(true), ec);
    if (ec) {
      throw nprpc::Exception(
          ("Could not set TCP_NODELAY option: " + ec.message()).c_str());
    } 
  }

  return selected_endpoint;
}

} // namespace nprpc::impl

// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include "helper.hpp"

#include <nprpc/exception.hpp>

namespace nprpc::impl {

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

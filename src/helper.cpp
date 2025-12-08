// Copyright (c) 2021-2025 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by
// LICENSING file in the topmost directory

#include "helper.hpp"

#include <nprpc/exception.hpp>
#include <nprpc/flat_buffer.hpp>
#include <nprpc/impl/lock_free_ring_buffer.hpp>

namespace nprpc::impl {

boost::asio::ip::tcp::endpoint
sync_socket_connect(const EndPoint& endpoint, boost::asio::ip::tcp::socket& socket) 
{
  namespace net = boost::asio;
  using tcp = net::ip::tcp;
  // try to create address from hostname
  // if it fails, try to resolve the hostname
  boost::system::error_code ec;
  tcp::endpoint selected_endpoint; 
  auto ipv4_addr = net::ip::make_address_v4(endpoint.hostname(), ec);

  if (ec) {
    tcp::resolver resolver(socket.get_executor());
    auto endpoints = resolver.resolve(endpoint.hostname(), std::to_string(endpoint.port()));
    if (endpoints.empty()) {
      throw nprpc::Exception(("Could not resolve the hostname: " + ec.message()).c_str());
    }
    selected_endpoint = endpoints.begin()->endpoint();
    socket.connect(selected_endpoint, ec);
  } else {
    // if the address is valid, set the port
    selected_endpoint = tcp::endpoint(ipv4_addr, endpoint.port());
    socket.connect(selected_endpoint, ec);
  }

  if (ec) {
    throw nprpc::Exception(("Could not connect to the socket: " + ec.message()).c_str());
  }

  return selected_endpoint;
}

} // namespace nprpc::impl

#include <iostream>

// Implementation of flat_buffer::commit_read_if_needed
namespace nprpc {

NPRPC_API void flat_buffer::commit_read_if_needed() {
    if (!has_read_view_) return;

    // Cast back to LockFreeRingBuffer and call commit_read
    auto* ring = static_cast<impl::LockFreeRingBuffer*>(ring_buffer_);
    if (ring) {
        // Reconstruct ReadView
        impl::LockFreeRingBuffer::ReadView view;
        view.data = view_base_;
        view.size = view_size_;
        view.read_idx = read_view_read_idx_;
        view.valid = true;
        
        ring->commit_read(view);
    }
    
    // Clear the tracking
    ring_buffer_ = nullptr;
    read_view_read_idx_ = 0;
    has_read_view_ = false;
}

} // namespace nprpc

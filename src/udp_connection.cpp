// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// This file is a part of npsystem (Distributed Control System) and covered by
// LICENSING file in the topmost directory

#include <nprpc/impl/udp_connection.hpp>
#include <nprpc/impl/nprpc_impl.hpp>
#include <boost/asio/ip/udp.hpp>
#include <iostream>
#include <unordered_map>
#include <mutex>

namespace nprpc::impl {

UdpConnection::UdpConnection(
    boost::asio::io_context& ioc,
    const endpoint_type& remote_endpoint)
    : socket_(ioc, boost::asio::ip::udp::v4())
    , remote_endpoint_(remote_endpoint)
{
    // Set socket options for better performance
    socket_.set_option(boost::asio::socket_base::send_buffer_size(65536));
    
    if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
        std::cout << "[UDP] Connection created to " 
                  << remote_endpoint_.address().to_string() 
                  << ":" << remote_endpoint_.port() << std::endl;
    }
}

UdpConnection::UdpConnection(
    boost::asio::io_context& ioc,
    const std::string& host,
    uint16_t port)
    : socket_(ioc, boost::asio::ip::udp::v4())
{
    // Resolve hostname
    boost::asio::ip::udp::resolver resolver(ioc);
    auto endpoints = resolver.resolve(host, std::to_string(port));
    
    if (endpoints.empty()) {
        throw std::runtime_error("Failed to resolve UDP endpoint: " + host);
    }
    
    remote_endpoint_ = *endpoints.begin();
    
    // Set socket options
    socket_.set_option(boost::asio::socket_base::send_buffer_size(65536));
    
    if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
        std::cout << "[UDP] Connection created to " 
                  << remote_endpoint_.address().to_string() 
                  << ":" << remote_endpoint_.port() << std::endl;
    }
}

UdpConnection::~UdpConnection() {
    close();
    
    if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
        std::cout << "[UDP] Connection destroyed" << std::endl;
    }
}

void UdpConnection::send(flat_buffer&& buffer) {
    // Fire-and-forget - no callback needed
    if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryMessageContent) {
        auto data = buffer.cdata();
        std::cout << "[UDP] send() called with " << data.size() << " bytes to "
                  << remote_endpoint_.address().to_string() << ":" << remote_endpoint_.port() << std::endl;
    }
    send_async(std::move(buffer), nullptr);
}

void UdpConnection::send_async(
    flat_buffer&& buffer,
    std::function<void(const boost::system::error_code&, std::size_t)> handler)
{
    // Post to strand to ensure thread safety
    boost::asio::post(socket_.get_executor(), [
        this,
        self = shared_from_this(),
        buf = std::move(buffer),
        h = std::move(handler)
    ]() mutable {
        send_queue_.push_back(PendingSend{std::move(buf), std::move(h)});
        
        if (!sending_) {
            do_send();
        }
    });
}

void UdpConnection::do_send() {
    if (send_queue_.empty()) {
        sending_ = false;
        return;
    }
    
    sending_ = true;
    auto& pending = send_queue_.front();
    
    auto data = pending.buffer.cdata();
    
    if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryMessageContent) {
        std::cout << "[UDP] Sending " << data.size() << " bytes to "
                  << remote_endpoint_.address().to_string() 
                  << ":" << remote_endpoint_.port() << std::endl;
    }
    
    socket_.async_send_to(
        boost::asio::buffer(data.data(), data.size()),
        remote_endpoint_,
        [this, self = shared_from_this()](
            const boost::system::error_code& ec,
            std::size_t bytes_sent)
        {
            auto pending = std::move(send_queue_.front());
            send_queue_.pop_front();
            
            if (pending.handler) {
                pending.handler(ec, bytes_sent);
            }
            
            if (ec) {
                if (g_cfg.debug_level >= DebugLevel::DebugLevel_Critical) {
                    std::cerr << "[UDP] Send error: " << ec.message() << std::endl;
                }
            }
            
            // Continue with next queued send
            do_send();
        });
}

UdpConnection::endpoint_type UdpConnection::local_endpoint() const {
    boost::system::error_code ec;
    auto ep = socket_.local_endpoint(ec);
    if (ec) {
        return endpoint_type();
    }
    return ep;
}

void UdpConnection::close() {
    if (socket_.is_open()) {
        boost::system::error_code ec;
        socket_.close(ec);
    }
}

// Connection cache for reusing UDP connections
namespace {
    std::mutex udp_connections_mutex_;
    std::unordered_map<std::string, std::weak_ptr<UdpConnection>> udp_connections_;
    
    std::string make_key(const std::string& host, uint16_t port) {
        return host + ":" + std::to_string(port);
    }
}

NPRPC_API std::shared_ptr<UdpConnection> get_udp_connection(
    boost::asio::io_context& ioc,
    const std::string& host,
    uint16_t port)
{
    std::lock_guard<std::mutex> lock(udp_connections_mutex_);
    
    auto key = make_key(host, port);
    
    // Check if we have a cached connection
    auto it = udp_connections_.find(key);
    if (it != udp_connections_.end()) {
        if (auto conn = it->second.lock()) {
            if (conn->is_open()) {
                return conn;
            }
        }
        // Connection expired or closed, remove it
        udp_connections_.erase(it);
    }
    
    // Create new connection
    auto conn = std::make_shared<UdpConnection>(ioc, host, port);
    udp_connections_[key] = conn;
    
    return conn;
}

} // namespace nprpc::impl

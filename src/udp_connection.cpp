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
    : ioc_(ioc)
    , socket_(ioc, boost::asio::ip::udp::v4())
    , remote_endpoint_(remote_endpoint)
{
    // Set socket options for better performance
    socket_.set_option(boost::asio::socket_base::send_buffer_size(65536));
    socket_.set_option(boost::asio::socket_base::receive_buffer_size(65536));
    
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
    : ioc_(ioc)
    , socket_(ioc, boost::asio::ip::udp::v4())
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
    socket_.set_option(boost::asio::socket_base::receive_buffer_size(65536));
    
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

void UdpConnection::send_reliable(
    flat_buffer&& buffer,
    response_handler handler,
    uint32_t timeout_ms,
    uint32_t max_retries)
{
    // Get the request_id from the header
    auto* header = reinterpret_cast<Header*>(buffer.data().data());
    uint32_t request_id = header->request_id;
    
    // If request_id is 0, assign a new one
    if (request_id == 0) {
        request_id = next_request_id_++;
        header->request_id = request_id;
    }
    
    if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
        std::cout << "[UDP] send_reliable() request_id=" << request_id 
                  << " timeout=" << timeout_ms << "ms retries=" << max_retries << std::endl;
    }
    
    // Store pending call info
    boost::asio::post(socket_.get_executor(), [
        this,
        self = shared_from_this(),
        buf = std::move(buffer),
        h = std::move(handler),
        request_id,
        timeout_ms,
        max_retries
    ]() mutable {
        // Create a copy of the buffer for potential retransmits
        flat_buffer request_copy(buf.size());
        auto src = buf.cdata();
        auto mb = request_copy.prepare(src.size());
        std::memcpy(mb.data(), src.data(), src.size());
        request_copy.commit(src.size());
        
        // Create timer
        auto timer = std::make_unique<boost::asio::steady_timer>(ioc_);
        
        // Store pending call
        pending_calls_[request_id] = PendingCall{
            std::move(request_copy),
            std::move(h),
            std::move(timer),
            timeout_ms,
            max_retries,
            0
        };
        
        // Start receiving if not already
        if (!receiving_) {
            start_receive();
        }
        
        // Start timeout timer
        auto& pending = pending_calls_[request_id];
        pending.timer->expires_after(std::chrono::milliseconds(timeout_ms));
        pending.timer->async_wait([this, self = shared_from_this(), request_id](
            const boost::system::error_code& ec)
        {
            if (!ec) {
                do_retransmit(request_id);
            }
        });
        
        // Send the original buffer
        send_queue_.push_back(PendingSend{std::move(buf), nullptr});
        if (!sending_) {
            do_send();
        }
    });
}

void UdpConnection::start_receive() {
    if (receiving_ || !socket_.is_open()) return;
    
    receiving_ = true;
    
    socket_.async_receive_from(
        boost::asio::buffer(recv_buffer_),
        recv_endpoint_,
        [this, self = shared_from_this()](
            const boost::system::error_code& ec,
            std::size_t bytes_received)
        {
            if (ec) {
                if (ec != boost::asio::error::operation_aborted) {
                    if (g_cfg.debug_level >= DebugLevel::DebugLevel_Critical) {
                        std::cerr << "[UDP] Receive error: " << ec.message() << std::endl;
                    }
                }
                receiving_ = false;
                return;
            }
            
            if (bytes_received > 0) {
                handle_response(bytes_received);
            }
            
            // Continue receiving if we still have pending calls
            if (!pending_calls_.empty()) {
                receiving_ = false;
                start_receive();
            } else {
                receiving_ = false;
            }
        });
}

void UdpConnection::handle_response(size_t bytes_received) {
    if (bytes_received < sizeof(Header)) {
        if (g_cfg.debug_level >= DebugLevel::DebugLevel_Critical) {
            std::cerr << "[UDP] Response too small: " << bytes_received << std::endl;
        }
        return;
    }
    
    auto* header = reinterpret_cast<const Header*>(recv_buffer_.data());
    uint32_t request_id = header->request_id;
    
    if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
        std::cout << "[UDP] Received response request_id=" << request_id 
                  << " size=" << bytes_received << std::endl;
    }
    
    // Find pending call
    auto it = pending_calls_.find(request_id);
    if (it == pending_calls_.end()) {
        if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
            std::cout << "[UDP] No pending call for request_id=" << request_id << std::endl;
        }
        return;
    }
    
    // Cancel timer
    it->second.timer->cancel();
    
    // Copy response to flat_buffer
    flat_buffer response(bytes_received);
    auto mb = response.prepare(bytes_received);
    std::memcpy(mb.data(), recv_buffer_.data(), bytes_received);
    response.commit(bytes_received);
    
    // Move handler out before erasing
    auto handler = std::move(it->second.handler);
    pending_calls_.erase(it);
    
    // Call handler with success
    if (handler) {
        handler(boost::system::error_code{}, response);
    }
}

void UdpConnection::do_retransmit(uint32_t request_id) {
    auto it = pending_calls_.find(request_id);
    if (it == pending_calls_.end()) {
        return;  // Already completed or cancelled
    }
    
    auto& pending = it->second;
    pending.retry_count++;
    
    if (pending.retry_count > pending.max_retries) {
        // Max retries exceeded - call handler with timeout error
        if (g_cfg.debug_level >= DebugLevel::DebugLevel_Critical) {
            std::cerr << "[UDP] Timeout after " << pending.max_retries 
                      << " retries for request_id=" << request_id << std::endl;
        }
        
        auto handler = std::move(pending.handler);
        pending_calls_.erase(it);
        
        if (handler) {
            flat_buffer empty_buf;
            handler(boost::asio::error::timed_out, empty_buf);
        }
        return;
    }
    
    if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
        std::cout << "[UDP] Retransmit #" << pending.retry_count 
                  << " for request_id=" << request_id << std::endl;
    }
    
    // Create a copy of the request for retransmit
    flat_buffer retransmit_buf(pending.request.size());
    auto src = pending.request.cdata();
    auto mb = retransmit_buf.prepare(src.size());
    std::memcpy(mb.data(), src.data(), src.size());
    retransmit_buf.commit(src.size());
    
    // Restart timer
    pending.timer->expires_after(std::chrono::milliseconds(pending.timeout_ms));
    pending.timer->async_wait([this, self = shared_from_this(), request_id](
        const boost::system::error_code& ec)
    {
        if (!ec) {
            do_retransmit(request_id);
        }
    });
    
    // Queue the retransmit
    send_queue_.push_back(PendingSend{std::move(retransmit_buf), nullptr});
    if (!sending_) {
        do_send();
    }
}

void UdpConnection::close() {
    // Cancel all pending calls
    for (auto& [id, pending] : pending_calls_) {
        pending.timer->cancel();
        if (pending.handler) {
            flat_buffer empty_buf;
            pending.handler(boost::asio::error::operation_aborted, empty_buf);
        }
    }
    pending_calls_.clear();
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

void clear_udp_connections() {
    std::lock_guard<std::mutex> lock(udp_connections_mutex_);
    udp_connections_.clear();
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

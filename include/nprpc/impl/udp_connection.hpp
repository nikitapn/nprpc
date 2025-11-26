// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// This file is a part of npsystem (Distributed Control System) and covered by
// LICENSING file in the topmost directory

#pragma once

#include <boost/asio/ip/udp.hpp>
#include <boost/asio/strand.hpp>
#include <nprpc/nprpc.hpp>
#include <nprpc/buffer.hpp>
#include <deque>
#include <memory>
#include <functional>

namespace nprpc::impl {

/**
 * @brief UDP connection for fire-and-forget RPC calls
 * 
 * Unlike TCP/WebSocket connections, UDP doesn't maintain a session.
 * Each datagram is independent. This class manages a UDP socket for
 * sending datagrams to a specific endpoint.
 * 
 * Features:
 * - Fire-and-forget send (no reply expected)
 * - Optional sequence numbers for ordering
 * - Asynchronous send queue to prevent blocking
 */
class UdpConnection : public std::enable_shared_from_this<UdpConnection> {
public:
    using endpoint_type = boost::asio::ip::udp::endpoint;
    
    /**
     * @brief Construct UDP connection to a specific endpoint
     * 
     * @param ioc IO context for async operations
     * @param remote_endpoint Target endpoint (host:port)
     */
    UdpConnection(boost::asio::io_context& ioc,
                  const endpoint_type& remote_endpoint);
    
    /**
     * @brief Construct UDP connection from hostname and port
     * 
     * @param ioc IO context for async operations
     * @param host Hostname or IP address
     * @param port UDP port number
     */
    UdpConnection(boost::asio::io_context& ioc,
                  const std::string& host,
                  uint16_t port);
    
    ~UdpConnection();
    
    /**
     * @brief Send datagram (fire-and-forget)
     * 
     * Queues the buffer for sending. Returns immediately.
     * No acknowledgment or reply is expected.
     * 
     * @param buffer Message buffer to send (moved)
     */
    void send(flat_buffer&& buffer);
    
    /**
     * @brief Send datagram with completion callback
     * 
     * @param buffer Message buffer to send (moved)
     * @param handler Called when send completes (may be with error)
     */
    void send_async(flat_buffer&& buffer,
                    std::function<void(const boost::system::error_code&, std::size_t)> handler);
    
    /**
     * @brief Get the remote endpoint
     */
    const endpoint_type& remote_endpoint() const noexcept { return remote_endpoint_; }
    
    /**
     * @brief Get the local endpoint (after socket is bound)
     */
    endpoint_type local_endpoint() const;
    
    /**
     * @brief Check if connection is open
     */
    bool is_open() const noexcept { return socket_.is_open(); }
    
    /**
     * @brief Close the socket
     */
    void close();
    
    /**
     * @brief Get next sequence number (for ordered delivery)
     */
    uint32_t next_sequence() noexcept { return sequence_++; }

private:
    void do_send();
    void start_send_queue();
    
    boost::asio::ip::udp::socket socket_;
    endpoint_type remote_endpoint_;
    
    // Send queue for async operations
    struct PendingSend {
        flat_buffer buffer;
        std::function<void(const boost::system::error_code&, std::size_t)> handler;
    };
    std::deque<PendingSend> send_queue_;
    bool sending_ = false;
    
    // Sequence number for ordered delivery
    std::atomic<uint32_t> sequence_{0};
};

/**
 * @brief Create or get cached UDP connection for endpoint
 * 
 * UDP connections are lightweight and can be reused.
 * This function returns a shared connection for the given endpoint.
 * 
 * @param ioc IO context
 * @param host Target hostname or IP
 * @param port Target UDP port
 * @return Shared pointer to UDP connection
 */
NPRPC_API std::shared_ptr<UdpConnection> get_udp_connection(
    boost::asio::io_context& ioc,
    const std::string& host,
    uint16_t port);

} // namespace nprpc::impl

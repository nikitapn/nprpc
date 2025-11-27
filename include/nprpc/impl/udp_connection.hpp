// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// This file is a part of npsystem (Distributed Control System) and covered by
// LICENSING file in the topmost directory

#pragma once

#include <boost/asio/ip/udp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/steady_timer.hpp>
#include <nprpc/nprpc.hpp>
#include <nprpc/buffer.hpp>
#include <deque>
#include <memory>
#include <functional>
#include <unordered_map>

namespace nprpc::impl {

/**
 * @brief UDP connection for RPC calls (both fire-and-forget and reliable)
 * 
 * Unlike TCP/WebSocket connections, UDP doesn't maintain a session.
 * Each datagram is independent. This class manages a UDP socket for
 * sending datagrams to a specific endpoint.
 * 
 * Features:
 * - Fire-and-forget send (no reply expected)
 * - Reliable send with ACK/retransmit for [reliable] methods
 * - Optional sequence numbers for ordering
 * - Asynchronous send queue to prevent blocking
 */
class UdpConnection : public std::enable_shared_from_this<UdpConnection> {
public:
    using endpoint_type = boost::asio::ip::udp::endpoint;
    using response_handler = std::function<void(const boost::system::error_code&, flat_buffer&)>;
    
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
     * @brief Send blocking reliable datagram and wait for response
     * 
     * For blocking callers - buffer passed by reference (no copy for initial send).
     * Buffer is copied on first timeout for retransmit.
     * 
     * @param buffer Message buffer (reference - caller blocks, so no copy needed)
     * @param handler Called with response or error (timeout, etc.)
     * @param timeout_ms Timeout per attempt in milliseconds
     * @param max_retries Maximum number of retransmit attempts
     */
    void send_reliable(flat_buffer& buffer,
                       response_handler handler,
                       uint32_t timeout_ms = 500,
                       uint32_t max_retries = 3);
    
    /**
     * @brief Send async reliable datagram with completion handler
     * 
     * For async callers - buffer is moved and copied for retransmit.
     * Handler is called when response is received or on timeout.
     * 
     * @param buffer Message buffer (moved - copied for retransmit)
     * @param handler Called with response or error (timeout, etc.)
     * @param timeout_ms Timeout per attempt in milliseconds
     * @param max_retries Maximum number of retransmit attempts
     */
    void send_reliable_async(flat_buffer&& buffer,
                             response_handler handler,
                             uint32_t timeout_ms = 500,
                             uint32_t max_retries = 3);
    
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
    void start_receive();
    void handle_response(size_t bytes_received);
    void do_retransmit(uint32_t request_id);
    
    boost::asio::io_context& ioc_;
    boost::asio::ip::udp::socket socket_;
    endpoint_type remote_endpoint_;
    
    // Send queue for async operations
    struct PendingSend {
        flat_buffer buffer;
        std::function<void(const boost::system::error_code&, std::size_t)> handler;
    };
    std::deque<PendingSend> send_queue_;
    bool sending_ = false;
    
    // Receive buffer for responses
    std::array<uint8_t, 65536> recv_buffer_;
    endpoint_type recv_endpoint_;
    bool receiving_ = false;
    
    // Pending reliable calls awaiting response
    struct PendingCall {
        flat_buffer request;           // Copy of request for retransmit (lazy - copied on first timeout)
        response_handler handler;       // Completion handler
        std::unique_ptr<boost::asio::steady_timer> timer;
        uint32_t timeout_ms;
        uint32_t max_retries;
        uint32_t retry_count = 0;
        bool request_saved = false;    // True once we've saved a copy for retransmit
    };
    std::unordered_map<uint32_t, PendingCall> pending_calls_;
    
    // Sequence number for ordered delivery
    std::atomic<uint32_t> sequence_{0};
    
    // Request ID for reliable calls
    std::atomic<uint32_t> next_request_id_{1};
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

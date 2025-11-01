#pragma once

#include <nprpc/impl/lock_free_ring_buffer.hpp>
#include <boost/asio.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <functional>
#include <vector>

namespace nprpc::impl {

/**
 * @brief Bidirectional IPC channel using lock-free ring buffers in shared memory
 * 
 * Uses memory-mapped files with lock-free ring buffers for true zero-copy IPC.
 * Two ring buffers: one for server-to-client, one for client-to-server.
 * Automatically handles cleanup when the last reference is destroyed.
 */
class SharedMemoryChannel {
public:
    static constexpr size_t MAX_MESSAGE_SIZE = 32 * 1024 * 1024;  // 32MB (same as TCP/WebSocket)
    static constexpr uint32_t RING_BUFFER_CAPACITY = 128;         // Number of slots per ring buffer
    static constexpr uint32_t RING_BUFFER_SLOT_SIZE = 64 * 1024;  // 64KB per slot

private:
    std::string channel_id_;
    std::string send_ring_name_;     // Ring buffer we write to
    std::string recv_ring_name_;     // Ring buffer we read from
    
    std::unique_ptr<LockFreeRingBuffer> send_ring_;
    std::unique_ptr<LockFreeRingBuffer> recv_ring_;
    
    bool is_server_;
    boost::asio::io_context& ioc_;
    
    std::unique_ptr<std::thread> read_thread_;
    std::atomic<bool> running_{true};
    
    // Buffer for receiving messages
    std::vector<char> recv_buffer_;

public:
    /**
     * @brief Construct a new Shared Memory Channel
     * 
     * @param ioc Boost.Asio io_context for async operations
     * @param channel_id Unique channel identifier (shared between client and server)
     * @param is_server true if this is the server side, false for client
     * @param create_rings true to create ring buffers (server), false to open existing (client)
     */
    SharedMemoryChannel(
        boost::asio::io_context& ioc, 
        const std::string& channel_id,
        bool is_server,
        bool create_rings);

    ~SharedMemoryChannel();

    // Non-copyable, movable
    SharedMemoryChannel(const SharedMemoryChannel&) = delete;
    SharedMemoryChannel& operator=(const SharedMemoryChannel&) = delete;
    SharedMemoryChannel(SharedMemoryChannel&&) = default;
    SharedMemoryChannel& operator=(SharedMemoryChannel&&) = default;

    /**
     * @brief Send data through the channel (non-blocking)
     * 
     * @param data Pointer to data to send
     * @param size Size of data in bytes
     * @return true if sent successfully, false if buffer is full or error
     */
    bool send(const void* data, uint32_t size);

    /**
     * @brief Callback invoked when data is received
     * 
     * Called on io_context thread. The vector is moved to avoid copying.
     */
    std::function<void(std::vector<char>&&)> on_data_received;

    /**
     * @brief Generate a unique channel ID
     */
    static std::string generate_channel_id() {
        return boost::uuids::to_string(boost::uuids::random_generator()());
    }

    /**
     * @brief Check if channel is valid and connected
     */
    bool is_valid() const {
        return send_ring_ && recv_ring_;
    }

private:
    void read_loop();
    void cleanup_rings();
};

} // namespace nprpc::impl

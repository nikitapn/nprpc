#pragma once

#include <boost/asio.hpp>
#include <boost/interprocess/ipc/message_queue.hpp>
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/mapped_region.hpp>
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
 * @brief Bidirectional IPC channel using Boost.Interprocess message queues
 * 
 * Uses two message queues for server-to-client and client-to-server communication.
 * Automatically handles cleanup when the last reference is destroyed.
 */
class SharedMemoryChannel {
public:
    static constexpr size_t MAX_MESSAGE_SIZE = 32 * 1024 * 1024;  // 32MB (same as TCP/WebSocket)
    static constexpr size_t MAX_QUEUE_MESSAGES = 100;              // Limit queue depth

private:
    std::string channel_id_;
    std::string send_queue_name_;    // Queue we write to
    std::string recv_queue_name_;    // Queue we read from
    
    std::unique_ptr<boost::interprocess::message_queue> send_queue_;
    std::unique_ptr<boost::interprocess::message_queue> recv_queue_;
    
    bool is_server_;
    boost::asio::io_context& ioc_;
    
    std::unique_ptr<std::thread> poll_thread_;
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
     * @param create_queues true to create queues (server), false to open existing (client)
     */
    SharedMemoryChannel(
        boost::asio::io_context& ioc, 
        const std::string& channel_id,
        bool is_server,
        bool create_queues);

    ~SharedMemoryChannel();

    // Non-copyable, movable
    SharedMemoryChannel(const SharedMemoryChannel&) = delete;
    SharedMemoryChannel& operator=(const SharedMemoryChannel&) = delete;
    SharedMemoryChannel(SharedMemoryChannel&&) = default;
    SharedMemoryChannel& operator=(SharedMemoryChannel&&) = default;

    /**
     * @brief Send data through the channel
     * 
     * @param data Pointer to data to send
     * @param size Size of data in bytes
     * @return true if sent successfully, false if queue is full or error
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
        return send_queue_ && recv_queue_;
    }

private:
    void poll_loop();
    void cleanup_queues();
};

} // namespace nprpc::impl

#include <nprpc/impl/shared_memory_channel.hpp>
#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/common.hpp>
#include <iostream>

namespace nprpc::impl {

SharedMemoryChannel::SharedMemoryChannel(
    boost::asio::io_context& ioc,
    const std::string& channel_id,
    bool is_server,
    bool create_rings)
    : channel_id_(channel_id)
    , is_server_(is_server)
    , ioc_(ioc)
    , recv_buffer_(MAX_MESSAGE_SIZE)
{
    // Server writes to s2c, reads from c2s
    // Client writes to c2s, reads from s2c
    send_ring_name_ = make_shm_name(channel_id, is_server ? "s2c" : "c2s");
    recv_ring_name_ = make_shm_name(channel_id, is_server ? "c2s" : "s2c");

    try {
        if (create_rings) {
            // Remove any existing shared memory from crashed processes
            LockFreeRingBuffer::remove(send_ring_name_);
            LockFreeRingBuffer::remove(recv_ring_name_);

            // Create new ring buffers
            send_ring_ = LockFreeRingBuffer::create(
                send_ring_name_,
                RING_BUFFER_CAPACITY,
                RING_BUFFER_SLOT_SIZE);

            recv_ring_ = LockFreeRingBuffer::create(
                recv_ring_name_,
                RING_BUFFER_CAPACITY,
                RING_BUFFER_SLOT_SIZE);

            if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
                std::cout << "Created ring buffers: " << send_ring_name_ 
                         << ", " << recv_ring_name_ << std::endl;
            }
        } else {
            // Open existing ring buffers
            send_ring_ = LockFreeRingBuffer::open(send_ring_name_);
            recv_ring_ = LockFreeRingBuffer::open(recv_ring_name_);

            if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
                std::cout << "Opened ring buffers: " << send_ring_name_ 
                         << ", " << recv_ring_name_ << std::endl;
            }
        }

        // Start read thread with blocking read
        read_thread_ = std::make_unique<std::thread>([this]() { read_loop(); });

    } catch (const std::exception& e) {
        std::cerr << "Failed to create/open ring buffers: " << e.what() << std::endl;
        cleanup_rings();
        throw std::runtime_error(std::string("SharedMemoryChannel initialization failed: ") + e.what());
    }
}

SharedMemoryChannel::~SharedMemoryChannel() {
    running_ = false;

    if (read_thread_ && read_thread_->joinable()) {
        read_thread_->join();
    }

    cleanup_rings();
}

bool SharedMemoryChannel::send(const void* data, uint32_t size) {
    if (!send_ring_ || size > MAX_MESSAGE_SIZE) {
        return false;
    }

    try {
        bool sent = send_ring_->try_write(data, size);
        
        if (!sent && g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
            std::cerr << "SharedMemoryChannel: Ring buffer full, message dropped" << std::endl;
        }
        
        return sent;

    } catch (const std::exception& e) {
        std::cerr << "SharedMemoryChannel send error: " << e.what() << std::endl;
        return false;
    }
}

void SharedMemoryChannel::read_loop() {
    while (running_) {
        try {
            // Blocking read with timeout (allows checking running_ flag)
            size_t bytes_read = recv_ring_->read_with_timeout(
                recv_buffer_.data(), 
                recv_buffer_.size(),
                std::chrono::milliseconds(100));

            if (bytes_read > 0) {
                // Validate message size (security check)
                if (bytes_read > MAX_MESSAGE_SIZE) {
                    std::cerr << "SharedMemoryChannel: Rejected oversized message: " 
                             << bytes_read << " bytes (max: " << MAX_MESSAGE_SIZE << ")" << std::endl;
                    continue;
                }

                // Message received, post to io_context
                std::vector<char> data(recv_buffer_.begin(), recv_buffer_.begin() + bytes_read);
                
                boost::asio::post(ioc_, [this, data = std::move(data)]() mutable {
                    if (on_data_received) {
                        on_data_received(std::move(data));
                    }
                });
            }
        } catch (const std::exception& e) {
            if (running_) {
                std::cerr << "SharedMemoryChannel receive error: " << e.what() << std::endl;
            }
            break;
        }
    }

    if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
        std::cout << "SharedMemoryChannel read thread exiting" << std::endl;
    }
}

void SharedMemoryChannel::cleanup_rings() {
    send_ring_.reset();
    recv_ring_.reset();

    // Only the creator (server) should remove the shared memory
    if (is_server_) {
        try {
            LockFreeRingBuffer::remove(send_ring_name_);
            LockFreeRingBuffer::remove(recv_ring_name_);
            
            if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
                std::cout << "Cleaned up ring buffers: " << send_ring_name_ 
                         << ", " << recv_ring_name_ << std::endl;
            }
        } catch (const std::exception& e) {
            // Ignore cleanup errors
            if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
                std::cout << "Cleanup error (ignored): " << e.what() << std::endl;
            }
        }
    }
}

} // namespace nprpc::impl

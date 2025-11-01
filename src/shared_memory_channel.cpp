#include <nprpc/impl/shared_memory_channel.hpp>
#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/common.hpp>
#include <iostream>

namespace bip = boost::interprocess;

namespace nprpc::impl {

SharedMemoryChannel::SharedMemoryChannel(
    boost::asio::io_context& ioc,
    const std::string& channel_id,
    bool is_server,
    bool create_queues)
    : channel_id_(channel_id)
    , is_server_(is_server)
    , ioc_(ioc)
    , recv_buffer_(MAX_MESSAGE_SIZE)
{
    // Server writes to s2c, reads from c2s
    // Client writes to c2s, reads from s2c
    send_queue_name_ = channel_id + (is_server ? "_s2c" : "_c2s");
    recv_queue_name_ = channel_id + (is_server ? "_c2s" : "_s2c");

    try {
        if (create_queues) {
            // Remove any existing queues from crashed processes
            bip::message_queue::remove(send_queue_name_.c_str());
            bip::message_queue::remove(recv_queue_name_.c_str());

            // Create new queues with message size and max queue depth
            send_queue_ = std::make_unique<bip::message_queue>(
                bip::create_only,
                send_queue_name_.c_str(),
                MAX_QUEUE_MESSAGES,
                MAX_MESSAGE_SIZE);

            recv_queue_ = std::make_unique<bip::message_queue>(
                bip::create_only,
                recv_queue_name_.c_str(),
                MAX_QUEUE_MESSAGES,
                MAX_MESSAGE_SIZE);

            if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
                std::cout << "Created message queues: " << send_queue_name_ 
                          << ", " << recv_queue_name_ << std::endl;
            }
        } else {
            // Open existing queues
            send_queue_ = std::make_unique<bip::message_queue>(
                bip::open_only,
                send_queue_name_.c_str());

            recv_queue_ = std::make_unique<bip::message_queue>(
                bip::open_only,
                recv_queue_name_.c_str());

            if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
                std::cout << "Opened message queues: " << send_queue_name_ 
                          << ", " << recv_queue_name_ << std::endl;
            }
        }

        // Start polling thread for receiving messages
        poll_thread_ = std::make_unique<std::thread>([this]() { poll_loop(); });

    } catch (const bip::interprocess_exception& e) {
        std::cerr << "Failed to create/open message queues: " << e.what() << std::endl;
        cleanup_queues();
        throw std::runtime_error(std::string("SharedMemoryChannel initialization failed: ") + e.what());
    }
}

SharedMemoryChannel::~SharedMemoryChannel() {
    running_ = false;

    if (poll_thread_ && poll_thread_->joinable()) {
        poll_thread_->join();
    }

    cleanup_queues();
}

bool SharedMemoryChannel::send(const void* data, uint32_t size) {
    if (!send_queue_ || size > MAX_MESSAGE_SIZE) {
        return false;
    }

    try {
        // Use timed_send with very short timeout to avoid blocking
        // Priority 0 (normal priority)
        auto timeout = boost::posix_time::microsec_clock::universal_time() + 
                       boost::posix_time::milliseconds(100);
        
        bool sent = send_queue_->timed_send(data, size, 0, timeout);
        
        if (!sent && g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
            std::cerr << "SharedMemoryChannel: Queue full, message dropped" << std::endl;
        }
        
        return sent;

    } catch (const bip::interprocess_exception& e) {
        std::cerr << "SharedMemoryChannel send error: " << e.what() << std::endl;
        return false;
    }
}

void SharedMemoryChannel::poll_loop() {
    unsigned int priority;
    bip::message_queue::size_type recv_size;

    while (running_) {
        try {
            // Use timed_receive to allow checking running_ flag
            auto timeout = boost::posix_time::microsec_clock::universal_time() + 
                           boost::posix_time::milliseconds(100);

            if (recv_queue_->timed_receive(recv_buffer_.data(), recv_buffer_.size(), 
                                          recv_size, priority, timeout)) {
                // Validate message size (security check)
                if (recv_size > max_message_size) {
                    std::cerr << "SharedMemoryChannel: Rejected oversized message: " 
                              << recv_size << " bytes (max: " << max_message_size << ")" << std::endl;
                    continue;
                }

                // Message received, post to io_context
                std::vector<char> data(recv_buffer_.begin(), recv_buffer_.begin() + recv_size);
                
                boost::asio::post(ioc_, [this, data = std::move(data)]() mutable {
                    if (on_data_received) {
                        on_data_received(std::move(data));
                    }
                });
            }
        } catch (const bip::interprocess_exception& e) {
            if (running_) {
                std::cerr << "SharedMemoryChannel receive error: " << e.what() << std::endl;
            }
            break;
        }
    }

    if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
        std::cout << "SharedMemoryChannel poll thread exiting" << std::endl;
    }
}

void SharedMemoryChannel::cleanup_queues() {
    send_queue_.reset();
    recv_queue_.reset();

    // Only the creator (server) should remove the queues
    if (is_server_) {
        try {
            bip::message_queue::remove(send_queue_name_.c_str());
            bip::message_queue::remove(recv_queue_name_.c_str());
            
            if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
                std::cout << "Cleaned up message queues: " << send_queue_name_ 
                          << ", " << recv_queue_name_ << std::endl;
            }
        } catch (const bip::interprocess_exception& e) {
            // Ignore cleanup errors
        }
    }
}

} // namespace nprpc::impl

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
            // Create new ring buffers (continuous, variable-sized)
            send_ring_ = LockFreeRingBuffer::create(
                send_ring_name_,
                RING_BUFFER_SIZE);

            recv_ring_ = LockFreeRingBuffer::create(
                recv_ring_name_,
                RING_BUFFER_SIZE);

            if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
                std::cout << "Created ring buffers: " << send_ring_name_ 
                         << ", " << recv_ring_name_ << " (" << RING_BUFFER_SIZE << " bytes each)" << std::endl;
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

    // Signal the condition variable multiple times to wake up the read thread
    // This is needed because the thread might be blocked in timed_wait
    // We notify multiple times to catch different timing windows
    for (int i = 0; i < 3; ++i) {
        if (recv_ring_) {
            try {
                recv_ring_->header()->data_available.notify_all();
            } catch (...) {
                // Ignore errors - shared memory might already be destroyed
                break;
            }
        }
        // Small delay to let the thread check running_ flag
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // Check if thread has already exited
        if (!read_thread_ || !read_thread_->joinable()) {
            break;
        }
    }

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

LockFreeRingBuffer::WriteReservation SharedMemoryChannel::reserve_write(size_t max_size) {
    if (!send_ring_) {
        return LockFreeRingBuffer::WriteReservation{nullptr, 0, 0, false};
    }
    // std::cout << "[nprpc][D] reserve_write on ring: " << send_ring_name_ << std::endl;
    return send_ring_->try_reserve_write(max_size);
}

void SharedMemoryChannel::commit_write(
    const LockFreeRingBuffer::WriteReservation& reservation,
    size_t actual_size) {
    if (send_ring_) {
        // std::cout << "[nprpc][D] commit_write on ring: " << send_ring_name_ 
        //           << " size=" << actual_size << std::endl;
        send_ring_->commit_write(reservation, actual_size);
    }
}

void SharedMemoryChannel::commit_read(const LockFreeRingBuffer::ReadView& view) {
    if (recv_ring_) {
        // std::string side = is_server_ ? "SERVER" : "CLIENT";
        // std::cout << "[nprpc][D] " << side << " commit_read on ring: " << recv_ring_name_ << std::endl;
        recv_ring_->commit_read(view);
    }
}

void SharedMemoryChannel::read_loop() {
    // std::string side = is_server_ ? "SERVER" : "CLIENT";
    // std::cout << "[nprpc][D] " << side << " read_loop starting for recv_ring: " << recv_ring_name_ << std::endl;
    while (running_) {
        try {
            // Try zero-copy read first if callback is set
            if (on_data_received_view) {
                // std::cout << "Attempting zero-copy read" << std::endl;
                // Wait for data with timeout
                {
                    boost::interprocess::scoped_lock<boost::interprocess::interprocess_mutex> 
                        lock(recv_ring_->header()->mutex);

                    auto deadline = boost::posix_time::microsec_clock::universal_time() + 
                                    boost::posix_time::milliseconds(100);

                    while (recv_ring_->is_empty() && running_) {
                        auto now = boost::posix_time::microsec_clock::universal_time();
                        if (now >= deadline) {
                            break; // Timeout, check running_ and try again
                        }
                        recv_ring_->header()->data_available.timed_wait(lock, deadline);
                    }
                }

                // Try to get a view into the ring buffer
                auto view = recv_ring_->try_read_view();
                // std::cout << "Zero-copy read view attempt returned valid=" << view.valid << std::endl;
                if (view) {
                    // std::cout << "[nprpc][D] " << side << " read_loop got view from " << recv_ring_name_ 
                    //           << " size=" << view.size << std::endl;
                    // Zero-copy path: provide view directly to callback
                    // Call synchronously in read thread to ensure commit_read() completes before next iteration
                    on_data_received_view(view);
                    // this->commit_read(view);
                    // boost::asio::post(ioc_, [this, view]() mutable {
                    //     if (on_data_received_view) {
                    //         on_data_received_view(view);
                    // }
                    // });
                }
                continue;
            }

            // std::cout << "Falling back to copy-based read" << std::endl;

            // Fallback to copy-based read
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
    std::cout << "Cleaning up SharedMemoryChannel rings for channel: " 
              << channel_id_ << std::endl;
    send_ring_.reset();
    recv_ring_.reset();

    // Only the creator (server) should remove the shared memory
    if (is_server_) {
        try {
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

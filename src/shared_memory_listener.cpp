#include <nprpc/impl/shared_memory_listener.hpp>
#include <nprpc/impl/shared_memory_channel.hpp>
#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/common.hpp>
#include <iostream>

namespace nprpc::impl {

SharedMemoryListener::SharedMemoryListener(
    boost::asio::io_context& ioc,
    const std::string& listener_name,
    AcceptHandler accept_handler)
    : listener_name_(listener_name)
    , ioc_(ioc)
    , accept_handler_(std::move(accept_handler))
{
    if (listener_name_.empty()) {
        throw std::invalid_argument("Listener name cannot be empty");
    }

    if (!accept_handler_) {
        throw std::invalid_argument("Accept handler cannot be null");
    }

    // Create well-known accept ring buffer
    // Remove any existing ring from crashed server
    std::string accept_ring_name = make_shm_name(listener_name_, "accept");
    LockFreeRingBuffer::remove(accept_ring_name);

    try {
        // Small ring buffer for handshakes (10 slots, each can hold a handshake)
        accept_ring_ = LockFreeRingBuffer::create(
            accept_ring_name,
            10,  // 10 pending connections
            1024);  // 1KB per slot (enough for handshake)

        if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
            std::cout << "SharedMemoryListener created: " << listener_name_ << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "Failed to create listener ring: " << e.what() << std::endl;
        throw std::runtime_error(std::string("SharedMemoryListener creation failed: ") + e.what());
    }
}

SharedMemoryListener::~SharedMemoryListener() {
    stop();

    // Clean up accept ring
    accept_ring_.reset();
    
    try {
        std::string accept_ring_name = make_shm_name(listener_name_, "accept");
        LockFreeRingBuffer::remove(accept_ring_name);
        
        if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
            std::cout << "SharedMemoryListener cleaned up: " << listener_name_ << std::endl;
        }
    } catch (const std::exception& e) {
        // Ignore cleanup errors
    }
}

void SharedMemoryListener::start() {
    if (running_) {
        return;
    }

    running_ = true;
    accept_thread_ = std::make_unique<std::thread>([this]() { accept_loop(); });

    if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
        std::cout << "SharedMemoryListener started: " << listener_name_ << std::endl;
    }
}

void SharedMemoryListener::stop() {
    if (!running_) {
        return;
    }

    running_ = false;

    if (accept_thread_ && accept_thread_->joinable()) {
        accept_thread_->join();
    }

    if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
        std::cout << "SharedMemoryListener stopped: " << listener_name_ << std::endl;
    }
}

void SharedMemoryListener::accept_loop() {
    char buffer[1024];  // Buffer for handshake
    
    while (running_) {
        try {
            // Wait for connection request with timeout
            size_t bytes_read = accept_ring_->read_with_timeout(
                buffer, 
                sizeof(buffer),
                std::chrono::milliseconds(100));

            if (bytes_read > 0) {
                // Validate handshake size
                if (bytes_read != sizeof(SharedMemoryHandshake)) {
                    std::cerr << "SharedMemoryListener: Invalid handshake size: " 
                              << bytes_read << std::endl;
                    continue;
                }

                // Parse handshake
                SharedMemoryHandshake handshake;
                std::memcpy(&handshake, buffer, sizeof(SharedMemoryHandshake));

                if (!handshake.is_valid()) {
                    std::cerr << "SharedMemoryListener: Invalid handshake magic/version" << std::endl;
                    continue;
                }

                // Handle the connection request
                handle_connection_request(handshake);
            }
        } catch (const std::exception& e) {
            if (running_) {
                std::cerr << "SharedMemoryListener accept error: " << e.what() << std::endl;
            }
            break;
        }
    }

    if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
        std::cout << "SharedMemoryListener accept loop exiting" << std::endl;
    }
}

void SharedMemoryListener::handle_connection_request(const SharedMemoryHandshake& handshake) {
    std::string channel_id(handshake.channel_id);

    if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
        std::cout << "SharedMemoryListener: Accepting connection on channel: " 
                  << channel_id << std::endl;
    }

    try {
        // Create dedicated channel for this client (server creates the rings)
        auto channel = std::make_unique<SharedMemoryChannel>(
            ioc_, 
            channel_id, 
            /*is_server=*/true, 
            /*create_rings=*/true);

        if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
            std::cout << "SharedMemoryListener: Channel created successfully: " << channel_id << std::endl;
        }

        // Call accept handler immediately in this thread
        // This ensures on_data_received is set before any messages arrive
        if (accept_handler_) {
            accept_handler_(std::move(channel));
        }

    } catch (const std::exception& e) {
        std::cerr << "SharedMemoryListener: Failed to create channel: " << e.what() << std::endl;
    }
}

// Client-side connection establishment
std::unique_ptr<SharedMemoryChannel> connect_to_shared_memory_listener(
    boost::asio::io_context& ioc,
    const std::string& listener_name)
{
    if (listener_name.empty()) {
        throw std::invalid_argument("Listener name cannot be empty");
    }

    // Generate unique channel ID for this connection
    std::string channel_id = SharedMemoryChannel::generate_channel_id();

    if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
        std::cout << "Connecting to listener: " << listener_name 
                  << " with channel: " << channel_id << std::endl;
    }

    // Create our side of the channel first (client doesn't create queues yet)
    // We'll wait for the server to create them
    
    // Prepare handshake
    SharedMemoryHandshake handshake;
    std::strncpy(handshake.channel_id, channel_id.c_str(), sizeof(handshake.channel_id) - 1);
    handshake.channel_id[sizeof(handshake.channel_id) - 1] = '\0';

    try {
        // Open the listener's accept ring
        std::string accept_ring_name = make_shm_name(listener_name, "accept");
        auto accept_ring = LockFreeRingBuffer::open(accept_ring_name);

        // Send connection request
        if (!accept_ring->try_write(&handshake, sizeof(handshake))) {
            throw std::runtime_error("Failed to send connection request to listener (ring buffer full)");
        }

        if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
            std::cout << "Sent connection request, waiting for server to create ring buffers..." << std::endl;
        }

        // Poll for ring buffer existence (wait for server to create them)
        std::unique_ptr<SharedMemoryChannel> channel;
        auto start = std::chrono::steady_clock::now();
        
        while (!channel) {
            try {
                channel = std::make_unique<SharedMemoryChannel>(
                    ioc,
                    channel_id,
                    /*is_server=*/false,
                    /*create_rings=*/false);
                break;
            } catch (const std::exception& e) {
                // Ring buffers don't exist yet, wait and retry
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                
                auto elapsed = std::chrono::steady_clock::now() - start;
                if (elapsed > std::chrono::seconds(5)) {
                    throw std::runtime_error("Timeout waiting for server to create ring buffers");
                }
            }
        }

        if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
            std::cout << "Connected to listener with dedicated channel: " << channel_id << std::endl;
        }

        return channel;

    } catch (const std::exception& e) {
        std::cerr << "Failed to connect to listener: " << e.what() << std::endl;
        throw std::runtime_error(std::string("Connection failed: ") + e.what());
    }
}

} // namespace nprpc::impl

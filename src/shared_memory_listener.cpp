#include <nprpc/impl/shared_memory_listener.hpp>
#include <nprpc/impl/shared_memory_channel.hpp>
#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/common.hpp>
#include <iostream>

namespace bip = boost::interprocess;

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

    // Create well-known accept queue
    // Remove any existing queue from crashed server
    bip::message_queue::remove(listener_name_.c_str());

    try {
        accept_queue_ = std::make_unique<bip::message_queue>(
            bip::create_only,
            listener_name_.c_str(),
            10,  // Max 10 pending connections
            sizeof(SharedMemoryHandshake));

        if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
            std::cout << "SharedMemoryListener created: " << listener_name_ << std::endl;
        }
    } catch (const bip::interprocess_exception& e) {
        std::cerr << "Failed to create listener queue: " << e.what() << std::endl;
        throw std::runtime_error(std::string("SharedMemoryListener creation failed: ") + e.what());
    }
}

SharedMemoryListener::~SharedMemoryListener() {
    stop();

    // Clean up accept queue
    accept_queue_.reset();
    
    try {
        bip::message_queue::remove(listener_name_.c_str());
        
        if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
            std::cout << "SharedMemoryListener cleaned up: " << listener_name_ << std::endl;
        }
    } catch (const bip::interprocess_exception& e) {
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
    SharedMemoryHandshake handshake;
    unsigned int priority;
    bip::message_queue::size_type recv_size;

    while (running_) {
        try {
            // Wait for connection request with timeout
            auto timeout = boost::posix_time::microsec_clock::universal_time() + 
                           boost::posix_time::milliseconds(100);

            if (accept_queue_->timed_receive(&handshake, sizeof(handshake), 
                                            recv_size, priority, timeout)) {
                // Validate handshake
                if (recv_size != sizeof(SharedMemoryHandshake)) {
                    std::cerr << "SharedMemoryListener: Invalid handshake size: " 
                              << recv_size << std::endl;
                    continue;
                }

                if (!handshake.is_valid()) {
                    std::cerr << "SharedMemoryListener: Invalid handshake magic/version" << std::endl;
                    continue;
                }

                // Handle the connection request
                handle_connection_request(handshake);
            }
        } catch (const bip::interprocess_exception& e) {
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
        // Create dedicated channel for this client (server creates the queues)
        auto channel = std::make_unique<SharedMemoryChannel>(
            ioc_, 
            channel_id, 
            /*is_server=*/true, 
            /*create_queues=*/true);

        if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
            std::cout << "SharedMemoryListener: Channel created successfully: " << channel_id << std::endl;
        }

        // Send handshake confirmation back to client via their dedicated channel
        // This signals the client that queues are ready
        if (!channel->send(&handshake, sizeof(handshake))) {
            std::cerr << "SharedMemoryListener: Failed to send handshake confirmation" << std::endl;
            return;
        }

        // Post the new connection to the io_context
        boost::asio::post(ioc_, [this, channel = std::move(channel)]() mutable {
            if (accept_handler_) {
                accept_handler_(std::move(channel));
            }
        });

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
        // Open the listener's accept queue
        bip::message_queue listener_queue(bip::open_only, listener_name.c_str());

        // Send connection request
        auto timeout = boost::posix_time::microsec_clock::universal_time() + 
                       boost::posix_time::seconds(5);  // 5 second timeout for handshake
        
        if (!listener_queue.timed_send(&handshake, sizeof(handshake), 0, timeout)) {
            throw std::runtime_error("Timeout sending connection request to listener");
        }

        if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
            std::cout << "Sent connection request, waiting for server to create queues..." << std::endl;
        }

        // Poll for queue existence (wait for server to create them)
        std::unique_ptr<SharedMemoryChannel> channel;
        auto start = std::chrono::steady_clock::now();
        
        while (!channel) {
            try {
                channel = std::make_unique<SharedMemoryChannel>(
                    ioc,
                    channel_id,
                    /*is_server=*/false,
                    /*create_queues=*/false);
                break;
            } catch (const std::exception& e) {
                // Queues don't exist yet, wait and retry
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                
                auto elapsed = std::chrono::steady_clock::now() - start;
                if (elapsed > std::chrono::seconds(5)) {
                    throw std::runtime_error("Timeout waiting for server to create queues");
                }
            }
        }

        if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
            std::cout << "Connected to listener with dedicated channel: " << channel_id << std::endl;
        }

        return channel;

    } catch (const bip::interprocess_exception& e) {
        std::cerr << "Failed to connect to listener: " << e.what() << std::endl;
        throw std::runtime_error(std::string("Connection failed: ") + e.what());
    }
}

} // namespace nprpc::impl

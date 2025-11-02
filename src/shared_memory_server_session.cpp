// Copyright (c) 2021 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/impl/shared_memory_channel.hpp>
#include <nprpc/impl/session.hpp>
#include <nprpc/common.hpp>
#include <iostream>
#include <memory>

namespace nprpc::impl {

/**
 * @brief Server-side shared memory session
 * 
 * Similar to Session_Socket, but for shared memory transport.
 * Handles incoming RPC requests from a client via SharedMemoryChannel.
 */
class SharedMemoryServerSession
    : public Session
    , public std::enable_shared_from_this<SharedMemoryServerSession>
{
    std::unique_ptr<SharedMemoryChannel> channel_;

public:
    // Server sessions don't initiate calls, so these should never be called
    virtual void timeout_action() final {
        // Server sessions don't have timeouts
    }

    virtual void send_receive(flat_buffer&, uint32_t) override {
        // Server sessions don't make outbound calls
        assert(false && "send_receive should not be called on server session");
    }

    virtual void send_receive_async(
        flat_buffer&&,
        std::optional<std::function<void(const boost::system::error_code&, flat_buffer&)>>&&,
        uint32_t) override
    {
        // Server sessions don't make outbound calls
        assert(false && "send_receive_async should not be called on server session");
    }

    /**
     * @brief Handle incoming request message
     * 
     * Called by SharedMemoryChannel when a complete message is received.
     * Processes the RPC request and sends response back.
     */
    void on_message_received(std::vector<char>&& data) {
        try {
            // Move data into rx_buffer (zero-copy)
            rx_buffer_().consume(rx_buffer_().size());
            auto mb = rx_buffer_().prepare(data.size());
            std::memcpy(mb.data(), data.data(), data.size());
            rx_buffer_().commit(data.size());

            // Dispatch the RPC request (calls servant methods)
            handle_request();

            // Send response back through the channel
            auto response_data = rx_buffer_().cdata();
            if (!channel_->send(response_data.data(), static_cast<uint32_t>(response_data.size()))) {
                std::cerr << "SharedMemoryServerSession: Failed to send response" << std::endl;
            }

        } catch (const std::exception& e) {
            std::cerr << "SharedMemoryServerSession: Error processing message: " << e.what() << std::endl;
        }
    }

    SharedMemoryServerSession(
        boost::asio::io_context& ioc,
        std::unique_ptr<SharedMemoryChannel> channel)
        : Session(ioc.get_executor())
        , channel_(std::move(channel))
    {
        // Set the endpoint for this session (used for tethered objects)
        // Server sessions get a "tethered" shared memory endpoint
        ctx_.remote_endpoint = EndPoint(
            EndPointType::SharedMemory,  // Will need to add TetheredSharedMemory if needed
            channel_->channel_id(),
            0);  // Port not used for shared memory

        // Note: We can't call shared_from_this() in constructor
        // The handler will be set up after construction

        if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
            std::cout << "SharedMemoryServerSession created for channel: " 
                      << channel_->channel_id() << std::endl;
        }
    }

    /**
     * @brief Initialize the session (must be called after construction)
     * 
     * This sets up the data received handler. Must be called after the
     * shared_ptr is constructed since we use shared_from_this().
     */
    void start() {
        // Set up the channel to call our handler when data arrives
        channel_->on_data_received = [this, self = shared_from_this()](std::vector<char>&& data) {
            on_message_received(std::move(data));
        };
    }

    ~SharedMemoryServerSession() {
        if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
            std::cout << "SharedMemoryServerSession destroyed for channel: "
                      << channel_->channel_id() << std::endl;
        }
    }
};

/**
 * @brief Create a server session for an accepted shared memory connection
 * 
 * This is called by the listener's accept handler.
 */
std::shared_ptr<Session> create_shared_memory_server_session(
    boost::asio::io_context& ioc,
    std::unique_ptr<SharedMemoryChannel> channel)
{
    auto session = std::make_shared<SharedMemoryServerSession>(ioc, std::move(channel));
    session->start();  // Initialize the handler after shared_ptr is created
    return session;
}

} // namespace nprpc::impl

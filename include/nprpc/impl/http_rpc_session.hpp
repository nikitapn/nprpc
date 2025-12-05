// Copyright (c) 2021-2025 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

#pragma once

#include <nprpc/impl/session.hpp>
#include <nprpc/common.hpp>
#include <boost/asio.hpp>
#include <memory>

namespace nprpc::impl {

/**
 * @brief HTTP RPC session for handling one-shot RPC calls over HTTP
 * 
 * Unlike WebSocket sessions, HTTP sessions are stateless:
 * - Each HTTP request creates a temporary session
 * - RPC call is processed
 * - Response is returned
 * - Session is destroyed
 * 
 * This is ideal for:
 * - Simple data fetching (GetData, GetList, etc.)
 * - Guest/anonymous users
 * - Initial page loads
 * - Fire-and-forget calls (analytics, logging)
 * 
 * For subscriptions and live updates, WebSocket is still preferred.
 */
class HttpRpcSession
    : public Session
    , public std::enable_shared_from_this<HttpRpcSession>
{
public:
    HttpRpcSession(boost::asio::io_context& ioc)
        : Session(ioc.get_executor())
    {
        // HTTP sessions are "tethered" (ephemeral) - use TcpTethered type
        ctx_.remote_endpoint = EndPoint(
            EndPointType::TcpTethered,
            "",
            0);
    }

    /**
     * @brief Process an RPC request from HTTP POST body
     * 
     * @param request_data Binary NPRPC request data from HTTP body
     * @param response_data Output parameter for response data
     * @return true if processed successfully, false on error
     */
    bool process_rpc_request(const std::string& request_data, std::string& response_data) {
        try {
            // FIXME: Avoid copy
            // Clear previous buffer
            rx_buffer_().consume(rx_buffer_().size());

            // Copy request data into rx_buffer
            auto mb = rx_buffer_().prepare(request_data.size());
            std::memcpy(mb.data(), request_data.data(), request_data.size());
            rx_buffer_().commit(request_data.size());

            // Dispatch the RPC call
            handle_request();

            // Extract response
            auto response_span = rx_buffer_().cdata();
            response_data.assign(
                static_cast<const char*>(response_span.data()),
                response_span.size());

            return true;

        } catch (const std::exception& e) {
            std::cerr << "HttpRpcSession: Error processing request: " << e.what() << std::endl;
            return false;
        }
    }

    // HTTP sessions don't support async operations - these should never be called
    virtual void timeout_action() final {
        // No-op for HTTP sessions
    }

    virtual void send_receive(flat_buffer&, uint32_t) override {
        // HTTP sessions don't make outbound calls
        assert(false && "send_receive not supported on HTTP sessions");
    }

    virtual void send_receive_async(
        flat_buffer&&,
        std::optional<std::function<void(const boost::system::error_code&, flat_buffer&)>>&&,
        uint32_t) override
    {
        // HTTP sessions don't make outbound calls
        assert(false && "send_receive_async not supported on HTTP sessions");
    }

    ~HttpRpcSession() = default;
};

/**
 * @brief Factory function to create and process an HTTP RPC request
 * 
 * @param ioc IO context
 * @param request_body HTTP POST body containing NPRPC request
 * @param response_body Output for NPRPC response
 * @return true if successful, false on error
 */
inline bool process_http_rpc(
    boost::asio::io_context& ioc,
    const std::string& request_body,
    std::string& response_body)
{
    auto session = std::make_shared<HttpRpcSession>(ioc);
    return session->process_rpc_request(request_body, response_body);
}

} // namespace nprpc::impl

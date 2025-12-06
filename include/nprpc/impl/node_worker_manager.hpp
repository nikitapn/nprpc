// Copyright (c) 2025 nikitapnn1@gmail.com
// SPDX-License-Identifier: MIT

#pragma once

#ifdef NPRPC_SSR_ENABLED

#include <nprpc/export.hpp>
#include <nprpc/impl/shared_memory_channel.hpp>

#include <boost/asio.hpp>
#include <boost/process.hpp>
#include <boost/process/v2/environment.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <memory>
#include <string>
#include <functional>
#include <atomic>
#include <map>
#include <mutex>
#include <condition_variable>

namespace nprpc::impl {

/**
 * @brief Manages a Node.js worker process for SSR via shared memory
 * 
 * This class:
 * 1. Spawns a Node.js process running the SvelteKit handler
 * 2. Creates shared memory channels for bidirectional communication
 * 3. Forwards HTTP requests to Node.js and receives responses
 * 
 * Usage:
 *   NodeWorkerManager manager(ioc);
 *   manager.start("/path/to/sveltekit/build");
 *   
 *   // Forward an HTTP request
 *   auto response = co_await manager.forward_request(request);
 */
class NPRPC_API NodeWorkerManager {
public:
    /**
     * @brief SSR Request to be forwarded to Node.js
     */
    struct SsrRequest {
        uint64_t id;
        std::string method;
        std::string url;
        std::map<std::string, std::string> headers;
        std::string body;  // Raw body bytes
        std::string client_address;
    };

    /**
     * @brief SSR Response from Node.js
     */
    struct SsrResponse {
        uint64_t request_id;
        int status_code;
        std::map<std::string, std::string> headers;
        std::string body;  // Raw body bytes
    };

    /**
     * @brief Construct NodeWorkerManager
     * @param ioc Boost.Asio io_context for async operations
     */
    explicit NodeWorkerManager(boost::asio::io_context& ioc);
    
    ~NodeWorkerManager();

    // Non-copyable
    NodeWorkerManager(const NodeWorkerManager&) = delete;
    NodeWorkerManager& operator=(const NodeWorkerManager&) = delete;

    /**
     * @brief Start the Node.js worker process
     * @param handler_path Path to the SvelteKit build directory (containing index.js)
     * @param node_executable Path to Node.js executable (default: "node")
     * @return true if started successfully
     */
    bool start(const std::string& handler_path, 
               const std::string& node_executable = "node");

    /**
     * @brief Stop the Node.js worker process
     */
    void stop();

    /**
     * @brief Check if the worker is running and ready
     */
    bool is_ready() const;

    /**
     * @brief Get the shared memory channel ID
     */
    const std::string& channel_id() const { return channel_id_; }

    /**
     * @brief Forward an HTTP request to Node.js for SSR
     * 
     * This is a blocking call that waits for the response.
     * For async usage, use forward_request_async.
     * 
     * @param request The HTTP request to forward
     * @param timeout_ms Timeout in milliseconds (default: 30000)
     * @return Response from Node.js, or nullopt on timeout/error
     */
    std::optional<SsrResponse> forward_request(
        const SsrRequest& request,
        uint32_t timeout_ms = 30000);

    /**
     * @brief Async version of forward_request using callback
     * @param request The HTTP request to forward
     * @param callback Called with the response (or error)
     * @param timeout_ms Timeout in milliseconds
     */
    void forward_request_async(
        const SsrRequest& request,
        std::function<void(std::optional<SsrResponse>)> callback,
        uint32_t timeout_ms = 30000);

private:
    void handle_response(std::vector<char>&& data);
    void read_loop();
    
    static std::string generate_channel_id();
    static std::string base64_encode(const std::string& data);
    static std::string base64_decode(const std::string& data);

    boost::asio::io_context& ioc_;
    std::string channel_id_;
    std::unique_ptr<SharedMemoryChannel> channel_;
    
    // Node.js process (Boost.Process v2 API)
    std::unique_ptr<boost::process::process> node_process_;
    std::unique_ptr<boost::asio::readable_pipe> node_stdout_;
    std::unique_ptr<boost::asio::readable_pipe> node_stderr_;
    
    // Async read buffers
    std::array<char, 4096> stdout_buffer_;
    std::array<char, 4096> stderr_buffer_;
    
    // Request tracking
    std::atomic<uint64_t> next_request_id_{1};
    
    struct PendingRequest {
        std::optional<SsrResponse> response;
        std::function<void(std::optional<SsrResponse>)> callback;
        std::mutex mtx;
        std::condition_variable cv;
        bool completed = false;
        std::chrono::steady_clock::time_point deadline;
    };
    
    std::mutex pending_mtx_;
    std::map<uint64_t, std::shared_ptr<PendingRequest>> pending_requests_;
    
    std::atomic<bool> running_{false};
    
    void async_read_stdout();
    void async_read_stderr();
};

} // namespace nprpc::impl

#endif // NPRPC_SSR_ENABLED
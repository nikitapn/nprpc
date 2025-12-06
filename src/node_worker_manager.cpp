// Copyright (c) 2025 nikitapnn1@gmail.com
// SPDX-License-Identifier: MIT

#ifdef NPRPC_SSR_ENABLED

#include <nprpc/impl/node_worker_manager.hpp>
#include <nprpc/impl/nprpc_impl.hpp>

#include <glaze/glaze.hpp>

#include <iostream>
#include <fstream>
#include <filesystem>
#include <unordered_map>

namespace nprpc::impl {

namespace fs = std::filesystem;

// JSON structures for SSR protocol
struct SsrRequestJson {
    std::string type = "request";
    uint64_t id{};
    std::string method;
    std::string url;
    std::map<std::string, std::string> headers;
    std::optional<std::string> body;  // Base64 encoded
    std::string clientAddress;
};

struct SsrResponseJson {
    std::string type;
    uint64_t id{};
    int status{};
    std::map<std::string, std::string> headers;
    std::optional<std::string> body;  // Base64 encoded
};

NodeWorkerManager::NodeWorkerManager(boost::asio::io_context& ioc)
    : ioc_(ioc)
    , channel_id_(generate_channel_id())
{
}

NodeWorkerManager::~NodeWorkerManager() {
    stop();
}

std::string NodeWorkerManager::generate_channel_id() {
    return "nprpc-ssr-" + boost::uuids::to_string(boost::uuids::random_generator()());
}

bool NodeWorkerManager::start(const std::string& handler_path,
                              const std::string& node_executable) {
    if (running_) {
        std::cerr << "NodeWorkerManager: Already running" << std::endl;
        return false;
    }

    // Verify handler exists
    fs::path index_path = fs::path(handler_path) / "index.js";
    if (!fs::exists(index_path)) {
        std::cerr << "NodeWorkerManager: Handler not found: " << index_path << std::endl;
        return false;
    }

    try {
        // Create shared memory channel (server side - creates the rings)
        channel_ = std::make_unique<SharedMemoryChannel>(
            ioc_, channel_id_, true, true);
        
        if (!channel_->is_valid()) {
            std::cerr << "NodeWorkerManager: Failed to create shared memory channel" << std::endl;
            return false;
        }

        // Set up response handler
        channel_->on_data_received = [this](std::vector<char>&& data) {
            handle_response(std::move(data));
        };

        // Build environment for Node.js process (Boost.Process v2)
        namespace bpe = boost::process::environment;
        std::unordered_map<bpe::key, bpe::value> env =
        {
          {"NPRPC_CHANNEL_ID", channel_id_},
        };

        // Start Node.js process
        if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
            std::cout << "NodeWorkerManager: Starting Node.js worker" << std::endl;
            std::cout << "  Handler: " << index_path << std::endl;
            std::cout << "  Channel: " << channel_id_ << std::endl;
        }

        // Create pipes for stdout/stderr
        node_stdout_ = std::make_unique<boost::asio::readable_pipe>(ioc_);
        node_stderr_ = std::make_unique<boost::asio::readable_pipe>(ioc_);

        // Launch process with Boost.Process v2 API
        node_process_ = std::make_unique<boost::process::process>(
            ioc_,
            boost::process::filesystem::path(node_executable),
            std::vector<std::string>{index_path.string()},
            boost::process::process_environment(env),
            boost::process::process_stdio{{}, *node_stdout_, *node_stderr_}
        );

        running_ = true;

        // Start async reading from stdout/stderr
        async_read_stdout();
        async_read_stderr();

        // Give Node.js a moment to start up
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        if (!node_process_->running()) {
            std::cerr << "NodeWorkerManager: Node.js process exited prematurely" << std::endl;
            stop();
            return false;
        }

        if (g_cfg.debug_level >= DebugLevel::DebugLevel_Critical) {
            std::cout << "NodeWorkerManager: Node.js worker started (PID: " 
                      << node_process_->id() << ")" << std::endl;
        }

        return true;

    } catch (const std::exception& e) {
        std::cerr << "NodeWorkerManager: Failed to start: " << e.what() << std::endl;
        stop();
        return false;
    }
}

void NodeWorkerManager::stop() {
    running_ = false;

    // Terminate Node.js process
    if (node_process_ && node_process_->running()) {
        if (g_cfg.debug_level >= DebugLevel::DebugLevel_Critical) {
            std::cout << "NodeWorkerManager: Stopping Node.js worker" << std::endl;
        }
        
        node_process_->terminate();
        node_process_->wait();
    }
    node_process_.reset();

    // Close pipes
    if (node_stdout_ && node_stdout_->is_open()) {
        boost::system::error_code ec;
        node_stdout_->close(ec);
    }
    node_stdout_.reset();
    
    if (node_stderr_ && node_stderr_->is_open()) {
        boost::system::error_code ec;
        node_stderr_->close(ec);
    }
    node_stderr_.reset();

    // Clean up pending requests
    {
        std::lock_guard<std::mutex> lock(pending_mtx_);
        for (auto& [id, pending] : pending_requests_) {
            std::lock_guard<std::mutex> req_lock(pending->mtx);
            pending->completed = true;
            pending->cv.notify_all();
            if (pending->callback) {
                pending->callback(std::nullopt);
            }
        }
        pending_requests_.clear();
    }

    // Destroy channel
    channel_.reset();
}

void NodeWorkerManager::async_read_stdout() {
    if (!running_ || !node_stdout_ || !node_stdout_->is_open()) return;
    
    node_stdout_->async_read_some(
        boost::asio::buffer(stdout_buffer_),
        [this](const boost::system::error_code& ec, std::size_t bytes) {
            if (ec) {
                if (ec != boost::asio::error::operation_aborted) {
                    // End of stream or error
                }
                return;
            }
            
            if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
                std::cout << "[Node.js] " << std::string_view(stdout_buffer_.data(), bytes);
            }
            
            // Continue reading
            async_read_stdout();
        }
    );
}

void NodeWorkerManager::async_read_stderr() {
    if (!running_ || !node_stderr_ || !node_stderr_->is_open()) return;
    
    node_stderr_->async_read_some(
        boost::asio::buffer(stderr_buffer_),
        [this](const boost::system::error_code& ec, std::size_t bytes) {
            if (ec) {
                if (ec != boost::asio::error::operation_aborted) {
                    // End of stream or error
                }
                return;
            }
            
            std::cerr << "[Node.js ERROR] " << std::string_view(stderr_buffer_.data(), bytes);
            
            // Continue reading
            async_read_stderr();
        }
    );
}

bool NodeWorkerManager::is_ready() const {
    return running_ && 
           node_process_ && 
           node_process_->running() && 
           channel_ && 
           channel_->is_valid();
}

std::optional<NodeWorkerManager::SsrResponse> NodeWorkerManager::forward_request(
    const SsrRequest& request,
    uint32_t timeout_ms) 
{
    if (!is_ready()) {
        return std::nullopt;
    }

    auto pending = std::make_shared<PendingRequest>();
    pending->deadline = std::chrono::steady_clock::now() + 
                        std::chrono::milliseconds(timeout_ms);

    uint64_t request_id = next_request_id_++;

    // Build request JSON using glaze
    SsrRequestJson req;
    req.id = request_id;
    req.method = request.method;
    req.url = request.url;
    req.headers = request.headers;
    req.clientAddress = request.client_address;
    
    if (!request.body.empty()) {
        req.body = base64_encode(request.body);
    }

    // Register pending request
    {
        std::lock_guard<std::mutex> lock(pending_mtx_);
        pending_requests_[request_id] = pending;
    }

    // Send request
    std::string request_str = glz::write_json(req).value_or("");
    if (request_str.empty() || !channel_->send(request_str.data(), static_cast<uint32_t>(request_str.size()))) {
        std::lock_guard<std::mutex> lock(pending_mtx_);
        pending_requests_.erase(request_id);
        return std::nullopt;
    }

    // Wait for response
    {
        std::unique_lock<std::mutex> lock(pending->mtx);
        if (!pending->cv.wait_until(lock, pending->deadline, 
                [&] { return pending->completed; })) {
            // Timeout
            std::lock_guard<std::mutex> plock(pending_mtx_);
            pending_requests_.erase(request_id);
            return std::nullopt;
        }
    }

    return pending->response;
}

void NodeWorkerManager::forward_request_async(
    const SsrRequest& request,
    std::function<void(std::optional<SsrResponse>)> callback,
    uint32_t timeout_ms)
{
    if (!is_ready()) {
        if (callback) callback(std::nullopt);
        return;
    }

    auto pending = std::make_shared<PendingRequest>();
    pending->callback = callback;
    pending->deadline = std::chrono::steady_clock::now() + 
                        std::chrono::milliseconds(timeout_ms);

    uint64_t request_id = next_request_id_++;

    // Build request JSON using glaze
    SsrRequestJson req;
    req.id = request_id;
    req.method = request.method;
    req.url = request.url;
    req.headers = request.headers;
    req.clientAddress = request.client_address;
    
    if (!request.body.empty()) {
        req.body = base64_encode(request.body);
    }

    // Register pending request
    {
        std::lock_guard<std::mutex> lock(pending_mtx_);
        pending_requests_[request_id] = pending;
    }

    // Send request
    std::string request_str = glz::write_json(req).value_or("");
    if (request_str.empty() || !channel_->send(request_str.data(), static_cast<uint32_t>(request_str.size()))) {
        std::lock_guard<std::mutex> lock(pending_mtx_);
        pending_requests_.erase(request_id);
        if (callback) callback(std::nullopt);
        return;
    }

    // Set up timeout timer
    auto timer = std::make_shared<boost::asio::steady_timer>(ioc_);
    timer->expires_at(pending->deadline);
    timer->async_wait([this, request_id, pending, timer](const boost::system::error_code& ec) {
        if (ec) return;  // Timer cancelled
        
        std::lock_guard<std::mutex> lock(pending_mtx_);
        auto it = pending_requests_.find(request_id);
        if (it != pending_requests_.end()) {
            pending_requests_.erase(it);
            if (pending->callback) {
                pending->callback(std::nullopt);
            }
        }
    });
}

void NodeWorkerManager::handle_response(std::vector<char>&& data) {
    try {
        // Parse response JSON using glaze
        SsrResponseJson resp;
        auto ec = glz::read_json(resp, std::string_view(data.data(), data.size()));
        
        if (ec) {
            std::cerr << "NodeWorkerManager: Failed to parse response JSON: " 
                      << glz::format_error(ec, std::string_view(data.data(), data.size())) << std::endl;
            return;
        }
        
        if (resp.type != "response") {
            std::cerr << "NodeWorkerManager: Unknown message type: " 
                      << resp.type << std::endl;
            return;
        }

        uint64_t request_id = resp.id;
        
        std::shared_ptr<PendingRequest> pending;
        {
            std::lock_guard<std::mutex> lock(pending_mtx_);
            auto it = pending_requests_.find(request_id);
            if (it == pending_requests_.end()) {
                std::cerr << "NodeWorkerManager: Response for unknown request: " 
                          << request_id << std::endl;
                return;
            }
            pending = it->second;
            pending_requests_.erase(it);
        }

        // Build response
        SsrResponse ssr_response;
        ssr_response.request_id = request_id;
        ssr_response.status_code = resp.status;
        ssr_response.headers = std::move(resp.headers);
        
        if (resp.body) {
            ssr_response.body = base64_decode(*resp.body);
        }

        // Complete the request
        {
            std::lock_guard<std::mutex> lock(pending->mtx);
            pending->response = std::move(ssr_response);
            pending->completed = true;
        }
        pending->cv.notify_all();
        
        if (pending->callback) {
            pending->callback(pending->response);
        }

    } catch (const std::exception& e) {
        std::cerr << "NodeWorkerManager: Error parsing response: " << e.what() << std::endl;
    }
}

std::string NodeWorkerManager::base64_encode(const std::string& data) {
    static const char* chars = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    
    std::string result;
    result.reserve((data.size() + 2) / 3 * 4);
    
    for (size_t i = 0; i < data.size(); i += 3) {
        uint32_t n = static_cast<uint8_t>(data[i]) << 16;
        if (i + 1 < data.size()) n |= static_cast<uint8_t>(data[i + 1]) << 8;
        if (i + 2 < data.size()) n |= static_cast<uint8_t>(data[i + 2]);
        
        result += chars[(n >> 18) & 63];
        result += chars[(n >> 12) & 63];
        result += (i + 1 < data.size()) ? chars[(n >> 6) & 63] : '=';
        result += (i + 2 < data.size()) ? chars[n & 63] : '=';
    }
    
    return result;
}

std::string NodeWorkerManager::base64_decode(const std::string& data) {
    static const int lookup[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
    };
    
    std::string result;
    result.reserve(data.size() * 3 / 4);
    
    uint32_t n = 0;
    int bits = 0;
    
    for (char c : data) {
        if (c == '=') break;
        int v = lookup[static_cast<uint8_t>(c)];
        if (v < 0) continue;
        n = (n << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            result += static_cast<char>((n >> bits) & 0xFF);
        }
    }
    
    return result;
}

} // namespace nprpc::impl

#endif // NPRPC_SSR_ENABLED
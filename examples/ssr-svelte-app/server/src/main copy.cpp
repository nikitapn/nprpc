// NPRPC SSR Server Example
// This server handles HTTP requests and forwards SSR requests to SvelteKit via shared memory

#include <nprpc/nprpc.hpp>
#include <nprpc/impl/shared_memory_channel.hpp>
#include <nprpc/impl/http_utils.hpp>

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <nlohmann/json.hpp>

#include <iostream>
#include <fstream>
#include <filesystem>
#include <atomic>
#include <chrono>
#include <thread>
#include <map>
#include <mutex>
#include <condition_variable>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;
using json = nlohmann::json;
namespace fs = std::filesystem;

// Configuration
static const std::string CHANNEL_ID = "svelte-ssr-demo";
static const std::string CLIENT_DIR = "../client/build/client";
static const std::string PRERENDERED_DIR = "../client/build/prerendered";
static const uint16_t HTTP_PORT = 3000;

class SsrBridge {
public:
    SsrBridge(net::io_context& ioc, const std::string& channel_id)
        : ioc_(ioc)
        , channel_(ioc, channel_id, true, true)  // server, create rings
        , next_request_id_(1)
    {
        // Set up callback for responses from SvelteKit
        channel_.on_data_received = [this](std::vector<char>&& data) {
            handle_response(std::move(data));
        };
        
        std::cout << "SSR Bridge initialized with channel: " << channel_id << std::endl;
    }

    ~SsrBridge() = default;

    struct SsrResponse {
        int status = 0;
        std::map<std::string, std::string> headers;
        std::string body;
        bool ready = false;
        std::condition_variable cv;
        std::mutex mtx;
    };

    // Send SSR request and wait for response
    std::shared_ptr<SsrResponse> send_ssr_request(
        const std::string& method,
        const std::string& url,
        const std::map<std::string, std::string>& headers,
        const std::string& body,
        const std::string& client_address
    ) {
        uint64_t request_id = next_request_id_++;
        
        // Build request JSON
        json req;
        req["type"] = "request";
        req["id"] = request_id;
        req["method"] = method;
        req["url"] = url;
        req["headers"] = headers;
        req["clientAddress"] = client_address;
        
        if (!body.empty()) {
            // Base64 encode body
            req["body"] = base64_encode(body);
        }

        auto response = std::make_shared<SsrResponse>();
        
        {
            std::lock_guard<std::mutex> lock(pending_mtx_);
            pending_responses_[request_id] = response;
        }

        // Send request
        std::string request_str = req.dump();
        if (!channel_.send(request_str.data(), static_cast<uint32_t>(request_str.size()))) {
            std::cerr << "Failed to send SSR request" << std::endl;
            std::lock_guard<std::mutex> lock(pending_mtx_);
            pending_responses_.erase(request_id);
            return nullptr;
        }

        return response;
    }

    bool is_valid() const {
        return channel_.is_valid();
    }

private:
    void handle_response(std::vector<char>&& data) {
        try {
            json resp = json::parse(data.begin(), data.end());
            
            if (resp["type"] != "response") {
                std::cerr << "Unknown response type: " << resp["type"] << std::endl;
                return;
            }

            uint64_t request_id = resp["id"];
            
            std::shared_ptr<SsrResponse> response;
            {
                std::lock_guard<std::mutex> lock(pending_mtx_);
                auto it = pending_responses_.find(request_id);
                if (it == pending_responses_.end()) {
                    std::cerr << "Response for unknown request ID: " << request_id << std::endl;
                    return;
                }
                response = it->second;
                pending_responses_.erase(it);
            }

            // Fill in response
            response->status = resp["status"];
            
            if (resp.contains("headers")) {
                for (auto& [key, value] : resp["headers"].items()) {
                    response->headers[key] = value;
                }
            }
            
            if (resp.contains("body")) {
                response->body = base64_decode(resp["body"]);
            }

            // Signal completion
            {
                std::lock_guard<std::mutex> lock(response->mtx);
                response->ready = true;
            }
            response->cv.notify_one();

        } catch (const std::exception& e) {
            std::cerr << "Error parsing SSR response: " << e.what() << std::endl;
        }
    }

    static std::string base64_encode(const std::string& data) {
        static const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
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

    static std::string base64_decode(const std::string& data) {
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

    net::io_context& ioc_;
    nprpc::impl::SharedMemoryChannel channel_;
    std::atomic<uint64_t> next_request_id_;
    
    std::mutex pending_mtx_;
    std::map<uint64_t, std::shared_ptr<SsrResponse>> pending_responses_;
};

// HTTP Session handler
class HttpSession : public std::enable_shared_from_this<HttpSession> {
public:
    HttpSession(tcp::socket socket, std::shared_ptr<SsrBridge> ssr_bridge,
                const std::string& client_dir, const std::string& prerendered_dir)
        : socket_(std::move(socket))
        , ssr_bridge_(ssr_bridge)
        , client_dir_(client_dir)
        , prerendered_dir_(prerendered_dir)
    {}

    void start() {
        do_read();
    }

private:
    void do_read() {
        auto self = shared_from_this();
        
        http::async_read(socket_, buffer_, request_,
            [self](beast::error_code ec, std::size_t) {
                if (!ec) {
                    self->handle_request();
                }
            });
    }

    void handle_request() {
        std::string target(request_.target());
        std::string method(request_.method_string());
        
        // Try to serve static file first
        if (method == "GET") {
            // Check client directory (static assets)
            std::string client_path = client_dir_ + target;
            if (target == "/") client_path = client_dir_ + "/index.html";
            
            if (fs::exists(client_path) && fs::is_regular_file(client_path)) {
                serve_static_file(client_path);
                return;
            }

            // Check prerendered directory
            std::string prerendered_path = prerendered_dir_ + target;
            if (!prerendered_path.ends_with(".html")) {
                if (fs::exists(prerendered_path + ".html")) {
                    prerendered_path += ".html";
                } else if (fs::exists(prerendered_path + "/index.html")) {
                    prerendered_path += "/index.html";
                }
            }
            
            if (fs::exists(prerendered_path) && fs::is_regular_file(prerendered_path)) {
                serve_static_file(prerendered_path);
                return;
            }
        }

        // Forward to SSR
        forward_to_ssr();
    }

    void serve_static_file(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            send_error(404, "Not Found");
            return;
        }

        std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
        
        auto mime = nprpc::impl::mime_type(path);
        
        http::response<http::string_body> response{http::status::ok, request_.version()};
        response.set(http::field::server, "NPRPC-SSR");
        response.set(http::field::content_type, mime);
        response.keep_alive(request_.keep_alive());
        response.body() = std::move(content);
        response.prepare_payload();
        
        send_response(std::move(response));
    }

    void forward_to_ssr() {
        if (!ssr_bridge_ || !ssr_bridge_->is_valid()) {
            send_error(503, "SSR not available");
            return;
        }

        // Build URL
        std::string url = "http://localhost:" + std::to_string(HTTP_PORT) + std::string(request_.target());
        
        // Extract headers
        std::map<std::string, std::string> headers;
        for (auto& field : request_) {
            headers[std::string(field.name_string())] = std::string(field.value());
        }
        
        // Get body
        std::string body;
        if (request_.body().size() > 0) {
            body = request_.body();
        }

        // Get client address
        std::string client_address;
        try {
            client_address = socket_.remote_endpoint().address().to_string();
        } catch (...) {
            client_address = "127.0.0.1";
        }

        // Send SSR request
        auto ssr_response = ssr_bridge_->send_ssr_request(
            std::string(request_.method_string()),
            url,
            headers,
            body,
            client_address
        );

        if (!ssr_response) {
            send_error(500, "Failed to send SSR request");
            return;
        }

        // Wait for response (with timeout)
        {
            std::unique_lock<std::mutex> lock(ssr_response->mtx);
            if (!ssr_response->cv.wait_for(lock, std::chrono::seconds(30),
                    [&] { return ssr_response->ready; })) {
                send_error(504, "SSR timeout");
                return;
            }
        }

        // Build HTTP response
        http::response<http::string_body> response{
            static_cast<http::status>(ssr_response->status),
            request_.version()
        };
        
        response.set(http::field::server, "NPRPC-SSR");
        for (auto& [key, value] : ssr_response->headers) {
            response.set(key, value);
        }
        response.keep_alive(request_.keep_alive());
        response.body() = std::move(ssr_response->body);
        response.prepare_payload();
        
        send_response(std::move(response));
    }

    void send_error(int status, const std::string& message) {
        http::response<http::string_body> response{
            static_cast<http::status>(status),
            request_.version()
        };
        response.set(http::field::server, "NPRPC-SSR");
        response.set(http::field::content_type, "text/plain");
        response.keep_alive(request_.keep_alive());
        response.body() = message;
        response.prepare_payload();
        
        send_response(std::move(response));
    }

    void send_response(http::response<http::string_body>&& response) {
        auto self = shared_from_this();
        auto sp = std::make_shared<http::response<http::string_body>>(std::move(response));
        
        http::async_write(socket_, *sp,
            [self, sp](beast::error_code ec, std::size_t) {
                self->socket_.shutdown(tcp::socket::shutdown_send, ec);
            });
    }

    tcp::socket socket_;
    beast::flat_buffer buffer_;
    http::request<http::string_body> request_;
    std::shared_ptr<SsrBridge> ssr_bridge_;
    std::string client_dir_;
    std::string prerendered_dir_;
};

// HTTP Server
class HttpServer {
public:
    HttpServer(net::io_context& ioc, uint16_t port,
               std::shared_ptr<SsrBridge> ssr_bridge,
               const std::string& client_dir,
               const std::string& prerendered_dir)
        : ioc_(ioc)
        , acceptor_(ioc, tcp::endpoint(tcp::v4(), port))
        , ssr_bridge_(ssr_bridge)
        , client_dir_(client_dir)
        , prerendered_dir_(prerendered_dir)
    {
        do_accept();
        std::cout << "HTTP server listening on port " << port << std::endl;
    }

private:
    void do_accept() {
        acceptor_.async_accept(
            [this](beast::error_code ec, tcp::socket socket) {
                if (!ec) {
                    std::make_shared<HttpSession>(
                        std::move(socket),
                        ssr_bridge_,
                        client_dir_,
                        prerendered_dir_
                    )->start();
                }
                do_accept();
            });
    }

    net::io_context& ioc_;
    tcp::acceptor acceptor_;
    std::shared_ptr<SsrBridge> ssr_bridge_;
    std::string client_dir_;
    std::string prerendered_dir_;
};

int main(int argc, char* argv[]) {
    try {
        net::io_context ioc;

        // Create SSR bridge (shared memory channel)
        auto ssr_bridge = std::make_shared<SsrBridge>(ioc, CHANNEL_ID);
        
        if (!ssr_bridge->is_valid()) {
            std::cerr << "Failed to create SSR bridge" << std::endl;
            return 1;
        }

        // Start HTTP server
        HttpServer server(ioc, HTTP_PORT, ssr_bridge, CLIENT_DIR, PRERENDERED_DIR);

        std::cout << "NPRPC SSR Server started" << std::endl;
        std::cout << "Channel: " << CHANNEL_ID << std::endl;
        std::cout << "HTTP: http://localhost:" << HTTP_PORT << std::endl;
        std::cout << "\nPlease start the SvelteKit handler:" << std::endl;
        std::cout << "  cd ../client/build && node index.js" << std::endl;

        ioc.run();

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

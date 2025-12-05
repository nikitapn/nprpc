// Copyright (c) 2025 nikitapnn1@gmail.com
// SPDX-License-Identifier: MIT

#include <nprpc/impl/http3_server.hpp>

// This file implements HTTP/3 using msh3 (MsQuic) backend
#if defined(NPRPC_HTTP3_ENABLED) && defined(NPRPC_HTTP3_BACKEND_MSH3)

#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/impl/http_rpc_session.hpp>
#include <nprpc/common.hpp>

#include <msh3.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <vector>
#include <cstring>
#include <format>

#include "debug.hpp"

namespace nprpc::impl {

// Forward declaration - implemented in http_server.cpp
extern beast::string_view mime_type(beast::string_view path);
extern std::string path_cat(beast::string_view base, beast::string_view path);

//==============================================================================
// Http3Request - Tracks state for a single HTTP/3 request
//==============================================================================

struct Http3Request {
    MSH3_REQUEST* handle = nullptr;
    std::string method;
    std::string path;
    std::string content_type;
    uint32_t content_length = 0;
    std::vector<uint8_t> body;
    bool headers_complete = false;
    bool body_complete = false;
    bool processed = false;  // Track if request has been processed
    
    // Response data - must be kept alive until SEND_COMPLETE
    std::string response_body;
    std::string response_status;
    std::string response_content_type;
    std::string response_content_length;
    
    void reset() {
        method.clear();
        path.clear();
        content_type.clear();
        content_length = 0;
        body.clear();
        headers_complete = false;
        body_complete = false;
        processed = false;
    }
};

//==============================================================================
// Http3Server - HTTP/3 server using msh3
//==============================================================================

class Http3Server {
public:
    Http3Server(boost::asio::io_context& ioc, 
                const std::string& cert_file,
                const std::string& key_file,
                uint16_t port);
    ~Http3Server();
    
    bool start();
    void stop();
    
private:
    // msh3 callbacks
    static MSH3_STATUS MSH3_CALL listener_callback(
        MSH3_LISTENER* listener,
        void* context,
        MSH3_LISTENER_EVENT* event);
        
    static MSH3_STATUS MSH3_CALL connection_callback(
        MSH3_CONNECTION* connection,
        void* context,
        MSH3_CONNECTION_EVENT* event);
        
    static MSH3_STATUS MSH3_CALL request_callback(
        MSH3_REQUEST* request,
        void* context,
        MSH3_REQUEST_EVENT* event);
    
    // Request handling
    void handle_new_connection(MSH3_CONNECTION* connection, const char* server_name);
    void handle_new_request(MSH3_REQUEST* request);
    void handle_header(Http3Request* req, const MSH3_HEADER* header);
    void handle_data(Http3Request* req, const uint8_t* data, uint32_t length);
    void handle_request_complete(Http3Request* req);
    
    // Response helpers
    void send_response(Http3Request* req, int status_code, 
                      const std::string& content_type,
                      const std::string& body);
    void send_file_response(Http3Request* req, const std::string& path);
    void send_error(Http3Request* req, int status_code, const std::string& message);
    void send_cors_preflight(Http3Request* req);
    
    // RPC handling
    void handle_rpc_request(Http3Request* req);
    
    boost::asio::io_context& ioc_;
    std::string cert_file_;
    std::string key_file_;
    uint16_t port_;
    
    MSH3_API* api_ = nullptr;
    MSH3_CONFIGURATION* config_ = nullptr;
    MSH3_LISTENER* listener_ = nullptr;
    
    std::mutex mutex_;
    std::unordered_map<MSH3_REQUEST*, std::unique_ptr<Http3Request>> requests_;
};

//==============================================================================
// Implementation
//==============================================================================

Http3Server::Http3Server(
    boost::asio::io_context& ioc,
    const std::string& cert_file,
    const std::string& key_file,
    uint16_t port)
    : ioc_(ioc)
    , cert_file_(cert_file)
    , key_file_(key_file)
    , port_(port)
{
}

Http3Server::~Http3Server() {
    stop();
}

bool Http3Server::start() {
    // Initialize msh3 API
    api_ = MsH3ApiOpen();
    if (!api_) {
        std::cerr << "[HTTP/3] Failed to initialize msh3 API" << std::endl;
        return false;
    }
    
    // Configure settings
    MSH3_SETTINGS settings = {};
    settings.IsSet.IdleTimeoutMs = 1;
    settings.IdleTimeoutMs = 30000;  // 30 seconds
    settings.IsSet.PeerRequestCount = 1;
    settings.PeerRequestCount = 100;  // Allow up to 100 concurrent requests
    
    config_ = MsH3ConfigurationOpen(api_, &settings, sizeof(settings));
    if (!config_) {
        std::cerr << "[HTTP/3] Failed to create configuration" << std::endl;
        MsH3ApiClose(api_);
        api_ = nullptr;
        return false;
    }
    
    // Load certificate
    MSH3_CERTIFICATE_FILE cert_file = {
        .PrivateKeyFile = key_file_.c_str(),
        .CertificateFile = cert_file_.c_str()
    };
    
    MSH3_CREDENTIAL_CONFIG cred_config = {
        .Type = MSH3_CREDENTIAL_TYPE_CERTIFICATE_FILE,
        .Flags = MSH3_CREDENTIAL_FLAG_NONE,  // Server mode
        .CertificateFile = &cert_file
    };
    
    MSH3_STATUS status = MsH3ConfigurationLoadCredential(config_, &cred_config);
    if (MSH3_FAILED(status)) {
        std::cerr << "[HTTP/3] Failed to load credentials, status=0x" 
                  << std::hex << status << std::dec << std::endl;
        MsH3ConfigurationClose(config_);
        MsH3ApiClose(api_);
        config_ = nullptr;
        api_ = nullptr;
        return false;
    }
    
    MSH3_ADDR local_addr = {0};
    local_addr.Ipv4.sin_family = AF_INET;
    local_addr.Ipv4.sin_addr.s_addr = INADDR_ANY;
    MSH3_SET_PORT(&local_addr, port_);
    
    // Create listener
    listener_ = MsH3ListenerOpen(api_, &local_addr, listener_callback, this);
    if (!listener_) {
        std::cerr << "[HTTP/3] Failed to create listener" << std::endl;
        MsH3ConfigurationClose(config_);
        MsH3ApiClose(api_);
        config_ = nullptr;
        api_ = nullptr;
        return false;
    }
    
    if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
        std::cout << "[HTTP/3] Server listening on port " << port_ << std::endl;
    }
    
    return true;
}

void Http3Server::stop() {
    if (listener_) {
        MsH3ListenerClose(listener_);
        listener_ = nullptr;
    }
    
    // Clear any pending requests
    {
        std::lock_guard lock(mutex_);
        for (auto& [handle, req] : requests_) {
            MsH3RequestClose(handle);
        }
        requests_.clear();
    }
    
    if (config_) {
        MsH3ConfigurationClose(config_);
        config_ = nullptr;
    }
    
    if (api_) {
        MsH3ApiClose(api_);
        api_ = nullptr;
    }
}

//==============================================================================
// msh3 Callbacks
//==============================================================================

MSH3_STATUS MSH3_CALL Http3Server::listener_callback(
    MSH3_LISTENER* listener,
    void* context,
    MSH3_LISTENER_EVENT* event)
{
    auto* self = static_cast<Http3Server*>(context);

    switch (event->Type) {
    case MSH3_LISTENER_EVENT_NEW_CONNECTION:
        NPRPC_HTTP3_TRACE("Listener New connection");
        if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
            std::cout << "[HTTP/3] New connection" << std::endl;
        }
        self->handle_new_connection(
            event->NEW_CONNECTION.Connection,
            event->NEW_CONNECTION.ServerName);
        break;
        
    case MSH3_LISTENER_EVENT_SHUTDOWN_COMPLETE:
        NPRPC_HTTP3_TRACE("Listener shutdown complete");
        if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
            std::cout << "[HTTP/3] Listener shutdown complete" << std::endl;
        }
        break;
    }
    
    return MSH3_STATUS_SUCCESS;
}

MSH3_STATUS MSH3_CALL Http3Server::connection_callback(
    MSH3_CONNECTION* connection,
    void* context,
    MSH3_CONNECTION_EVENT* event)
{
    auto* self = static_cast<Http3Server*>(context);
    
    switch (event->Type) {
    case MSH3_CONNECTION_EVENT_NEW_REQUEST:
        self->handle_new_request(event->NEW_REQUEST.Request);
        break;
        
    case MSH3_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
        if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
            std::cout << "[HTTP/3] Connection shutdown by transport" << std::endl;
        }
        break;
        
    case MSH3_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
        if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
            std::cout << "[HTTP/3] Connection shutdown by peer" << std::endl;
        }
        break;
        
    case MSH3_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
        break;
    }
    
    return MSH3_STATUS_SUCCESS;
}

MSH3_STATUS MSH3_CALL Http3Server::request_callback(
    MSH3_REQUEST* request,
    void* context,
    MSH3_REQUEST_EVENT* event)
{
    auto* self = static_cast<Http3Server*>(context);
    
    Http3Request* req = nullptr;
    {
        std::lock_guard lock(self->mutex_);
        auto it = self->requests_.find(request);
        if (it != self->requests_.end()) {
            req = it->second.get();
        }
    }
    
    if (!req) {
        return MSH3_STATUS_SUCCESS;
    }
    
    switch (event->Type) {
    case MSH3_REQUEST_EVENT_HEADER_RECEIVED:
        self->handle_header(req, event->HEADER_RECEIVED.Header);
        break;
        
    case MSH3_REQUEST_EVENT_DATA_RECEIVED:
        // Skip if already processed
        if (req->processed) {
            if (event->DATA_RECEIVED.Length > 0 && event->DATA_RECEIVED.Length < 100*1024*1024) {
                MsH3RequestCompleteReceive(request, event->DATA_RECEIVED.Length);
            }
            break;
        }
        
        // Validate length - msh3 sometimes sends bogus large values
        if (event->DATA_RECEIVED.Length > 0 && event->DATA_RECEIVED.Length < 100*1024*1024) {
            self->handle_data(req, event->DATA_RECEIVED.Data, event->DATA_RECEIVED.Length);
            MsH3RequestCompleteReceive(request, event->DATA_RECEIVED.Length);
            
            // Check if we've received all expected data
            if (req->content_length > 0 && req->body.size() >= req->content_length) {
                req->body_complete = true;
                req->processed = true;
                self->handle_request_complete(req);
            }
        } else if (event->DATA_RECEIVED.Length == 0) {
            // Zero-length receive, just acknowledge
            MsH3RequestCompleteReceive(request, 0);
        } else if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
            std::cerr << "[HTTP/3] Ignoring bogus data length: " << event->DATA_RECEIVED.Length << std::endl;
        }
        break;
        
    case MSH3_REQUEST_EVENT_PEER_SEND_SHUTDOWN:
        // Client finished sending, process the request if not already done
        if (!req->processed) {
            req->body_complete = true;
            req->processed = true;
            self->handle_request_complete(req);
        }
        break;
        
    case MSH3_REQUEST_EVENT_SHUTDOWN_COMPLETE:
        // Clean up the request
        {
            std::lock_guard lock(self->mutex_);
            self->requests_.erase(request);
        }
        break;
        
    case MSH3_REQUEST_EVENT_SEND_COMPLETE:
    case MSH3_REQUEST_EVENT_SEND_SHUTDOWN_COMPLETE:
        // These are informational only
        break;
        
    default:
        if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
            std::cout << "[HTTP/3] Unknown request event type: " << (int)event->Type << std::endl;
        }
        break;
    }
    
    return MSH3_STATUS_SUCCESS;
}

//==============================================================================
// Request Handling
//==============================================================================

void Http3Server::handle_new_connection(MSH3_CONNECTION* connection, const char* server_name) {
    if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
        std::cout << "[HTTP/3] New connection from: " << (server_name ? server_name : "unknown") << std::endl;
    }
    
    // Set connection callback
    MsH3ConnectionSetCallbackHandler(connection, connection_callback, this);

    // Apply configuration
    MSH3_STATUS status = MsH3ConnectionSetConfiguration(connection, config_);
    if (MSH3_FAILED(status)) {
        std::cerr << "[HTTP/3] Failed to configure connection" << std::endl;
        MsH3ConnectionClose(connection);
    }
}

void Http3Server::handle_new_request(MSH3_REQUEST* request) {
    auto req = std::make_unique<Http3Request>();
    req->handle = request;
    
    {
        std::lock_guard lock(mutex_);
        requests_[request] = std::move(req);
    }
    
    // Set request callback
    MsH3RequestSetCallbackHandler(request, request_callback, this);
    
    // Enable receiving data
    MsH3RequestSetReceiveEnabled(request, true);
}

void Http3Server::handle_header(Http3Request* req, const MSH3_HEADER* header) {
    std::string name(header->Name, header->NameLength);
    std::string value(header->Value, header->ValueLength);
    
    if (name == ":method") {
        req->method = value;
    } else if (name == ":path") {
        req->path = value;
    } else if (name == "content-type") {
        req->content_type = value;
    } else if (name == "content-length") {
        req->content_length = std::stoul(value);
    }
    
    if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
        std::cout << "[HTTP/3] Header: " << name << ": " << value << std::endl;
    }
}

void Http3Server::handle_data(Http3Request* req, const uint8_t* data, uint32_t length) {
    if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
        std::cout << "[HTTP/3] Data received: " << length << " bytes (total: " 
                  << req->body.size() + length << "/" << req->content_length << ")" << std::endl;
    }
    req->body.insert(req->body.end(), data, data + length);
}

void Http3Server::handle_request_complete(Http3Request* req) {
    if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
        std::cout << "[HTTP/3] Request: " << req->method << " " << req->path 
                  << " (body: " << req->body.size() << " bytes)" << std::endl;
    }
    
    // Handle OPTIONS preflight for CORS
    if (req->method == "OPTIONS") {
        send_cors_preflight(req);
        return;
    }
    
    // Handle RPC requests (POST to /rpc)
    if (req->method == "POST" && 
        (req->path == "/rpc" || req->path.rfind("/rpc/", 0) == 0)) {
        handle_rpc_request(req);
        return;
    }
    
    // Handle static file requests
    if (req->method == "GET" || req->method == "HEAD") {
        if (g_cfg.http_root_dir.empty()) {
            send_error(req, 400, "Static file serving not configured");
            return;
        }
        
        // Security check
        if (req->path.empty() || req->path[0] != '/' || 
            req->path.find("..") != std::string::npos) {
            send_error(req, 400, "Invalid request path");
            return;
        }
        
        std::string file_path;
        if (req->path == "/") {
            file_path = path_cat(g_cfg.http_root_dir, "/index.html");
        } else if (!g_cfg.spa_links.empty() && req->path.find('.') == std::string::npos) {
            // SPA route handling
            auto it = std::find(g_cfg.spa_links.begin(), g_cfg.spa_links.end(), req->path);
            if (it == g_cfg.spa_links.end()) {
                send_error(req, 404, "Not found");
                return;
            }
            file_path = path_cat(g_cfg.http_root_dir, "/index.html");
        } else {
            file_path = path_cat(g_cfg.http_root_dir, req->path);
        }
        
        send_file_response(req, file_path);
        return;
    }
    
    send_error(req, 405, "Method not allowed");
}

//==============================================================================
// RPC Handling
//==============================================================================

void Http3Server::handle_rpc_request(Http3Request* req) {
    if (req->body.empty()) {
        send_error(req, 400, "Empty request body");
        return;
    }
    
    try {
        std::string request_body(req->body.begin(), req->body.end());
        std::string response_body;
        
        if (!process_http_rpc(ioc_, request_body, response_body)) {
            send_error(req, 500, "RPC processing failed");
            return;
        }
        
        if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
            std::cout << "[HTTP/3] RPC processed, response size: " << response_body.size() << " bytes" << std::endl;
        }
        
        send_response(req, 200, "application/octet-stream", response_body);
        
    } catch (const std::exception& e) {
        std::cerr << "[HTTP/3] RPC exception: " << e.what() << std::endl;
        send_error(req, 500, e.what());
    }
}

//==============================================================================
// Response Helpers
//==============================================================================

void Http3Server::send_response(Http3Request* req, int status_code,
                                 const std::string& content_type,
                                 const std::string& body) {
    // Store response data in request object to keep it alive until SEND_COMPLETE
    // MsQuic/msh3 takes ownership of buffers until send is complete!
    req->response_body = body;
    req->response_status = std::to_string(status_code);
    req->response_content_type = content_type;
    req->response_content_length = std::to_string(req->response_body.size());

    if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
        std::cout << "[HTTP/3] Sending response: status=" << status_code 
                  << ", content-type=" << content_type
                  << ", body size=" << req->response_body.size() << std::endl;
    }
    
    MSH3_HEADER headers[] = {
        { ":status", 7, req->response_status.c_str(), req->response_status.size() },
        { "content-type", 12, req->response_content_type.c_str(), req->response_content_type.size() },
        { "content-length", 14, req->response_content_length.c_str(), req->response_content_length.size() },
        { "access-control-allow-origin", 27, "*", 1 },
    };

    MsH3RequestSend(
        req->handle,
        MSH3_REQUEST_SEND_FLAG_FIN,
        headers,
        sizeof(headers) / sizeof(headers[0]),
        req->response_body.data(),
        static_cast<uint32_t>(req->response_body.size()),
        nullptr);
}

void Http3Server::send_file_response(Http3Request* req, const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        send_error(req, 404, "File not found");
        return;
    }
    
    std::ostringstream oss;
    oss << file.rdbuf();
    std::string content = oss.str();
    
    std::string mime = std::string(mime_type(path));
    send_response(req, 200, mime, content);
}

void Http3Server::send_error(Http3Request* req, int status_code, const std::string& message) {
    std::string body = "Error " + std::to_string(status_code) + ": " + message;
    send_response(req, status_code, "text/plain", body);
}

void Http3Server::send_cors_preflight(Http3Request* req) {
    MSH3_HEADER headers[] = {
        { ":status", 7, "204", 3 },
        { "access-control-allow-origin", 27, "*", 1 },
        { "access-control-allow-methods", 28, "GET, POST, OPTIONS", 18 },
        { "access-control-allow-headers", 28, "Content-Type", 12 },
        { "access-control-max-age", 22, "86400", 5 },
    };
    
    MsH3RequestSend(
        req->handle,
        MSH3_REQUEST_SEND_FLAG_FIN,
        headers,
        sizeof(headers) / sizeof(headers[0]),
        nullptr,
        0,
        nullptr);
}

//==============================================================================
// Global HTTP/3 Server Instance
//==============================================================================

static std::unique_ptr<Http3Server> g_http3_server;

NPRPC_API void init_http3_server(boost::asio::io_context& ioc) {
    // HTTP/3 is enabled when http3_enabled flag is set and we have an HTTP port
    if (!g_cfg.http3_enabled || g_cfg.listen_http_port == 0) {
        return;
    }
    
    std::cout << "[HTTP/3] Initializing on port " << g_cfg.listen_http_port << std::endl;
    
    if (g_cfg.http3_cert_file.empty() || g_cfg.http3_key_file.empty()) {
        std::cerr << "[HTTP/3] Certificate and key files required" << std::endl;
        return;
    }
    
    std::cout << "[HTTP/3] Using cert: " << g_cfg.http3_cert_file << std::endl;
    
    g_http3_server = std::make_unique<Http3Server>(
        ioc,
        g_cfg.http3_cert_file,
        g_cfg.http3_key_file,
        g_cfg.listen_http_port);  // Use same port as HTTP/1.1
    
    if (!g_http3_server->start()) {
        std::cerr << "[HTTP/3] Failed to start server" << std::endl;
        g_http3_server.reset();
    } else {
        std::cout << "[HTTP/3] Server started successfully on port " << g_cfg.listen_http_port << std::endl;
    }
}

NPRPC_API void stop_http3_server() {
    if (g_http3_server) {
        g_http3_server->stop();
        g_http3_server.reset();
    }
}

} // namespace nprpc::impl

#endif // NPRPC_HTTP3_ENABLED && NPRPC_HTTP3_BACKEND_MSH3

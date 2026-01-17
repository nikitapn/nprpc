// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

// Bridge header for Swift C++ interop
// This header exposes a simplified C++ API that Swift can import directly

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <memory>
#include <vector>
#include <optional>
#include <functional>

// Forward declare nprpc types to avoid pulling in all headers initially
namespace nprpc {
    class Rpc;
    class Poa;
    class Object;
    
    namespace impl {
        class RpcImpl;
    }
}

namespace nprpc_swift {

// ============================================================================
// Version & Info
// ============================================================================

/// Get NPRPC version string
inline const char* get_version() {
    return "1.0.0";  // TODO: Pull from actual version
}

// ============================================================================
// Simple test function to verify interop works
// ============================================================================

/// Simple function to verify Swift can call C++ code
inline int32_t add_numbers(int32_t a, int32_t b) {
    return a + b;
}

/// Test string passing from Swift to C++
inline std::string greet(const std::string& name) {
    std::string result = "Hello from NPRPC C++, ";
    result += name;
    result += "!";
    return result;
}

/// Test returning a vector to Swift
inline std::vector<int32_t> get_test_array() {
    return {1, 2, 3, 4, 5};
}

/// Helper to convert std::string to C string for Swift (avoids interior pointer issues)
/// Swift can't call .c_str() due to lifetime concerns
inline const char* string_to_cstr(const std::string& str) {
    return str.c_str();
}

// ============================================================================
// RpcConfig - Configuration for RPC builder (Swift-friendly)
// ============================================================================

struct RpcConfig {
    std::string nameserver_ip = "127.0.0.1";
    uint16_t nameserver_port = 15000;
    uint16_t listen_tcp_port = 0;
    uint16_t listen_ws_port = 0;
    uint16_t listen_http_port = 0;
    uint16_t listen_quic_port = 0;
    uint16_t listen_udp_port = 0;
    std::string http_root_dir;
    std::string ssl_cert_file;
    std::string ssl_key_file;
    std::string quic_cert_file;
    std::string quic_key_file;
    uint16_t thread_pool_size = 4;
    
    // Default constructor
    RpcConfig() = default;
};

// ============================================================================
// RpcHandle - Opaque handle to Rpc singleton (Swift-friendly wrapper)
// ============================================================================

class RpcHandle {
public:
    RpcHandle() = default;
    ~RpcHandle();
    
    // Prevent copying (Rpc is a singleton)
    RpcHandle(const RpcHandle&) = delete;
    RpcHandle& operator=(const RpcHandle&) = delete;
    
    // Allow moving
    RpcHandle(RpcHandle&&) noexcept;
    RpcHandle& operator=(RpcHandle&&) noexcept;
    
    /// Initialize the RPC system with config
    bool initialize(const RpcConfig& config);
    
    /// Run the io_context (blocks until stopped)
    void run();
    
    /// Stop the io_context
    void stop();
    
    /// Check if initialized
    bool is_initialized() const { return initialized_; }
    
    /// Get debug info
    std::string get_debug_info() const;
    
private:
    bool initialized_ = false;
};

/// Create a new RPC handle
inline std::unique_ptr<RpcHandle> create_rpc() {
    return std::make_unique<RpcHandle>();
}

// ============================================================================
// EndPoint - Network endpoint (Swift-friendly)
// ============================================================================

enum class EndPointType : uint8_t {
    Unknown = 0,
    Tcp = 1,
    WebSocket = 2,
    Http = 3,
    Quic = 4,
    Udp = 5,
    SharedMemory = 6
};

struct EndPointInfo {
    EndPointType type = EndPointType::Unknown;
    std::string hostname;
    uint16_t port = 0;
    std::string path;  // For WebSocket/HTTP
    
    /// Parse from URL string
    static std::optional<EndPointInfo> parse(const std::string& url);
    
    /// Convert back to URL string
    std::string to_url() const;
};

} // namespace nprpc_swift

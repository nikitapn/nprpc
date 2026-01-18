// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include "include/nprpc_bridge.hpp"

#include <sstream>
#include <regex>

namespace nprpc_swift {

// ============================================================================
// RpcHandle implementation
// ============================================================================

RpcHandle::~RpcHandle() {
    if (initialized_) {
        stop();
    }
}

RpcHandle::RpcHandle(RpcHandle&& other) noexcept 
    : initialized_(other.initialized_) {
    other.initialized_ = false;
}

RpcHandle& RpcHandle::operator=(RpcHandle&& other) noexcept {
    if (this != &other) {
        if (initialized_) {
            stop();
        }
        initialized_ = other.initialized_;
        other.initialized_ = false;
    }
    return *this;
}

bool RpcHandle::initialize(const RpcConfig& config) {
    if (initialized_) {
        return false;  // Already initialized
    }
    
    // TODO: Actually initialize nprpc::Rpc here
    // For POC, just mark as initialized
    // 
    // auto builder = nprpc::Rpc::builder();
    // builder->set_nameserver_address(config.nameserver_ip, config.nameserver_port);
    // if (config.listen_tcp_port > 0) builder->set_listen_tcp_port(config.listen_tcp_port);
    // ... etc
    // builder->build();
    
    initialized_ = true;
    return true;
}

void RpcHandle::run() {
    if (!initialized_) return;
    // TODO: Call nprpc::Rpc::run()
}

void RpcHandle::stop() {
    if (!initialized_) return;
    // TODO: Call nprpc::Rpc::stop()
    initialized_ = false;
}

std::string RpcHandle::get_debug_info() const {
    std::ostringstream oss;
    oss << "RpcHandle { initialized: " << (initialized_ ? "true" : "false") << " }";
    return oss.str();
}

// ============================================================================
// EndPointInfo implementation
// ============================================================================

std::optional<EndPointInfo> EndPointInfo::parse(const std::string& url) {
    // Simple URL parsing for POC
    // Format: scheme://host:port/path
    
    EndPointInfo info;
    
    // Find scheme
    auto scheme_end = url.find("://");
    if (scheme_end == std::string::npos) {
        return std::nullopt;
    }
    
    std::string scheme = url.substr(0, scheme_end);
    if (scheme == "tcp") info.type = EndPointType::Tcp;
    else if (scheme == "ws" || scheme == "wss") info.type = EndPointType::WebSocket;
    else if (scheme == "http" || scheme == "https") info.type = EndPointType::Http;
    else if (scheme == "quic") info.type = EndPointType::Quic;
    else if (scheme == "udp") info.type = EndPointType::Udp;
    else if (scheme == "shm" || scheme == "mem") info.type = EndPointType::SharedMemory;
    else return std::nullopt;
    
    // Parse host:port
    size_t host_start = scheme_end + 3;
    size_t path_start = url.find('/', host_start);
    std::string host_port;
    
    if (path_start != std::string::npos) {
        host_port = url.substr(host_start, path_start - host_start);
        info.path = url.substr(path_start);
    } else {
        host_port = url.substr(host_start);
    }
    
    // Split host:port
    auto colon_pos = host_port.rfind(':');
    if (colon_pos != std::string::npos) {
        info.hostname = host_port.substr(0, colon_pos);
        info.port = static_cast<uint16_t>(std::stoi(host_port.substr(colon_pos + 1)));
    } else {
        info.hostname = host_port;
    }
    
    return info;
}

std::string EndPointInfo::to_url() const {
    std::ostringstream oss;
    
    switch (type) {
        case EndPointType::Tcp: oss << "tcp://"; break;
        case EndPointType::WebSocket: oss << "ws://"; break;
        case EndPointType::Http: oss << "http://"; break;
        case EndPointType::Quic: oss << "quic://"; break;
        case EndPointType::Udp: oss << "udp://"; break;
        case EndPointType::SharedMemory: oss << "shm://"; break;
        default: oss << "unknown://"; break;
    }
    
    oss << hostname;
    if (port > 0) {
        oss << ":" << port;
    }
    if (!path.empty()) {
        oss << path;
    }
    
    return oss.str();
}

} // namespace nprpc_swift

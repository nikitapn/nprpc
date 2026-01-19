// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include "include/nprpc_bridge.hpp"

// Include full nprpc headers in implementation
#include <nprpc/nprpc.hpp>
#include <nprpc/basic.hpp>
#include <nprpc/endpoint.hpp>
#include <nprpc/object_ptr.hpp>

#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/asio/use_future.hpp>

#include <sstream>
#include <regex>

namespace nprpc_swift {

struct RpcHandleImpl {
    nprpc::Rpc* rpc_instance = nullptr;

    boost::asio::io_context ioc;
    boost::asio::thread_pool pool;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type>
      work_guard;

    RpcHandleImpl(size_t size)
        : pool(size), work_guard(boost::asio::make_work_guard(ioc))
    {
        for (size_t i = 0; i < size; ++i) {
            boost::asio::post(pool, [this] {
                ioc.run();
            });
        }
    }

    ~RpcHandleImpl() {
        ioc.stop();
        work_guard.reset();
        pool.join();
    }
};

// ============================================================================
// RpcHandle implementation
// ============================================================================

RpcHandle::~RpcHandle() {
    if (initialized_) {
        stop();
    }
}

RpcHandle::RpcHandle(RpcHandle&& other) noexcept 
    : initialized_(other.initialized_), impl_(other.impl_) {
    other.initialized_ = false;
    other.impl_ = nullptr;
}

RpcHandle& RpcHandle::operator=(RpcHandle&& other) noexcept {
    if (this != &other) {
        if (initialized_) {
            stop();
        }
        initialized_ = other.initialized_;
        impl_ = other.impl_;
        other.initialized_ = false;
        other.impl_ = nullptr;
    }
    return *this;
}

bool RpcHandle::initialize(RpcBuildConfig* config) {
    if (initialized_ || !config) {
        return false;  // Already initialized or null config
    }
    
    try {
        // Create implementation (with thread pool if needed)
        impl_ = new RpcHandleImpl(4);
        auto* impl = static_cast<RpcHandleImpl*>(impl_);

        // Convert RpcBuildConfig to nprpc::impl::BuildConfig
        nprpc::impl::BuildConfig cxxConfig;
        cxxConfig.log_level = static_cast<nprpc::LogLevel>(config->log_level);
        std::memcpy(&cxxConfig.uuid, config->uuid, 16);
        cxxConfig.tcp_port = config->tcp_port;
        cxxConfig.udp_port = config->udp_port;
        cxxConfig.hostname = config->hostname;
        cxxConfig.http_port = config->http_port;
        cxxConfig.http_ssl_enabled = config->http_ssl_enabled;
        cxxConfig.http3_enabled = config->http3_enabled;
        cxxConfig.ssr_enabled = config->ssr_enabled;
        cxxConfig.http_ssl_client_disable_verification = config->http_ssl_client_disable_verification;
        cxxConfig.http_cert_file = config->http_cert_file;
        cxxConfig.http_key_file = config->http_key_file;
        cxxConfig.http_dhparams_file = config->http_dhparams_file;
        cxxConfig.http_root_dir = config->http_root_dir;
        cxxConfig.ssr_handler_dir = config->ssr_handler_dir;
        cxxConfig.quic_port = config->quic_port;
        cxxConfig.quic_cert_file = config->quic_cert_file;
        cxxConfig.quic_key_file = config->quic_key_file;
        cxxConfig.ssl_client_self_signed_cert_path = config->ssl_client_self_signed_cert_path;

        // Build RpcSwift using the provided config
        // We create a custom builder that accepts pre-configured BuildConfig
        class RpcSwiftBuilder : public nprpc::impl::RpcBuilderBase {
            nprpc::impl::BuildConfig cfg_;
        public:
            explicit RpcSwiftBuilder(nprpc::impl::BuildConfig&& cfg)
                : nprpc::impl::RpcBuilderBase(cfg_), cfg_(std::move(cfg)) {}
            
            nprpc::Rpc* build(boost::asio::io_context& ioc) {
                return nprpc::impl::RpcBuilderBase::build(ioc);
            }
        };
        
        impl->rpc_instance = RpcSwiftBuilder(std::move(cxxConfig)).build(impl->ioc);

        initialized_ = true;
        return true;
    } catch (const std::exception& e) {
        // Log error but don't throw to Swift (Swift runtime not built with exceptions)
        std::cerr << "RpcHandle::initialize failed: " << e.what() << std::endl;
        if (impl_) {
            delete static_cast<RpcHandleImpl*>(impl_);
            impl_ = nullptr;
        }
        return false;
    } catch (...) {
        std::cerr << "RpcHandle::initialize failed with unknown exception" << std::endl;
        if (impl_) {
            delete static_cast<RpcHandleImpl*>(impl_);
            impl_ = nullptr;
        }
        return false;
    }
}

void RpcHandle::run() {
    if (!initialized_ || !impl_) return;
    try {
        auto* impl = static_cast<RpcHandleImpl*>(impl_);
        // Run the io_context (blocks)
        impl->ioc.run();
    } catch (const std::exception& e) {
        std::cerr << "RpcHandle::run failed: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "RpcHandle::run failed with unknown exception" << std::endl;
    }
}

void RpcHandle::stop() {
    if (!initialized_ || !impl_) return;
    try {
        auto* impl = static_cast<RpcHandleImpl*>(impl_);
        delete impl;
        impl_ = nullptr;
        initialized_ = false;
    } catch (const std::exception& e) {
        std::cerr << "RpcHandle::stop failed: " << e.what() << std::endl;
        impl_ = nullptr;
        initialized_ = false;
    } catch (...) {
        std::cerr << "RpcHandle::stop failed with unknown exception" << std::endl;
        impl_ = nullptr;
        initialized_ = false;
    }
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

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

// External declaration for the free function in rpc_impl.cpp
// This avoids including nprpc_impl.hpp which has template issues with Swift's clang
namespace nprpc::impl {
    NPRPC_API void rpc_call(const nprpc::EndPoint& endpoint, nprpc::flat_buffer& buffer, uint32_t timeout_ms);
}

namespace nprpc_swift {

struct RpcHandleImpl {
    nprpc::Rpc* rpc_instance = nullptr;

    boost::asio::io_context ioc;
    std::unique_ptr<boost::asio::thread_pool> pool;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type>
      work_guard;

    RpcHandleImpl(size_t thread_count)
        : work_guard(boost::asio::make_work_guard(ioc))
    {
        // Only create thread pool if thread_count > 0
        // If thread_count == 0, user must call run() manually
        if (thread_count > 0) {
            pool = std::make_unique<boost::asio::thread_pool>(thread_count);
            for (size_t i = 0; i < thread_count; ++i) {
                boost::asio::post(*pool, [this] {
                    ioc.run();
                });
            }
        }
    }

    ~RpcHandleImpl() {
        ioc.stop();
        work_guard.reset();
        if (pool) {
            pool->join();
        }
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

bool RpcHandle::initialize(RpcBuildConfig* config, size_t thread_pool_size) {
    if (initialized_ || !config) {
        return false;  // Already initialized or null config
    }
    
    try {
        // Create implementation
        // thread_pool_size == 0: no thread pool, user must call run() manually (blocking)
        // thread_pool_size > 0: background thread pool, run() is a no-op
        impl_ = new RpcHandleImpl(thread_pool_size);
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
        // Only call run() if no thread pool (thread_count was 0)
        // Otherwise io_context is already running in background threads
        if (!impl->pool) {
            impl->ioc.run();  // Blocking call for manual mode
        }
        // If thread pool exists, this is a no-op (already running in background)
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

// ============================================================================
// C Bridge Functions for Swift Interop
// ============================================================================

extern "C" {

// FlatBuffer operations
void* nprpc_flatbuffer_create() {
    return new nprpc::flat_buffer();
}

void nprpc_flatbuffer_destroy(void* fb) {
    delete static_cast<nprpc::flat_buffer*>(fb);
}

void* nprpc_flatbuffer_data(void* fb) {
    auto* buffer = static_cast<nprpc::flat_buffer*>(fb);
    auto span = buffer->data();
    return span.size() > 0 ? span.data() : nullptr;
}

const void* nprpc_flatbuffer_cdata(void* fb) {
    auto* buffer = static_cast<nprpc::flat_buffer*>(fb);
    auto span = buffer->cdata();
    return span.size() > 0 ? span.data() : nullptr;
}

size_t nprpc_flatbuffer_size(void* fb) {
    return static_cast<nprpc::flat_buffer*>(fb)->size();
}

void nprpc_flatbuffer_prepare(void* fb, size_t n) {
    static_cast<nprpc::flat_buffer*>(fb)->prepare(n);
}

void nprpc_flatbuffer_commit(void* fb, size_t n) {
    static_cast<nprpc::flat_buffer*>(fb)->commit(n);
}

void nprpc_flatbuffer_consume(void* fb, size_t n) {
    static_cast<nprpc::flat_buffer*>(fb)->consume(n);
}

// Object operations
void nprpc_object_release(void* obj) {
    if (!obj) return;
    auto* object = static_cast<nprpc::Object*>(obj);
    object->release();
}

uint32_t nprpc_object_add_ref(void* obj) {
    if (!obj) return 0;
    return static_cast<nprpc::Object*>(obj)->add_ref();
}

uint32_t nprpc_object_get_timeout(void* obj) {
    if (!obj) return 0;
    return static_cast<nprpc::Object*>(obj)->get_timeout();
}

uint32_t nprpc_object_set_timeout(void* obj, uint32_t timeout_ms) {
    if (!obj) return 0;
    return static_cast<nprpc::Object*>(obj)->set_timeout(timeout_ms);
}

// Thread-local storage for hostname string to ensure lifetime
static thread_local std::string g_endpoint_hostname;

uint32_t nprpc_object_get_endpoint_type(void* obj) {
    if (!obj) return 0;
    const auto& ep = static_cast<nprpc::Object*>(obj)->get_endpoint();
    return static_cast<uint32_t>(ep.type());
}

const char* nprpc_object_get_endpoint_hostname(void* obj) {
    if (!obj) return "";
    const auto& ep = static_cast<nprpc::Object*>(obj)->get_endpoint();
    // Copy to thread-local to ensure lifetime
    g_endpoint_hostname = std::string(ep.hostname());
    return g_endpoint_hostname.c_str();
}

uint16_t nprpc_object_get_endpoint_port(void* obj) {
    if (!obj) return 0;
    const auto& ep = static_cast<nprpc::Object*>(obj)->get_endpoint();
    return ep.port();
}

bool nprpc_object_select_endpoint(void* obj) {
    if (!obj) return false;
    return static_cast<nprpc::Object*>(obj)->select_endpoint();
}

// ObjectId accessor functions for Swift
uint64_t nprpc_objectid_get_object_id(void* oid_ptr) {
    return static_cast<nprpc::ObjectId*>(oid_ptr)->object_id();
}

uint16_t nprpc_objectid_get_poa_idx(void* oid_ptr) {
    return static_cast<nprpc::ObjectId*>(oid_ptr)->poa_idx();
}

uint16_t nprpc_objectid_get_flags(void* oid_ptr) {
    return static_cast<nprpc::ObjectId*>(oid_ptr)->flags();
}

const char* nprpc_objectid_get_class_id(void* oid_ptr) {
    return static_cast<nprpc::ObjectId*>(oid_ptr)->class_id().data();
}

const char* nprpc_objectid_get_urls(void* oid_ptr) {
    return static_cast<nprpc::ObjectId*>(oid_ptr)->urls().data();
}

const uint8_t* nprpc_objectid_get_origin(void* oid_ptr) {
    return static_cast<nprpc::ObjectId*>(oid_ptr)->origin().data();
}

void nprpc_objectid_destroy(void* oid_ptr) {
    delete static_cast<nprpc::ObjectId*>(oid_ptr);
}

// Object RPC call - sends request and receives reply via C++ runtime
int nprpc_object_send_receive(void* obj_ptr, void* buffer_ptr, uint32_t timeout_ms) {
    if (!obj_ptr || !buffer_ptr) return -1;
    
    auto* obj = static_cast<nprpc::Object*>(obj_ptr);
    auto* buffer = static_cast<nprpc::flat_buffer*>(buffer_ptr);
    
    // Ensure endpoint is selected
    if (obj->get_endpoint().empty()) {
        if (!obj->select_endpoint()) {
            return -2;  // Failed to select endpoint
        }
    }
    
    try {
        // Use the free function wrapper to make the call
        nprpc::impl::rpc_call(obj->get_endpoint(), *buffer, timeout_ms);
        return 0;  // Success
    } catch (const std::exception& e) {
        // TODO: Consider passing error message back to Swift
        return -3;  // RPC call failed
    }
}

// Object string serialization (NPRPC IOR format)
const char* nprpc_object_to_string(void* obj_ptr) {
    if (!obj_ptr) return nullptr;
    
    auto* obj = static_cast<nprpc::Object*>(obj_ptr);
    std::string str = obj->to_string();
    
    // Allocate a copy that Swift can own and free
    char* result = new char[str.size() + 1];
    std::memcpy(result, str.c_str(), str.size() + 1);
    return result;
}

void* nprpc_object_from_string(const char* str) {
    if (!str) return nullptr;
    return nprpc::Object::from_string(str);
}

void nprpc_free_string(const char* str) {
    delete[] str;
}

// Swift Servant Bridge
class SwiftServantBridge : public nprpc::ObjectServant {
public:
    using DispatchFunc = void (*)(void*, void*, void*, void*);
    
    SwiftServantBridge(void* swift_servant, const std::string& class_name, DispatchFunc dispatch)
        : swift_servant_(swift_servant)
        , class_name_(class_name)
        , dispatch_func_(dispatch)
    {}
    
    ~SwiftServantBridge() override {
        // Don't delete swift_servant_ - Swift manages its lifetime
    }
    
    std::string_view get_class() const noexcept override {
        return class_name_;
    }
    
    void dispatch(nprpc::SessionContext& ctx, bool from_parent) override {
        // Call Swift dispatch through trampoline
        // Pass rx_buffer, tx_buffer, and endpoint
        if (dispatch_func_ && swift_servant_) {
            dispatch_func_(swift_servant_, ctx.rx_buffer, ctx.tx_buffer, &ctx.remote_endpoint);
        }
    }
    
private:
    void* swift_servant_;
    std::string class_name_;
    DispatchFunc dispatch_func_;
};

// Activate a Swift servant in a POA
// Returns serialized ObjectId in a new flat_buffer that Swift must destroy
void* nprpc_poa_activate_swift_servant(
    void* poa_handle,
    void* swift_servant,
    const char* class_name,
    void (*dispatch_func)(void*, void*, void*, void*))
{
    auto* poa = static_cast<nprpc::Poa*>(poa_handle);
    if (!poa || !swift_servant || !class_name || !dispatch_func) {
        return nullptr;
    }
    
    try {
        auto bridge = std::make_unique<SwiftServantBridge>(
            swift_servant, class_name, dispatch_func);
        
        auto oid = poa->activate_object(bridge.release(),
                                        nprpc::ObjectActivationFlags::NONE);
        
        // Allocate and copy ObjectId to return to Swift
        // Swift will need to free this memory
        auto* oid_copy = new nprpc::ObjectId(oid);
        
        // Return pointer to ObjectId - Swift will read it and delete it
        return oid_copy;
    } catch (...) {
        return nullptr;
    }
}

} // extern "C"

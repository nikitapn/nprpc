// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include "include/nprpc_bridge.hpp"

// Include full nprpc headers in implementation
#include <nprpc/nprpc.hpp>
#include <nprpc/basic.hpp>
#include <nprpc/endpoint.hpp>
#include <nprpc/object_ptr.hpp>
#include <nprpc/stream_base.hpp>
#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/impl/stream_manager.hpp>
#include <nprpc/impl/session.hpp>
#include <nprpc/session_context.h>
#include <nprpc/http_auth.hpp>

// Use nprpc's internal logger for synchronized output
#include "../../../src/logging.hpp"
#include "nprpc_base.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/asio/use_future.hpp>

#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string_view>

// Forward declarations for nprpc impl internals
// Avoids including nprpc_impl.hpp which has template issues with Swift's clang
namespace nprpc::impl {
    class RpcImpl;
    class Session;
    NPRPC_API extern RpcImpl* g_rpc;

    NPRPC_API void rpc_call(const nprpc::EndPoint& endpoint, nprpc::flat_buffer& buffer, uint32_t timeout_ms);

    NPRPC_API void rpc_call_async(
        const nprpc::EndPoint& endpoint,
        nprpc::flat_buffer&& buffer,
        std::function<void(const boost::system::error_code&, nprpc::flat_buffer&)>&& completion_handler,
        uint32_t timeout_ms);

    NPRPC_API void rpc_call_async_no_reply(
        const nprpc::EndPoint& endpoint,
        nprpc::flat_buffer&& buffer,
        uint32_t timeout_ms);

    // Forward declare get_session - we'll use it for stream registration
    NPRPC_API std::shared_ptr<Session> get_session_for_endpoint(const nprpc::EndPoint& endpoint);
}

namespace nprpc_swift {

namespace {
const char* copy_string_to_c_heap(const std::string& value) {
    auto* buffer = new char[value.size() + 1];
    std::memcpy(buffer, value.c_str(), value.size() + 1);
    return buffer;
}

void decode_allowed_origins(std::string_view encoded,
                            std::vector<std::string>& out) {
    size_t start = 0;
    while (start <= encoded.size()) {
        const auto end = encoded.find('\n', start);
        const auto len = end == std::string_view::npos ? encoded.size() - start
                                                       : end - start;
        if (len != 0) {
            out.emplace_back(encoded.substr(start, len));
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
}
}

struct RpcHandleImpl {
    nprpc::Rpc* rpc_instance = nullptr;

    void run() {
        if (!rpc_instance) {
            NPRPC_LOG_ERROR("[SWB] RpcHandleImpl::run called but rpc_instance is null");
            return;
        }
        rpc_instance->run();
    }

    void start_thread_pool(size_t thread_count) noexcept {
        if (!rpc_instance) {
            NPRPC_LOG_ERROR("[SWB] RpcHandleImpl::start_thread_pool called but rpc_instance is null");
            return;
        }
        rpc_instance->start_thread_pool(thread_count);
    }

    void stop() noexcept {
        if (!rpc_instance) {
            NPRPC_LOG_ERROR("[SWB] RpcHandleImpl::stop called but rpc_instance is null");
            return;
        }
        rpc_instance->ioc().stop();
    }

    ~RpcHandleImpl() {
        if (rpc_instance) {
            rpc_instance->destroy();
            rpc_instance = nullptr;
        }
    }
};

// ============================================================================
// RpcHandle implementation
// ============================================================================

RpcHandle::~RpcHandle() {
    if (initialized_) {
        delete static_cast<RpcHandleImpl*>(impl_);
        impl_ = nullptr;
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
        // Create implementation
        impl_ = new RpcHandleImpl();
        auto* impl = static_cast<RpcHandleImpl*>(impl_);

        // Convert RpcBuildConfig to nprpc::impl::BuildConfig
        nprpc::impl::BuildConfig cxxConfig;
        cxxConfig.log_level = static_cast<nprpc::LogLevel>(config->log_level);
        std::memcpy(&cxxConfig.uuid, config->uuid, 16);
        cxxConfig.tcp_port = config->tcp_port;
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
        decode_allowed_origins(config->http_allowed_origins,
                       cxxConfig.http_allowed_origins);
        cxxConfig.http_max_request_body_size = config->http_max_request_body_size;
        cxxConfig.http_websocket_compression_enabled =
            config->http_websocket_compression_enabled;
        cxxConfig.http_websocket_max_message_size = config->http_websocket_max_message_size;
        cxxConfig.http_webtransport_max_message_size = config->http_webtransport_max_message_size;
        cxxConfig.http_websocket_max_active_sessions_per_ip =
            config->http_websocket_max_active_sessions_per_ip;
        cxxConfig.http_websocket_upgrades_per_ip_per_second =
            config->http_websocket_upgrades_per_ip_per_second;
        cxxConfig.http_websocket_upgrades_burst =
            config->http_websocket_upgrades_burst;
        cxxConfig.http_websocket_requests_per_session_per_second =
            config->http_websocket_requests_per_session_per_second;
        cxxConfig.http_websocket_requests_burst =
            config->http_websocket_requests_burst;
        cxxConfig.http3_worker_count = config->http3_worker_count;
        cxxConfig.http3_max_active_connections_per_ip =
            config->http3_max_active_connections_per_ip;
        cxxConfig.http3_max_new_connections_per_ip_per_second =
            config->http3_max_new_connections_per_ip_per_second;
        cxxConfig.http3_max_new_connections_burst =
            config->http3_max_new_connections_burst;
        cxxConfig.http_rpc_max_requests_per_ip_per_second =
            config->http_rpc_max_requests_per_ip_per_second;
        cxxConfig.http_rpc_max_requests_burst =
            config->http_rpc_max_requests_burst;
        cxxConfig.http_webtransport_connects_per_ip_per_second =
            config->http_webtransport_connects_per_ip_per_second;
        cxxConfig.http_webtransport_connects_burst =
            config->http_webtransport_connects_burst;
        cxxConfig.http_webtransport_requests_per_session_per_second =
            config->http_webtransport_requests_per_session_per_second;
        cxxConfig.http_webtransport_requests_burst =
            config->http_webtransport_requests_burst;
        cxxConfig.http_webtransport_stream_opens_per_session_per_second =
            config->http_webtransport_stream_opens_per_session_per_second;
        cxxConfig.http_webtransport_stream_opens_burst =
            config->http_webtransport_stream_opens_burst;
        cxxConfig.ssr_handler_dir = config->ssr_handler_dir;
        cxxConfig.watch_files = config->watch_files;
        cxxConfig.quic_port = config->quic_port;
        cxxConfig.quic_cert_file = config->quic_cert_file;
        cxxConfig.quic_key_file = config->quic_key_file;
        cxxConfig.ssl_client_self_signed_cert_path = config->ssl_client_self_signed_cert_path;
        cxxConfig.shm_egress_channel  = config->shm_egress_channel;
        cxxConfig.shm_ingress_channel = config->shm_ingress_channel;

        // Build Rpc using the provided config
        class RpcSwiftBuilder : public nprpc::impl::RpcBuilderBase {
        public:
            explicit RpcSwiftBuilder(std::shared_ptr<nprpc::impl::BuildConfig> cfg)
                : nprpc::impl::RpcBuilderBase(std::move(cfg)) {}

            nprpc::Rpc* build() {
                return nprpc::impl::RpcBuilderBase::build();
            }
        };

        impl->rpc_instance = RpcSwiftBuilder(
            std::make_shared<nprpc::impl::BuildConfig>(std::move(cxxConfig))).build();

        initialized_ = true;
        return true;
    } catch (const std::exception& e) {
        // Log error but don't throw to Swift (Swift runtime not built with exceptions)
        NPRPC_LOG_ERROR("[SWB] RpcHandle::initialize failed: {}", e.what());
        if (impl_) {
            delete static_cast<RpcHandleImpl*>(impl_);
            impl_ = nullptr;
        }
        return false;
    } catch (...) {
        NPRPC_LOG_ERROR("[SWB] RpcHandle::initialize failed with unknown exception");
        if (impl_) {
            delete static_cast<RpcHandleImpl*>(impl_);
            impl_ = nullptr;
        }
        return false;
    }
}

int RpcHandle::run() {
    if (!initialized_ || !impl_)
        return -1;
    auto* impl = static_cast<RpcHandleImpl*>(impl_);
    try {
        impl->run();
    } catch (const std::exception& e) {
        NPRPC_LOG_ERROR("RpcHandle::run failed: {}", e.what());
        return -2;
    } catch (...) {
        NPRPC_LOG_ERROR("RpcHandle::run failed with unknown exception");
        return -2;
    }
    return 0;
}

void RpcHandle::start_thread_pool(size_t thread_count) noexcept {
    if (!initialized_ || !impl_) return;
    auto* impl = static_cast<RpcHandleImpl*>(impl_);
    impl->start_thread_pool(thread_count);
}

void RpcHandle::stop() noexcept {
    if (!initialized_ || !impl_) return;
    auto* impl = static_cast<RpcHandleImpl*>(impl_);
    impl->stop();
}

std::string RpcHandle::get_debug_info() const {
    std::ostringstream oss;
    oss << "RpcHandle { initialized: " << (initialized_ ? "true" : "false") << " }";
    return oss.str();
}

void* RpcHandle::create_poa(uint32_t max_objects, uint32_t lifespan, uint32_t id_policy) {
    if (!initialized_ || !impl_) return nullptr;

    try {
        auto* impl = static_cast<RpcHandleImpl*>(impl_);
        if (!impl->rpc_instance) return nullptr;

        auto builder = impl->rpc_instance->create_poa();

        if (max_objects > 0) {
            builder.with_max_objects(max_objects);
        }

        builder.with_lifespan(lifespan == 0 
            ? nprpc::PoaPolicy::Lifespan::Persistent 
            : nprpc::PoaPolicy::Lifespan::Transient);

        builder.with_object_id_policy(id_policy == 0
            ? nprpc::PoaPolicy::ObjectIdPolicy::SystemGenerated
            : nprpc::PoaPolicy::ObjectIdPolicy::UserSupplied);

        return builder.build();
    } catch (const std::exception& e) {
        NPRPC_LOG_ERROR("[SWB] RpcHandle::create_poa failed: {}", e.what());
        return nullptr;
    }
}

bool RpcHandle::add_to_host_json(
    const std::string& name,
    uint64_t object_id,
    uint16_t poa_idx,
    uint16_t flags,
    const uint8_t* origin,
    size_t origin_size,
    const std::string& class_id,
    const std::string& urls)
{
    if (!initialized_ || !impl_ || !origin || origin_size != 16) {
        return false;
    }

    try {
        auto* impl = static_cast<RpcHandleImpl*>(impl_);
        if (!impl->rpc_instance) return false;

        nprpc::ObjectId oid;
        auto& data = oid.get_data();
        data.object_id = object_id;
        data.poa_idx = poa_idx;
        data.flags = flags;
        std::copy(origin, origin + origin_size, data.origin.begin());
        data.class_id = class_id;
        data.urls = urls;

        impl->rpc_instance->add_to_host_json(name, oid);
        return true;
    } catch (const std::exception& e) {
        NPRPC_LOG_ERROR("[SWB] RpcHandle::add_to_host_json failed: {}", e.what());
        return false;
    } catch (...) {
        NPRPC_LOG_ERROR("[SWB] RpcHandle::add_to_host_json failed with unknown exception");
        return false;
    }
}

void RpcHandle::clear_host_json() {
    if (!initialized_ || !impl_) return;

    try {
        auto* impl = static_cast<RpcHandleImpl*>(impl_);
        if (!impl->rpc_instance) return;
        impl->rpc_instance->clear_host_json();
    } catch (const std::exception& e) {
        NPRPC_LOG_ERROR("[SWB] RpcHandle::clear_host_json failed: {}", e.what());
    } catch (...) {
        NPRPC_LOG_ERROR("[SWB] RpcHandle::clear_host_json failed with unknown exception");
    }
}

std::string RpcHandle::produce_host_json(const std::string& output_path) {
    if (!initialized_ || !impl_) {
        return {};
    }

    try {
        auto* impl = static_cast<RpcHandleImpl*>(impl_);
        if (!impl->rpc_instance) return {};
        return impl->rpc_instance->produce_host_json(output_path);
    } catch (const std::exception& e) {
        NPRPC_LOG_ERROR("[SWB] RpcHandle::produce_host_json failed: {}", e.what());
        return {};
    } catch (...) {
        NPRPC_LOG_ERROR("[SWB] RpcHandle::produce_host_json failed with unknown exception");
        return {};
    }
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

uint32_t nprpc_get_thread_id() {
    uint32_t id = nprpc::impl::get_thread_id() & 0xFFFFFFFF; // Ensure it fits in 32 bits
    return id;
}

const char* nprpc_get_thread_name() {
    return nprpc::impl::get_thread_name_cstr();
}

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

bool nprpc_object_select_endpoint_with_info(void* obj, uint32_t type, const char* hostname, uint16_t port) {
    if (!obj) return false;
    nprpc::EndPoint ep(static_cast<nprpc::EndPointType>(type), hostname, port);
    return static_cast<nprpc::Object*>(obj)->select_endpoint(ep);
}

// Raw EndPoint* accessor functions (for dispatch endpoint parameter)
uint32_t nprpc_endpoint_get_type(void* endpoint_ptr) {
    if (!endpoint_ptr) return 0;
    const auto* ep = static_cast<nprpc::EndPoint*>(endpoint_ptr);
    return static_cast<uint32_t>(ep->type());
}

const char* nprpc_endpoint_get_hostname(void* endpoint_ptr) {
    if (!endpoint_ptr) return "";
    const auto* ep = static_cast<nprpc::EndPoint*>(endpoint_ptr);
    // Copy to thread-local to ensure lifetime
    g_endpoint_hostname = std::string(ep->hostname());
    return g_endpoint_hostname.c_str();
}

uint16_t nprpc_endpoint_get_port(void* endpoint_ptr) {
    if (!endpoint_ptr) return 0;
    const auto* ep = static_cast<nprpc::EndPoint*>(endpoint_ptr);
    return ep->port();
}

// ObjectId accessor functions for Swift
// NOTE: These are for raw ObjectId* from nprpc_poa_activate_swift_servant
// Use nprpc_object_get_* for Object* from nprpc_create_object_from_components

uint64_t nprpc_objectid_get_object_id(void* ptr) {
    if (!ptr) return 0;
    auto* oid = static_cast<nprpc::ObjectId*>(ptr);
    return oid->object_id();
}

uint16_t nprpc_objectid_get_poa_idx(void* ptr) {
    if (!ptr) return 0;
    return static_cast<nprpc::ObjectId*>(ptr)->poa_idx();
}

uint16_t nprpc_objectid_get_flags(void* ptr) {
    if (!ptr) return 0;
    return static_cast<nprpc::ObjectId*>(ptr)->flags();
}

const char* nprpc_objectid_get_class_id(void* ptr) {
    if (!ptr) return nullptr;
    return static_cast<nprpc::ObjectId*>(ptr)->class_id().data();
}

const char* nprpc_objectid_get_urls(void* ptr) {
    if (!ptr) return nullptr;
    return static_cast<nprpc::ObjectId*>(ptr)->urls().data();
}

const uint8_t* nprpc_objectid_get_origin(void* ptr) {
    if (!ptr) return nullptr;
    return static_cast<nprpc::ObjectId*>(ptr)->origin().data();
}

void nprpc_objectid_destroy(void* oid_ptr) {
    delete static_cast<nprpc::ObjectId*>(oid_ptr);
}

// Object accessor functions for Swift
// NOTE: These are for Object* from nprpc_create_object_from_components
// Object has a vtable, so static_cast<Object*> properly handles inheritance

uint64_t nprpc_object_get_object_id(void* ptr) {
    if (!ptr) return 0;
    return static_cast<nprpc::Object*>(ptr)->object_id();
}

uint16_t nprpc_object_get_poa_idx(void* ptr) {
    if (!ptr) return 0;
    return static_cast<nprpc::Object*>(ptr)->poa_idx();
}

uint16_t nprpc_object_get_flags(void* ptr) {
    if (!ptr) return 0;
    return static_cast<nprpc::Object*>(ptr)->flags();
}

const char* nprpc_object_get_class_id(void* ptr) {
    if (!ptr) return nullptr;
    return static_cast<nprpc::Object*>(ptr)->class_id().data();
}

const char* nprpc_object_get_urls(void* ptr) {
    if (!ptr) return nullptr;
    return static_cast<nprpc::Object*>(ptr)->urls().data();
}

const uint8_t* nprpc_object_get_origin(void* ptr) {
    if (!ptr) return nullptr;
    return static_cast<nprpc::Object*>(ptr)->origin().data();
}

// Object async RPC call with callback - for Swift async/await integration
int nprpc_object_send_async(
    void* obj_ptr,
    void* buffer_ptr,
    void* context,
    swift_async_callback callback,
    uint32_t timeout_ms
) {
    if (!obj_ptr || !buffer_ptr || !callback) {
        return -1;  // Invalid arguments
    }

    auto* obj = static_cast<nprpc::Object*>(obj_ptr);
    auto* buffer = static_cast<nprpc::flat_buffer*>(buffer_ptr);

    // Ensure endpoint is selected
    if (obj->get_endpoint().empty()) {
        if (!obj->select_endpoint()) {
            return -2;  // Failed to select endpoint
        }
    }

    try {
        // Create completion handler that invokes the Swift callback
        auto handler = [context, callback](const boost::system::error_code& ec, nprpc::flat_buffer& buf) {
            if (ec) {
                std::string msg = ec.message();
                callback(context, -3, msg.c_str());
            } else {
                callback(context, 0, nullptr);
            }
        };

        nprpc::impl::rpc_call_async(
            obj->get_endpoint(),
            std::move(*buffer),
            std::move(handler),
            timeout_ms > 0 ? timeout_ms : obj->get_timeout()
        );
        return 0;  // Started successfully
    } catch (const std::exception& e) {
        NPRPC_LOG_ERROR("[SWB] nprpc_object_send_async exception: {}", e.what());
        return -3;  // Failed to start
    }
}

// Object async RPC call with response - for async methods with output parameters
int nprpc_object_send_async_receive(
    void* obj_ptr,
    void* buffer_ptr,
    void* context,
    swift_async_receive_callback callback,
    uint32_t timeout_ms
) {
    if (!obj_ptr || !buffer_ptr || !callback) {
        return -1;  // Invalid arguments
    }

    auto* obj = static_cast<nprpc::Object*>(obj_ptr);
    auto* buffer = static_cast<nprpc::flat_buffer*>(buffer_ptr);

    // Ensure endpoint is selected
    if (obj->get_endpoint().empty()) {
        if (!obj->select_endpoint()) {
            return -2;  // Failed to select endpoint
        }
    }

    try {
        // Create completion handler that invokes the Swift callback with response buffer
        auto handler = [context, callback](const boost::system::error_code& ec, nprpc::flat_buffer& buf) {
            if (ec) {
                std::string msg = ec.message();
                callback(context, -3, msg.c_str(), nullptr);
            } else {
                // Create a new flat_buffer with the response data and transfer ownership to Swift
                auto* response = new nprpc::flat_buffer(std::move(buf));
                callback(context, 0, nullptr, response);
            }
        };

        nprpc::impl::rpc_call_async(
            obj->get_endpoint(),
            std::move(*buffer),
            std::move(handler),
            timeout_ms > 0 ? timeout_ms : obj->get_timeout()
        );
        return 0;  // Started successfully
    } catch (const std::exception& e) {
        NPRPC_LOG_ERROR("[SWB] nprpc_object_send_async_receive exception: {}", e.what());
        return -3;  // Failed to start
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

// Create Object from ObjectId components
void* nprpc_create_object_from_components(
    uint64_t object_id,
    uint16_t poa_idx,
    uint16_t flags,
    const uint8_t* origin,
    const char* class_id,
    const char* urls)
{
    if (!origin || !class_id || !urls) {
        return nullptr;
    }

    try {
        // Create a temporary ObjectId with the data
        nprpc::ObjectId temp_oid;
        auto& data = temp_oid.get_data();
        data.object_id = object_id;
        data.poa_idx = poa_idx;
        data.flags = flags;
        std::memcpy(data.origin.data(), origin, 16);
        data.class_id = class_id;
        data.urls = urls;

        // Serialize to string and deserialize to Object
        // This uses the existing from_string infrastructure
        std::string oid_str = temp_oid.to_string();
        auto* obj = nprpc::Object::from_string(oid_str);
        return obj;
    } catch (const std::exception& e) {
        NPRPC_LOG_ERROR("[SWB] nprpc_create_object_from_components exception: {}", e.what());
        return nullptr;
    } catch (...) {
        return nullptr;
    }
}

// Create Object from flat buffer ObjectId with endpoint selection
// This is the Swift equivalent of C++ create_object_from_flat
// Takes raw buffer pointer and offset, creates temporary flat_buffer view
int nprpc_create_object_from_flat(
    void* buffer_ptr,
    uint32_t offset,
    uint32_t endpoint_type,
    const char* endpoint_hostname,
    uint16_t endpoint_port,
    void** out_object)
{
    if (!out_object) return -1;
    *out_object = nullptr;

    if (!buffer_ptr || !endpoint_hostname) {
        return -2;  // Invalid parameters
    }

    try {
        // Create a view-mode flat_buffer wrapping the raw pointer
        nprpc::flat_buffer buf(
            static_cast<std::uint8_t*>(buffer_ptr),
            // We kind of have to guess the size here since we only have a pointer and offset
            // FIXME: This is a bit hacky - we assume the ObjectId is within the next 64KB of the buffer
            // This is a vulnerability now, actually no, since a reasonable string length (class_id, urls)
            // has been checked in safety checks in Swift before calling this function, but still not ideal
            offset + 65535,  // size
            offset + 65535,  // max_size (read-only, same as size)
            nullptr          // no endpoint needed for reading
        );

        // Create ObjectId_Direct accessor on the stack
        nprpc::detail::flat::ObjectId_Direct direct(buf, offset);

        // Create the remote endpoint on the stack
        nprpc::EndPoint remote_endpoint(
            static_cast<nprpc::EndPointType>(endpoint_type),
            endpoint_hostname,
            endpoint_port
        );

        // Call the actual create_object_from_flat
        nprpc::Object* obj = nprpc::impl::create_object_from_flat(direct, remote_endpoint);

        if (!obj) {
            return 0;  // Success but null object (invalid_object_id)
        }

        *out_object = obj;
        return 1;  // Success with valid object
    } catch (const nprpc::Exception& e) {
        // This is thrown when endpoint selection fails
        NPRPC_LOG_ERROR("[SWB] nprpc_create_object_from_flat: {}", e.what());
        return -3;  // Cannot select endpoint
    } catch (const std::exception& e) {
        NPRPC_LOG_ERROR("[SWB] nprpc_create_object_from_flat exception: {}", e.what());
        return -4;  // Other exception
    } catch (...) {
        return -5;  // Unknown exception
    }
}

// ============================================================================
// POA operations
// ============================================================================

void* nprpc_rpc_create_poa(void* rpc_handle, uint32_t max_objects, uint32_t lifespan, uint32_t id_policy) {
    if (!rpc_handle) return nullptr;

    auto* handle = static_cast<nprpc_swift::RpcHandle*>(rpc_handle);
    return handle->create_poa(max_objects, lifespan, id_policy);
}

bool nprpc_rpc_add_to_host_json(
    void* rpc_handle,
    const char* name,
    uint64_t object_id,
    uint16_t poa_idx,
    uint16_t flags,
    const uint8_t* origin,
    size_t origin_size,
    const char* class_id,
    const char* urls)
{
    if (!rpc_handle || !name || !origin || !class_id || !urls) {
        return false;
    }

    auto* handle = static_cast<nprpc_swift::RpcHandle*>(rpc_handle);
    return handle->add_to_host_json(
        name,
        object_id,
        poa_idx,
        flags,
        origin,
        origin_size,
        class_id,
        urls);
}

void nprpc_rpc_clear_host_json(void* rpc_handle) {
    if (!rpc_handle) return;
    auto* handle = static_cast<nprpc_swift::RpcHandle*>(rpc_handle);
    handle->clear_host_json();
}

const char* nprpc_rpc_produce_host_json(void* rpc_handle, const char* output_path) {
    if (!rpc_handle) return nullptr;
    auto* handle = static_cast<nprpc_swift::RpcHandle*>(rpc_handle);
    const std::string result = handle->produce_host_json(output_path ? output_path : "");
    if (result.empty()) {
        return nullptr;
    }
    return nprpc_swift::copy_string_to_c_heap(result);
}

uint16_t nprpc_poa_get_index(void* poa_handle) {
    if (!poa_handle) return 0;
    return static_cast<nprpc::Poa*>(poa_handle)->get_index();
}

void nprpc_poa_deactivate_object(void* poa_handle, uint64_t object_id) {
    if (!poa_handle) return;
    static_cast<nprpc::Poa*>(poa_handle)->deactivate_object(object_id);
}

// Swift Servant Bridge
class SwiftServantBridge : public nprpc::ObjectServant {
public:
    // Updated dispatch function signature to include session context
    using DispatchFunc = void (*)(void*, void*, void*, void*, void*);

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
        // Pass rx_buffer, tx_buffer, endpoint, and session context (for streaming)
        if (dispatch_func_ && swift_servant_) {
            dispatch_func_(swift_servant_, ctx.rx_buffer, ctx.tx_buffer, &ctx.remote_endpoint, &ctx);
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
    uint32_t activation_flags,
    void (*dispatch_func)(void*, void*, void*, void*, void*),
    void* session_ctx)
{
    auto* poa = static_cast<nprpc::Poa*>(poa_handle);
    if (!poa || !swift_servant || !class_name || !dispatch_func) {
        return nullptr;
    }

    try {
        auto bridge = std::make_unique<SwiftServantBridge>(
            swift_servant, class_name, dispatch_func);

        auto* ctx = static_cast<nprpc::SessionContext*>(session_ctx);
        auto oid = poa->activate_object(bridge.release(),
            static_cast<nprpc::ObjectActivationFlags>(activation_flags), ctx);

        // Allocate and copy ObjectId to return to Swift
        // Swift will need to free this memory
        auto* oid_copy = new nprpc::ObjectId(oid);

        // Return pointer to ObjectId - Swift will read it and delete it
        return oid_copy;
    } catch (...) {
        return nullptr;
    }
}

// Activate a Swift servant in a POA with user-supplied object ID
void* nprpc_poa_activate_swift_servant_with_id(
    void* poa_handle,
    void* swift_servant,
    const char* class_name,
    uint32_t activation_flags,
    void (*dispatch_func)(void*, void*, void*, void*, void*),
    void* session_ctx,
    uint64_t object_id)
{
    auto* poa = static_cast<nprpc::Poa*>(poa_handle);
    if (!poa || !swift_servant || !class_name || !dispatch_func) {
        return nullptr;
    }

    try {
        auto bridge = std::make_unique<SwiftServantBridge>(
            swift_servant, class_name, dispatch_func);

        auto* ctx = static_cast<nprpc::SessionContext*>(session_ctx);
        auto oid = poa->activate_object_with_id(object_id, bridge.release(),
            static_cast<nprpc::ObjectActivationFlags>(activation_flags), ctx);

        // Allocate and copy ObjectId to return to Swift
        // Swift will need to free this memory
        auto* oid_copy = new nprpc::ObjectId(oid);

        // Return pointer to ObjectId - Swift will read it and delete it
        return oid_copy;
    } catch (...) {
        return nullptr;
    }
}

// ============================================================================
// Streaming operations
// ============================================================================

uint64_t nprpc_generate_stream_id() {
    return nprpc::impl::StreamManager::generate_stream_id();
}

void* nprpc_object_get_stream_manager(void* object_ptr) {
    auto* obj = static_cast<nprpc::Object*>(object_ptr);
    if (!obj) {
        NPRPC_LOG_WARN("[SWB] nprpc_object_get_stream_manager: object_ptr is null");
        return nullptr;
    }

    auto session = nprpc::impl::get_session_for_endpoint(obj->get_endpoint());
    if (!session || !session->ctx().stream_manager) {
        NPRPC_LOG_WARN("[SWB] nprpc_object_get_stream_manager: no session or stream_manager");
        return nullptr;
    }

    return session->ctx().stream_manager;
}

void* nprpc_object_get_session(void* object_ptr) {
    auto* obj = static_cast<nprpc::Object*>(object_ptr);
    if (!obj) {
        NPRPC_LOG_WARN("[SWB] nprpc_object_get_session: object_ptr is null");
        return nullptr;
    }

    if (obj->get_endpoint().empty()) {
        if (!obj->select_endpoint()) {
            NPRPC_LOG_WARN("[SWB] nprpc_object_get_session: failed to select endpoint");
            return nullptr;
        }
    }

    auto session = nprpc::impl::get_session_for_endpoint(obj->get_endpoint());
    if (!session) {
        NPRPC_LOG_WARN("[SWB] nprpc_object_get_session: session lookup failed");
        return nullptr;
    }

    return session.get();
}

void* nprpc_session_get_stream_manager(void* session_ptr) {
    auto* session = static_cast<nprpc::impl::Session*>(session_ptr);
    if (!session || !session->ctx().stream_manager) {
        NPRPC_LOG_WARN("[SWB] nprpc_session_get_stream_manager: no session or stream_manager");
        return nullptr;
    }

    return session->ctx().stream_manager;
}

void* nprpc_get_stream_manager(void* session_ctx) {
    auto* ctx = static_cast<nprpc::SessionContext*>(session_ctx);
    if (!ctx) {
        NPRPC_LOG_WARN("[SWB] nprpc_get_stream_manager: session_ctx is null");
        return nullptr;
    }
    return ctx->stream_manager;
}

void nprpc_stream_manager_send_chunk(
    void* stream_manager,
    uint64_t stream_id,
    const void* data,
    uint32_t data_size,
    uint64_t sequence)
{
    auto* mgr = static_cast<nprpc::impl::StreamManager*>(stream_manager);
    if (!mgr) {
        NPRPC_LOG_ERROR("[SWB] send_chunk: stream_manager is null");
        return;
    }

    std::span<const uint8_t> data_span(
        static_cast<const uint8_t*>(data),
        data_size
    );

    mgr->send_chunk(stream_id, data_span, sequence);
}

void nprpc_stream_manager_send_complete(void* stream_manager, uint64_t stream_id, uint64_t final_sequence) {
    auto* mgr = static_cast<nprpc::impl::StreamManager*>(stream_manager);
    if (!mgr) return;

    mgr->send_complete(stream_id, final_sequence);
}

void nprpc_stream_manager_send_cancel(void* stream_manager, uint64_t stream_id) {
    auto* mgr = static_cast<nprpc::impl::StreamManager*>(stream_manager);
    if (!mgr) return;

    mgr->send_cancel(stream_id);
}

void nprpc_stream_manager_send_error(
    void* stream_manager,
    uint64_t stream_id,
    uint32_t error_code,
    const void* error_data,
    uint32_t error_data_size)
{
    auto* mgr = static_cast<nprpc::impl::StreamManager*>(stream_manager);
    if (!mgr) return;

    std::span<const uint8_t> data_span(
        static_cast<const uint8_t*>(error_data),
        error_data_size
    );

    mgr->send_error(stream_id, error_code, data_span);
}

void nprpc_stream_manager_on_window_update(
    void* stream_manager,
    uint64_t stream_id,
    uint32_t credits)
{
    auto* mgr = static_cast<nprpc::impl::StreamManager*>(stream_manager);
    if (!mgr) return;
    mgr->on_window_update(stream_id, credits);
}

void nprpc_stream_manager_send_window_update(
    void* stream_manager,
    uint64_t stream_id,
    uint32_t credits)
{
    auto* mgr = static_cast<nprpc::impl::StreamManager*>(stream_manager);
    if (!mgr) return;
    mgr->send_window_update(stream_id, credits);
}

void nprpc_stream_manager_register_external_writer(
    void* stream_manager,
    uint64_t stream_id)
{
    auto* mgr = static_cast<nprpc::impl::StreamManager*>(stream_manager);
    if (!mgr) return;
    mgr->register_external_writer(stream_id);
}

void nprpc_stream_manager_write_chunk_async(
    void* stream_manager,
    uint64_t stream_id,
    const void* data,
    uint32_t data_size,
    uint64_t sequence,
    void* context,
    nprpc_write_callback callback)
{
    auto* mgr = static_cast<nprpc::impl::StreamManager*>(stream_manager);
    if (!mgr) {
        if (callback) callback(context);
        return;
    }

    std::span<const uint8_t> data_span(
        static_cast<const uint8_t*>(data),
        data_size);

    // write_chunk_or_queue checks the per-stream credits and either sends
    // immediately or parks the write until on_window_update() drains it.
    mgr->write_chunk_or_queue(
        stream_id,
        data_span,
        sequence,
        [ctx = context, cb = callback]() {
            if (cb) cb(ctx);
        });
}

// Legacy session context versions
void nprpc_stream_send_chunk(
    void* session_ctx,
    uint64_t stream_id,
    const void* data,
    uint32_t data_size,
    uint64_t sequence)
{
    auto* ctx = static_cast<nprpc::SessionContext*>(session_ctx);
    if (!ctx || !ctx->stream_manager) return;

    // Create a span from the data
    std::span<const uint8_t> data_span(
        static_cast<const uint8_t*>(data),
        data_size
    );

    ctx->stream_manager->send_chunk(stream_id, data_span, sequence);
}

void nprpc_stream_send_complete(void* session_ctx, uint64_t stream_id, uint64_t final_sequence) {
    auto* ctx = static_cast<nprpc::SessionContext*>(session_ctx);
    if (!ctx || !ctx->stream_manager) return;

    ctx->stream_manager->send_complete(stream_id, final_sequence);
}

void nprpc_stream_send_error(
    void* session_ctx,
    uint64_t stream_id,
    uint32_t error_code,
    const void* error_data,
    uint32_t error_data_size)
{
    auto* ctx = static_cast<nprpc::SessionContext*>(session_ctx);
    if (!ctx || !ctx->stream_manager) return;

    std::span<const uint8_t> data_span(
        static_cast<const uint8_t*>(error_data),
        error_data_size
    );

    ctx->stream_manager->send_error(stream_id, error_code, data_span);
}

// Swift stream reader bridge - stores Swift callbacks
struct SwiftStreamReader : public nprpc::StreamReaderBase {
    void* swift_context;
    void (*on_chunk_callback)(void*, const void*, uint32_t);
    void (*on_complete_callback)(void*);
    void (*on_error_callback)(void*, uint32_t);
    void (*on_destroy_callback)(void*);

    SwiftStreamReader(
        void* context,
        void (*on_chunk)(void*, const void*, uint32_t),
        void (*on_complete)(void*),
        void (*on_error)(void*, uint32_t),
        void (*on_destroy)(void*)
    ) : swift_context(context),
        on_chunk_callback(on_chunk),
        on_complete_callback(on_complete),
        on_error_callback(on_error),
        on_destroy_callback(on_destroy) {}

    ~SwiftStreamReader() override {
        if (swift_context && on_destroy_callback) {
            on_destroy_callback(swift_context);
        }
    }

    void on_chunk_received(nprpc::flat_buffer fb) override {
        // Extract data from StreamChunk
        // Chunk layout: Header (16) + stream_id (8) + sequence (8) + data vector header (8) + window_size (4)
        constexpr size_t header_size = 16;
        constexpr size_t chunk_offset = header_size;

        auto data = fb.data();
        if (data.size() < chunk_offset + 28) return;

        // Read data vector
        const uint8_t* chunk_ptr = static_cast<const uint8_t*>(data.data()) + chunk_offset;
        uint32_t data_rel_offset = *reinterpret_cast<const uint32_t*>(chunk_ptr + 16);
        uint32_t data_count = *reinterpret_cast<const uint32_t*>(chunk_ptr + 20);

        if (data_count > 0 && on_chunk_callback) {
            const void* data_ptr = chunk_ptr + 16 + data_rel_offset;
            on_chunk_callback(swift_context, data_ptr, data_count);
        }
    }

    void on_complete() override {
        if (on_complete_callback) {
            on_complete_callback(swift_context);
        }
    }

    void on_error(uint32_t error_code, nprpc::flat_buffer error_data) override {
        if (on_error_callback) {
            on_error_callback(swift_context, error_code);
        }
    }
};

void nprpc_stream_register_reader(
    void* object_ptr,
    uint64_t stream_id,
    void* context,
    void (*on_chunk)(void*, const void*, uint32_t),
    void (*on_complete)(void*),
    void (*on_error)(void*, uint32_t),
    void (*on_destroy)(void*))
{
    auto* obj = static_cast<nprpc::Object*>(object_ptr);
    if (!obj) {
        NPRPC_LOG_ERROR("[SWB] nprpc_stream_register_reader: object_ptr is null");
        if (context && on_destroy) {
            on_destroy(context);
        }
        return;
    }

    // Get session from object's endpoint
    auto session = nprpc::impl::get_session_for_endpoint(obj->get_endpoint());
    if (!session || !session->ctx().stream_manager) {
        NPRPC_LOG_ERROR("[SWB] nprpc_stream_register_reader: no session or stream_manager");
        if (context && on_destroy) {
            on_destroy(context);
        }
        return;
    }

    // Create and register the Swift reader bridge
    // Note: Memory ownership is transferred to stream_manager
    auto reader = std::make_unique<SwiftStreamReader>(context, on_chunk, on_complete, on_error, on_destroy);
    session->ctx().stream_manager->register_reader(stream_id, std::move(reader));
}

void nprpc_stream_set_reader_unreliable(
    void* object_ptr,
    uint64_t stream_id,
    bool unreliable)
{
    auto* obj = static_cast<nprpc::Object*>(object_ptr);
    if (!obj) {
        NPRPC_LOG_ERROR("[SWB] nprpc_stream_set_reader_unreliable: object_ptr is null");
        return;
    }

    auto session = nprpc::impl::get_session_for_endpoint(obj->get_endpoint());
    if (!session || !session->ctx().stream_manager) {
        NPRPC_LOG_ERROR("[SWB] nprpc_stream_set_reader_unreliable: no session or stream_manager");
        return;
    }

    session->ctx().stream_manager->set_reader_unreliable(stream_id, unreliable);
}

void nprpc_stream_manager_register_reader(
    void* stream_manager,
    uint64_t stream_id,
    void* context,
    void (*on_chunk)(void*, const void*, uint32_t),
    void (*on_complete)(void*),
    void (*on_error)(void*, uint32_t),
    void (*on_destroy)(void*))
{
    auto* mgr = static_cast<nprpc::impl::StreamManager*>(stream_manager);
    if (!mgr) {
        NPRPC_LOG_ERROR("[SWB] nprpc_stream_manager_register_reader: stream_manager is null");
        if (context && on_destroy) {
            on_destroy(context);
        }
        return;
    }

    auto reader = std::make_unique<SwiftStreamReader>(context, on_chunk, on_complete, on_error, on_destroy);
    mgr->register_reader(stream_id, std::move(reader));
}

void nprpc_stream_manager_set_reader_unreliable(
    void* stream_manager,
    uint64_t stream_id,
    bool unreliable)
{
    auto* mgr = static_cast<nprpc::impl::StreamManager*>(stream_manager);
    if (!mgr) {
        NPRPC_LOG_ERROR("[SWB] nprpc_stream_manager_set_reader_unreliable: stream_manager is null");
        return;
    }

    mgr->set_reader_unreliable(stream_id, unreliable);
}

void nprpc_stream_manager_defer_stream_start(
    void* stream_manager,
    uint64_t stream_id)
{
    auto* mgr = static_cast<nprpc::impl::StreamManager*>(stream_manager);
    if (!mgr) {
        NPRPC_LOG_ERROR("[SWB] nprpc_stream_manager_defer_stream_start: stream_manager is null");
        return;
    }

    mgr->defer_stream_start(stream_id);
}

int nprpc_stream_send_init(
    void* object_ptr,
    void* buffer_ptr,
    uint32_t timeout_ms)
{
    auto* obj = static_cast<nprpc::Object*>(object_ptr);
    if (!obj || !buffer_ptr) return -1;

    auto* session = static_cast<nprpc::impl::Session*>(nprpc_object_get_session(object_ptr));
    if (!session) {
        return -2;
    }

    return nprpc_session_stream_send_init(session, buffer_ptr, timeout_ms);
}

int nprpc_session_stream_send_init(
    void* session_ptr,
    void* buffer_ptr,
    uint32_t timeout_ms)
{
    auto* session = static_cast<nprpc::impl::Session*>(session_ptr);
    auto* buf = static_cast<nprpc::flat_buffer*>(buffer_ptr);
    if (!session || !buf) return -1;

    nprpc::impl::flat::StreamInit_Direct init(*buf, sizeof(nprpc::impl::Header));
    const auto stream_id = init.stream_id();

    try {
        session->send_receive(*buf, timeout_ms);
        const auto std_reply = nprpc::impl::handle_standart_reply(*buf);
        if (std_reply != 0) {
            return std_reply;
        }

        if (session->ctx().stream_manager) {
            session->ctx().stream_manager->defer_stream_start(stream_id);
            session->ctx().stream_manager->on_reply_sent();
        }

        return 0;
    } catch (...) {
        return -1;
    }
}

} // extern "C"
// ============================================================================
// HTTP Cookie bridge implementations
// ============================================================================

extern "C" {

// Thread-local buffer so the returned pointer stays valid until the next call.
static thread_local std::string g_cookie_value;

const char* nprpc_http_get_cookie(const char* name)
{
    if (!name) return nullptr;
    auto val = nprpc::http::get_cookie(name);
    if (!val.has_value()) return nullptr;
    g_cookie_value = std::move(*val);
    return g_cookie_value.c_str();
}

void nprpc_http_set_cookie(
    const char* name,
    const char* value,
    bool        http_only,
    bool        secure,
    const char* same_site,
    int32_t     max_age_seconds,
    const char* path,
    const char* domain)
{
    if (!name || !value) return;
    nprpc::http::CookieOptions opts;
    opts.http_only  = http_only;
    opts.secure     = secure;
    opts.same_site  = same_site  ? same_site  : "Strict";
    opts.max_age    = max_age_seconds >= 0
                        ? std::optional<int>(max_age_seconds)
                        : std::nullopt;
    opts.path       = (path   && path[0])   ? path   : "/";
    opts.domain     = (domain && domain[0]) ? domain : "";
    nprpc::http::set_cookie(name, value, opts);
}

void nprpc_http_clear_cookie(
    const char* name,
    const char* path,
    const char* domain)
{
    if (!name) return;
    nprpc::http::clear_cookie(
        name,
        (path   && path[0])   ? std::string_view(path)   : "/",
        (domain && domain[0]) ? std::string_view(domain) : "");
}

} // extern "C"
// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

// Bridge header for Swift C++ interop
// This header exposes a simplified C++ API that Swift can import directly

#pragma once

#include <cstdint>
#include <string>
#include <memory>
#include <vector>
#include <optional>

// Swift interop macros
#ifndef SWIFT_RETURNS_INDEPENDENT_VALUE
#define SWIFT_RETURNS_INDEPENDENT_VALUE __attribute__((swift_attr("returns_independent_value")))
#endif

// Forward declarations only - avoid pulling in heavy implementation headers
// Swift only needs to see the types, not the full implementation
namespace nprpc {
    class Rpc;
    class Poa;
    class Object;
    template<typename T> class ObjectPtr;
    class flat_buffer;

    // Forward declare LogLevel (defined in nprpc_base.hpp)
    enum class LogLevel : uint32_t;

    namespace impl {
        struct BuildConfig;
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
// ============================================================================
// BuildConfig - Replicates nprpc::impl::BuildConfig for Swift access
// We duplicate this struct to make it visible to Swift without exposing internal headers
// ============================================================================

struct RpcBuildConfig {
    uint32_t log_level = 2;  // LogLevel::info
    uint8_t uuid[16] = {};

    uint16_t tcp_port = 0;
    uint16_t udp_port = 0;
    std::string hostname;

    // HTTP/HTTPS + WebSocket/SSL WebSocket
    uint16_t http_port = 0;
    bool http_ssl_enabled = false;
    bool http3_enabled = false;
    bool ssr_enabled = false;
    bool http_ssl_client_disable_verification = false;
    std::string http_cert_file;
    std::string http_key_file;
    std::string http_dhparams_file;
    std::string http_root_dir;
    std::string ssr_handler_dir;

    // QUIC
    uint16_t quic_port = 0;
    std::string quic_cert_file;
    std::string quic_key_file;
    std::string ssl_client_self_signed_cert_path;
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
    /// @param config Configuration struct (pointer, can't use rvalue ref for Swift compat)
    /// @return true if successful, false on error
    bool initialize(RpcBuildConfig* config);

    /// Run the io_context (blocks until stopped)
    /// @return 0 - success, negative on error: -1 = not initialized, -2 = run failed
    int run();

    /// Start io_context in background threads
    /// @param thread_count Number of threads to run (must be > 0 to start thread pool)
    void start_thread_pool(size_t thread_count) noexcept;

    /// Stop the io_context
    void stop() noexcept;

    /// Check if initialized
    bool is_initialized() const { return initialized_; }

    /// Get debug info
    std::string get_debug_info() const;

    /// Create a POA
    /// @param max_objects Maximum number of objects (0 = default)
    /// @param lifespan 0 = persistent, 1 = transient
    /// @param id_policy 0 = system-generated, 1 = user-supplied
    /// @return Opaque pointer to nprpc::Poa or nullptr on error
    SWIFT_RETURNS_INDEPENDENT_VALUE
    void* create_poa(uint32_t max_objects, uint32_t lifespan, uint32_t id_policy);

private:
    bool initialized_ = false;
    void* impl_ = nullptr;  // Opaque pointer to RpcHandleImpl
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

// ============================================================================
// C Bridge Functions for Swift Interop
// ============================================================================

extern "C" {

// FlatBuffer operations
void* nprpc_flatbuffer_create();
void nprpc_flatbuffer_destroy(void* fb);
void* nprpc_flatbuffer_data(void* fb);
const void* nprpc_flatbuffer_cdata(void* fb);
size_t nprpc_flatbuffer_size(void* fb);
void nprpc_flatbuffer_prepare(void* fb, size_t n);
void nprpc_flatbuffer_commit(void* fb, size_t n);
void nprpc_flatbuffer_consume(void* fb, size_t n);

// Object operations
void nprpc_object_release(void* obj);
uint32_t nprpc_object_add_ref(void* obj);
uint32_t nprpc_object_get_timeout(void* obj);
uint32_t nprpc_object_set_timeout(void* obj, uint32_t timeout_ms);

// Object endpoint accessors
uint32_t nprpc_object_get_endpoint_type(void* obj);  // Returns EndPointType as uint32_t
const char* nprpc_object_get_endpoint_hostname(void* obj);  // Returns hostname string
uint16_t nprpc_object_get_endpoint_port(void* obj);  // Returns port number
bool nprpc_object_select_endpoint(void* obj);  // Select best endpoint
bool nprpc_object_select_endpoint_with_info(void* obj, uint32_t type, const char* hostname, uint16_t port);  // Select endpoint with hint

// Raw EndPoint* accessors (for dispatch endpoint parameter)
uint32_t nprpc_endpoint_get_type(void* endpoint_ptr);  // Returns EndPointType as uint32_t
const char* nprpc_endpoint_get_hostname(void* endpoint_ptr);  // Returns hostname string (temp memory)
uint16_t nprpc_endpoint_get_port(void* endpoint_ptr);  // Returns port number

// ObjectId accessor functions (for raw ObjectId* from poa_activate)
uint64_t nprpc_objectid_get_object_id(void* oid_ptr);
uint16_t nprpc_objectid_get_poa_idx(void* oid_ptr);
uint16_t nprpc_objectid_get_flags(void* oid_ptr);
const char* nprpc_objectid_get_class_id(void* oid_ptr);
const char* nprpc_objectid_get_urls(void* oid_ptr);
const uint8_t* nprpc_objectid_get_origin(void* oid_ptr);
void nprpc_objectid_destroy(void* oid_ptr);

// Object accessor functions (for Object* from create_object_from_components)
// These handle the vtable offset properly
uint64_t nprpc_object_get_object_id(void* obj_ptr);
uint16_t nprpc_object_get_poa_idx(void* obj_ptr);
uint16_t nprpc_object_get_flags(void* obj_ptr);
const char* nprpc_object_get_class_id(void* obj_ptr);
const char* nprpc_object_get_urls(void* obj_ptr);
const uint8_t* nprpc_object_get_origin(void* obj_ptr);

// Object RPC call - sends request and receives reply via C++ runtime
// Returns: 0 = success, -1 = null args, -2 = endpoint selection failed, -3 = RPC call failed
int nprpc_object_send_receive(void* obj_ptr, void* buffer_ptr, uint32_t timeout_ms);

// Callback type for async RPC completion (fire-and-forget)
// context: user-provided context (e.g., boxed Swift continuation)
// error_code: 0 = success, negative = error
// error_message: error description (null on success), valid only during callback
typedef void (*swift_async_callback)(void* context, int error_code, const char* error_message);

// Callback type for async RPC with response
// context: user-provided context (e.g., boxed Swift continuation)
// error_code: 0 = success, negative = error
// error_message: error description (null on success), valid only during callback
// response_buffer: FlatBuffer handle containing the response (null on error)
//                  Ownership is transferred to callback - must be freed with nprpc_flatbuffer_destroy
typedef void (*swift_async_receive_callback)(void* context, int error_code, const char* error_message, void* response_buffer);

// Object async RPC call with callback - for Swift async/await integration
// Used for async methods in IDL
// callback will be invoked when the operation completes (on success or failure)
// Returns: 0 = started successfully, negative = failed to start (callback not invoked)
int nprpc_object_send_async(
    void* obj_ptr,
    void* buffer_ptr,
    void* context,
    swift_async_callback callback,
    uint32_t timeout_ms
);

// Object async RPC call with response - for Swift async/await integration
// Used for async methods that have output parameters
// callback will be invoked when the operation completes with the response buffer
// Returns: 0 = started successfully, negative = failed to start (callback not invoked)
int nprpc_object_send_async_receive(
    void* obj_ptr,
    void* buffer_ptr,
    void* context,
    swift_async_receive_callback callback,
    uint32_t timeout_ms
);

// Object string serialization (NPRPC IOR format)
// Returns: newly allocated string that caller must free with nprpc_free_string()
const char* nprpc_object_to_string(void* obj_ptr);

// Create Object from serialized string (NPRPC IOR format)
// Returns: new Object handle or nullptr on parse error
void* nprpc_object_from_string(const char* str);

// Free a string allocated by nprpc_object_to_string
void nprpc_free_string(const char* str);

// Create an Object from ObjectId components (for local proxy creation)
// Parameters:
//   object_id: unique object ID within POA
//   poa_idx: POA index
//   flags: object flags
//   origin: 16-byte UUID of origin process
//   class_id: class identifier string
//   urls: semicolon-separated endpoint URLs
// Returns: new Object handle or nullptr on error
void* nprpc_create_object_from_components(
    uint64_t object_id,
    uint16_t poa_idx,
    uint16_t flags,
    const uint8_t* origin,
    const char* class_id,
    const char* urls);

// Create an Object from flat buffer ObjectId with endpoint selection
// This is the Swift equivalent of C++ create_object_from_flat
// Parameters:
//   buffer_ptr: raw pointer to the flat buffer data
//   offset: offset within buffer where ObjectId starts
//   endpoint_type: EndPointType as uint32_t
//   endpoint_hostname: hostname string
//   endpoint_port: port number
//   out_object: output parameter for the created Object handle
// Returns: 1 = success with valid object, 0 = success but null object, negative = error
int nprpc_create_object_from_flat(
    void* buffer_ptr,
    uint32_t offset,
    uint32_t endpoint_type,
    const char* endpoint_hostname,
    uint16_t endpoint_port,
    void** out_object);

// ============================================================================
// POA operations
// ============================================================================

// Create a POA from RpcHandle
// Parameters:
//   rpc_handle: pointer to RpcHandle
//   max_objects: maximum number of objects (0 = unlimited)
//   lifespan: 0 = persistent, 1 = transient
//   id_policy: 0 = system-generated, 1 = user-supplied
// Returns: opaque pointer to nprpc::Poa or nullptr on error
void* nprpc_rpc_create_poa(void* rpc_handle, uint32_t max_objects, uint32_t lifespan, uint32_t id_policy);

// Get POA index
uint16_t nprpc_poa_get_index(void* poa_handle);

// Deactivate an object in a POA
void nprpc_poa_deactivate_object(void* poa_handle, uint64_t object_id);

// Swift Servant activation
// Returns pointer to nprpc::ObjectId that Swift must destroy with nprpc_objectid_destroy
// session_ctx: optional SessionContext* for session-specific activation (can be nullptr)
void* nprpc_poa_activate_swift_servant(
    void* poa_handle,
    void* swift_servant,
    const char* class_name,
    uint32_t activation_flags,
    void (*dispatch_func)(void*, void*, void*, void*, void*),
    void* session_ctx);

// ============================================================================
// Streaming operations
// ============================================================================

// Generate a unique stream ID
uint64_t nprpc_generate_stream_id();

// Get stream_manager pointer from session context (for use in async Tasks)
// The stream_manager pointer remains valid for the lifetime of the session
void* nprpc_get_stream_manager(void* session_ctx);

// Send a stream chunk via stream_manager (server -> client)
// Parameters:
//   stream_manager: StreamManager* from nprpc_get_stream_manager
//   stream_id: stream identifier
//   data: chunk data pointer
//   data_size: size of data in bytes
//   sequence: sequence number
void nprpc_stream_manager_send_chunk(
    void* stream_manager,
    uint64_t stream_id,
    const void* data,
    uint32_t data_size,
    uint64_t sequence);

// Send stream completion via stream_manager (server -> client)
void nprpc_stream_manager_send_complete(void* stream_manager, uint64_t stream_id, uint64_t final_sequence);

// Send stream error via stream_manager (server -> client)
void nprpc_stream_manager_send_error(
    void* stream_manager,
    uint64_t stream_id,
    uint32_t error_code,
    const void* error_data,
    uint32_t error_data_size);

// Legacy: Send a stream chunk via session context (deprecated - use stream_manager versions)
// Parameters:
//   session_ctx: SessionContext* from dispatch
//   stream_id: stream identifier
//   data: chunk data pointer
//   data_size: size of data in bytes
//   sequence: sequence number
void nprpc_stream_send_chunk(
    void* session_ctx,
    uint64_t stream_id,
    const void* data,
    uint32_t data_size,
    uint64_t sequence);

// Send stream completion (server -> client)
void nprpc_stream_send_complete(void* session_ctx, uint64_t stream_id, uint64_t final_sequence);

// Send stream error (server -> client)
void nprpc_stream_send_error(
    void* session_ctx,
    uint64_t stream_id,
    uint32_t error_code,
    const void* error_data,
    uint32_t error_data_size);

// Register a stream reader to receive chunks (client side)
// Parameters:
//   object_ptr: Object* to get session from
//   stream_id: stream identifier
//   context: opaque Swift context
//   on_chunk: callback when chunk received (context, data_ptr, data_size)
//   on_complete: callback when stream completes (context)
//   on_error: callback when error occurs (context, error_code)
void nprpc_stream_register_reader(
    void* object_ptr,
    uint64_t stream_id,
    void* context,
    void (*on_chunk)(void*, const void*, uint32_t),
    void (*on_complete)(void*),
    void (*on_error)(void*, uint32_t));

// Send StreamInit message (client -> server)
// Returns 0 on success, error code on failure
int nprpc_stream_send_init(
    void* object_ptr,
    void* buffer_ptr,
    uint32_t timeout_ms);

} // extern "C"

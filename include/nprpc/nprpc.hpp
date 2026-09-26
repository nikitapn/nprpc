// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include "export.hpp"

#include <initializer_list>
#include <array>
#include <atomic>
#include <chrono>
#include <exception>
#include <functional>
#include <optional>
#include <stop_token>
#include <string_view>
#include <string>
#include <vector>
#include <utility>

#include <boost/asio/io_context.hpp>
#include <boost/core/exchange.hpp>

#include <nprpc/basic.hpp>
#include <nprpc/common.hpp>
#include <nprpc/endpoint.hpp>
#include <nprpc/flat_buffer.hpp>
#include <nprpc/object_ptr.hpp>
#include <nprpc/page_handler.hpp>
#include <nprpc/task.hpp>
#include <nprpc/serialization/serialization.h>
#include <nprpc/session_context.h>
#include <nprpc/utils.hpp>
#include <nprpc/config_default.hpp>
#include <nprpc_base_ext.hpp>

namespace nprpc {

class Rpc;
namespace common {
class Nameserver;
}
class Poa;
class ObjectServant;
class Object;

/// Object id that names no object, e.g. a failed nameserver lookup.
constexpr oid_t invalid_object_id = std::numeric_limits<oid_t>::max();

/**
 * @brief Re-read the configured TLS certificate and key files
 *
 * Applies to every listener that terminates TLS with them: HTTP/1.1 +
 * WebSocket, HTTP/3, and the QUIC RPC listener.  Connections already
 * established keep the certificate they handshook with; new handshakes use
 * the reloaded one.
 *
 * A subsystem whose files fail to load keeps its previous certificate rather
 * than dropping to none, so a half-written renewal cannot take the server
 * down.
 *
 * Safe to call from any thread while the server is running.  It reads files
 * and allocates, so call it from a normal thread (e.g. an asio signal_set
 * handler), never from a raw POSIX signal handler.
 *
 * @return true if every configured subsystem reloaded successfully
 */
NPRPC_API bool reload_certificates();

namespace impl {

class RpcImpl;
class PoaImpl;
class ObjectGuard;

NPRPC_API Object* create_object_from_flat(detail::flat::ObjectId_Direct oid,
                                          EndPoint remote_endpoint);
} // namespace impl

/// A reference to an object: its id, where it lives, and the URLs it can be
/// reached at. `Object` adds a connection to it.
///
/// Converts to a string (`to_string()` / `from_string()`) so a reference
/// can be handed out of band, e.g. in a config file.
class ObjectId
{
  friend impl::PoaImpl;

protected:
  ::nprpc::detail::ObjectId data_;

public:
  /// Writes or reads the fields through `ar`; used for host.json.
  template <typename Archive> void serialize(Archive& ar)
  {
    ar& NVP2("object_id", data_.object_id);
    ar& NVP2("poa_idx", data_.poa_idx);
    ar& NVP2("flags", data_.flags);
    ar& NVP2("origin", data_.origin);
    ar& NVP2("class_id", data_.class_id);
    ar& NVP2("urls", data_.urls);
  }

  /// Copies from the wire form, as received in a call.
  void assign_from_direct(const detail::flat::ObjectId_Direct& other)
  {
    data_ = nprpc::detail::helpers::ObjectId::from_flat(
        const_cast<detail::flat::ObjectId_Direct&>(other));
  }

  /// Copies `oid` into its wire form, to send it in a call.
  static void assign_to_direct(const ::nprpc::ObjectId& oid,
                               detail::flat::ObjectId_Direct& direct)
  {
    nprpc::detail::helpers::ObjectId::to_flat(direct, oid.data_);
  }

  /// The object's id within its POA.
  auto object_id() const noexcept { return data_.object_id; }
  /// Index of the POA the object lives in.
  auto poa_idx() const noexcept { return data_.poa_idx; }
  /// `detail::ObjectFlag` bits: lifespan, and which browser transports may
  /// reach the object.
  auto flags() const noexcept { return data_.flags; }
  /// UUID of the process that created the object.
  const auto& origin() const noexcept { return data_.origin; }
  /// Interface identifier, `<idl file>/<namespace>.<interface>`.
  const auto& class_id() const noexcept { return data_.class_id; }
  /// Where the object can be reached, `;`-separated
  /// (`tcp://`, `web://`, `mem://`, `quic://`).
  const auto& urls() const noexcept { return data_.urls; }

  /// Whether the object was created by the process with UUID `other`.
  bool is_same_origin(const uuid_t& other) const noexcept
  {
    return origin() == other;
  }

  /// The underlying record, for code that builds references by hand.
  auto& get_data() noexcept { return data_; }
  /// The underlying record.
  const auto& get_data() const noexcept { return data_; }

  /// Serialize ObjectId to a string (NPRPC IOR format).
  /// Format: `NPRPC1:<base64_encoded_binary_data>`
  NPRPC_API std::string to_string() const;

  /// Deserialize ObjectId from string.
  /// Returns true on success, false on parse error.
  NPRPC_API bool from_string(std::string_view str);
};

/// Policies a POA is created with; see `PoaBuilder`.
namespace PoaPolicy {
/// How long a POA's objects live.
enum class Lifespan {
  /// Reference counted: deleted when the last client releases it.
  Transient = 0,
  /// Lives until deactivated explicitly, whatever clients hold.
  Persistent = 1
};

/// Who picks object ids.
enum class ObjectIdPolicy {
  /// The POA assigns ids; use `Poa::activate_object`.
  SystemGenerated = 0,
  /// The caller supplies them; use `Poa::activate_object_with_id`.
  UserSupplied = 1
};

/// Where servant dispatch may run relative to the transport I/O thread
/// (SHM ring consumer, TCP read loop, etc.).
enum class TransportAffinity {
  /// Prefer the transport thread when there is no DispatchExecutor.
  /// Best for extreme low-latency servants that only do cheap work.
  /// Default.
  AllowBlockTransport = 0,
  /// Never run dispatch on the transport thread: always hand off
  /// (even without a custom DispatchExecutor). Use for servants that
  /// might block, allocate heavily, or touch UI — keeps the ring/read
  /// path free.
  NeverBlockTransport = 1,
};
} // namespace PoaPolicy

/**
 * @brief Optional dispatch executor for servant callbacks / SHM offload.
 *
 * Two uses:
 *  1. **Fire-and-forget hop (preferred for SHM UI POAs):** the ring thread
 *     calls @c post(ctx, work, arg) to run a full handle_request+reply job
 *     on the target queue (Swift: DispatchQueue.async / dispatch_async_f).
 *     No Asio hop, no blocking the ring.
 *  2. **invoke_sync:** post+wait when a caller is not already on the queue.
 *     With @c is_running_on set, work already on the queue runs inline
 *     (avoids nested async / deadlock).
 *
 * Default (all null) = no custom executor; transport affinity decides
 * ring-inline vs Asio offload.
 *
 * Swift / Foundation example for DispatchQueue.main:
 *   post:           dispatch_async_f(queue, arg, fn)  or  queue.async { fn(arg) }
 *   is_running_on:  Thread.isMainThread / queue-specific key
 */
struct DispatchExecutor {
  /// A unit of work: call with its argument.
  using WorkFn = void (*)(void* arg);
  /// Schedule `fn(arg)` on the executor identified by `ctx`.
  using PostFn = void (*)(void* ctx, WorkFn fn, void* arg);
  /// Return true if the calling thread is already the executor's thread.
  using IsRunningOnFn = bool (*)(void* ctx);

  /// Schedules `fn(arg)` on the executor and returns without waiting.
  PostFn post = nullptr;
  /// Optional; lets `invoke_sync` run inline instead of deadlocking.
  IsRunningOnFn is_running_on = nullptr;
  /// Passed back to `post` and `is_running_on`.
  void* ctx = nullptr;

  /// Whether an executor is set.
  explicit operator bool() const noexcept { return post != nullptr; }

  /**
   * @brief Run @p fn on this executor; blocks until it finishes.
   *
   * If the executor is empty or @c is_running_on reports the current
   * thread, @p fn runs inline. Exceptions thrown by @p fn are rethrown
   * on the calling thread.
   *
   * Prefer fire-and-forget @c post from the SHM ring for whole requests;
   * use this when already inside a stack that must wait for the result.
   */
  template <typename F>
  void invoke_sync(F&& fn) const
  {
    if (!post || (is_running_on && is_running_on(ctx))) {
      std::forward<F>(fn)();
      return;
    }

    struct State {
      std::function<void()> work;
      std::atomic<bool> done{false};
      std::exception_ptr eptr;
    };

    auto* st = new State{std::function<void()>(std::forward<F>(fn))};
    post(
        ctx,
        [](void* p) {
          auto* s = static_cast<State*>(p);
          try {
            s->work();
          } catch (...) {
            s->eptr = std::current_exception();
          }
          s->done.store(true, std::memory_order_release);
          s->done.notify_one();
        },
        st);

    st->done.wait(false);
    auto eptr = st->eptr;
    delete st;
    if (eptr)
      std::rethrow_exception(eptr);
  }
};

/// Configures a POA; get one from `Rpc::create_poa()` and finish with
/// `build()`.
///
/// ```cpp
/// auto poa = rpc->create_poa()
///                .with_max_objects(64)
///                .with_lifespan(nprpc::PoaPolicy::Lifespan::Persistent)
///                .build();
/// ```
class NPRPC_API PoaBuilder
{
  uint32_t objects_max_ = 32;
  PoaPolicy::Lifespan lifespan_policy_ = PoaPolicy::Lifespan::Transient;
  PoaPolicy::ObjectIdPolicy object_id_policy_ =
      PoaPolicy::ObjectIdPolicy::SystemGenerated;
  PoaPolicy::TransportAffinity transport_affinity_ =
      PoaPolicy::TransportAffinity::AllowBlockTransport;
  DispatchExecutor dispatch_executor_{};
  Rpc* rpc_ = nullptr;

public:
  /// Prefer `Rpc::create_poa()`.
  explicit PoaBuilder(Rpc* rpc)
      : rpc_(rpc)
  {
  }

  /// Maximum number of objects active at once. Default: 32.
  PoaBuilder& with_max_objects(uint32_t max)
  {
    objects_max_ = max;
    return *this;
  }

  /// Whether objects are reference counted or live until deactivated.
  /// Default: `Transient`.
  PoaBuilder& with_lifespan(PoaPolicy::Lifespan policy)
  {
    lifespan_policy_ = policy;
    return *this;
  }

  /// Whether the POA assigns object ids or the caller does.
  /// Default: `SystemGenerated`.
  PoaBuilder& with_object_id_policy(PoaPolicy::ObjectIdPolicy policy)
  {
    object_id_policy_ = policy;
    return *this;
  }

  /// Prefer / forbid servant dispatch on the transport (e.g. SHM ring) thread.
  PoaBuilder& with_transport_affinity(PoaPolicy::TransportAffinity affinity)
  {
    transport_affinity_ = affinity;
    return *this;
  }

  /// Route servant dispatch through @p ex (post+wait). Empty = inline.
  /// Setting a non-empty executor implies off-transport dispatch for SHM
  /// (same effect as NeverBlockTransport for the ring hand-off decision).
  PoaBuilder& with_dispatch_executor(DispatchExecutor ex)
  {
    dispatch_executor_ = ex;
    return *this;
  }

  /// Creates the POA. The runtime owns it: do not delete it, call
  /// `Rpc::destroy_poa` to remove it.
  Poa* build();
};

/// Portable Object Adapter: holds servants and routes incoming calls to
/// them. Create one with `Rpc::create_poa()`; see the POA guide for the
/// threading options.
class NPRPC_API Poa
{
  poa_idx_t idx_;
  DispatchExecutor dispatch_executor_{};
  PoaPolicy::TransportAffinity transport_affinity_ =
      PoaPolicy::TransportAffinity::AllowBlockTransport;

public:
  /// Optional executor used for servant dispatch (empty = transport thread).
  const DispatchExecutor& dispatch_executor() const noexcept
  {
    return dispatch_executor_;
  }

  /// Replaces the executor set by `PoaBuilder::with_dispatch_executor`.
  void set_dispatch_executor(DispatchExecutor ex) noexcept
  {
    dispatch_executor_ = ex;
  }

  /// Whether dispatch may run on the transport thread.
  PoaPolicy::TransportAffinity transport_affinity() const noexcept
  {
    return transport_affinity_;
  }

  /// Replaces the affinity set by `PoaBuilder::with_transport_affinity`.
  void set_transport_affinity(PoaPolicy::TransportAffinity a) noexcept
  {
    transport_affinity_ = a;
  }

  /// True when transport layers should not run this POA's dispatch inline
  /// (custom executor and/or NeverBlockTransport).
  bool requires_off_transport_dispatch() const noexcept
  {
    return static_cast<bool>(dispatch_executor_) ||
           transport_affinity_ ==
               PoaPolicy::TransportAffinity::NeverBlockTransport;
  }
  /**
   * @brief Activate an object servant in this POA.
   * @param obj The object servant to activate.
   * @param activation_flags Flags that control how the object is activated.
   * @param ctx Optional session context for session-specific activation.
   * @return The object ID of the activated object.
   * @throws std::runtime_error if the object cannot be activated.
   */
  virtual ObjectId activate_object(ObjectServant* obj,
                                   ObjectActivationFlags activation_flags,
                                   SessionContext* ctx = nullptr) = 0;

  /**
   * @brief Activate an object servant with a user-supplied object ID.
   * Available only when POA is configured with ObjectIdPolicy::UserSupplied.
   */
  virtual ObjectId activate_object_with_id(oid_t object_id,
                                           ObjectServant* obj,
                                           ObjectActivationFlags activation_flags,
                                           SessionContext* ctx = nullptr) = 0;

  /**
   * @brief Deactivate an object servant in this POA.
   * @param object_id The ID of the object to deactivate.
   * @throws std::runtime_error if the object cannot be deactivated.
   */
  virtual void deactivate_object(oid_t object_id) = 0;

  /// This POA's index within the process.
  poa_idx_t get_index() const noexcept { return idx_; }

  /// Created by the runtime; see `PoaBuilder`.
  Poa(poa_idx_t idx)
      : idx_{idx}
  {
  }

  // Poa lifetime is managed by RpcImpl, so no need to delete it manually via
  // delete or wrap Poa in a smart pointer.
  virtual ~Poa() = default;
};

/// Base of every server-side object. npidl generates `I<Interface>_Servant`
/// from it; implement the interface's methods in a subclass and activate an
/// instance with `Poa::activate_object`.
class ObjectServant
{
  friend impl::PoaImpl;
  friend impl::ObjectGuard;

  std::shared_ptr<Poa> poa_;
  oid_t object_id_;
  std::atomic_uint32_t ref_cnt_{0};
  std::atomic_uint32_t in_use_cnt_{0};
  std::atomic_bool to_delete_{false};
  std::chrono::system_clock::time_point activation_time_;
  SessionContext* session_ctx_ = nullptr;

public:
  /// Interface identifier; generated.
  virtual std::string_view get_class() const noexcept = 0;
  /// Decodes a call and invokes the matching method; generated.
  virtual void dispatch(::nprpc::SessionContext& ctx, bool from_parent) = 0;
  /// Called when the runtime is done with the servant. Deletes it by
  /// default; override for servants that are not heap-allocated.
  virtual void destroy() noexcept { delete this; }

  /// The POA the servant is active in.
  Poa* poa() const noexcept { return poa_.get(); }
  /// The servant's object id.
  oid_t oid() const noexcept { return object_id_; }
  /// Index of the POA the servant is active in.
  poa_idx_t poa_index() const noexcept { return poa_->get_index(); }
  /// Adds a client reference. Returns the new count.
  NPRPC_API uint32_t add_ref() noexcept;
  /// Drops a client reference. In a transient POA the servant is
  /// deactivated and destroyed when the count reaches zero; in a persistent
  /// one this does nothing. Returns the new count.
  NPRPC_API uint32_t release() noexcept;
  /// When the servant was activated.
  auto activation_time() const noexcept { return activation_time_; }
  /// Whether no client holds a reference.
  bool is_unused() const noexcept { return ref_cnt_.load() == 0; }
  /// Whether the servant has been deactivated and awaits deletion.
  bool is_deleted() const noexcept { return to_delete_.load(); }
  /// Whether `ctx` may call the servant: always, unless it was activated
  /// for one session only.
  bool validate_session(SessionContext& ctx) const noexcept
  {
    return (!session_ctx_ || session_ctx_ == &ctx);
  }
  virtual ~ObjectServant() = default;
};

/// Client-side proxy for a remote object. npidl generates a subclass per
/// interface with a method for each call.
///
/// Reference counted with `add_ref()`/`release()`; hold it in an
/// `ObjectPtr` rather than calling them by hand.
class Object : public ObjectId
{
  friend impl::RpcImpl;
  template <typename T> friend T* narrow(Object*&) noexcept;
  friend NPRPC_API Object*
  impl::create_object_from_flat(detail::flat::ObjectId_Direct oid,
                                EndPoint remote_endpoint);

  std::atomic_uint32_t local_ref_cnt_ = 0;
  uint32_t timeout_ms_ = 1000;
  EndPoint endpoint_;
  // Soft preference applied by select_endpoint() when the transport is
  // present in urls() (and eligible, e.g. SHM only on the same machine).
  // nullopt = use the default preference order.
  std::optional<EndPointType> preferred_transport_;

public:
  /// Interface identifier; same as `class_id()`.
  std::string_view get_class() const noexcept { return class_id(); };

  /// Lifespan of the POA the object lives in.
  PoaPolicy::Lifespan policy_lifespan() const noexcept
  {
    return static_cast<PoaPolicy::Lifespan>(
        flags() & std::underlying_type_t<detail::ObjectFlag>(
                      detail::ObjectFlag::Persistent));
  }

  /// Adds a local reference. The first one on a transient object also
  /// registers this client with the server. Returns the new count.
  NPRPC_API uint32_t add_ref();
  /// Drops a local reference. The last one tells a transient object's server
  /// that this client is done, then deletes the proxy. Returns the new count.
  NPRPC_API uint32_t release();
  /// Picks the transport to call through from `urls()`, honouring
  /// `set_preferred_transport()`, or uses `remote_endpoint` when given.
  /// Returns false if none of the URLs is usable here.
  NPRPC_API bool select_endpoint(
      std::optional<EndPoint> remote_endpoint = std::nullopt) noexcept;

  /// Prefer a specific transport when select_endpoint() runs.
  /// Non-binding: if the transport is not advertised in urls() (or is
  /// ineligible, e.g. SharedMemory to a remote machine), the default
  /// preference order is used instead. Pass std::nullopt to clear.
  void set_preferred_transport(
      std::optional<EndPointType> type) noexcept
  {
    preferred_transport_ = type;
  }

  /// The transport set with `set_preferred_transport()`, if any.
  std::optional<EndPointType> preferred_transport() const noexcept
  {
    return preferred_transport_;
  }

  /// Sets how long a call waits for its reply before raising
  /// `ExceptionTimeout`. Returns the previous value. Default: 1000 ms.
  uint32_t set_timeout(uint32_t timeout_ms) noexcept
  {
    return boost::exchange(timeout_ms_, timeout_ms);
  }

  /// Call timeout in milliseconds.
  uint32_t get_timeout() const noexcept { return timeout_ms_; }

  /// The transport and address calls go to; see `select_endpoint()`.
  const EndPoint& get_endpoint() const noexcept { return endpoint_; }

  /// Create an Object from a serialized string (NPRPC IOR format).
  /// Returns nullptr on parse error.
  NPRPC_API static Object* from_string(std::string_view str);

  NPRPC_API virtual ~Object() = default;

  Object(const Object&) = delete;
  Object& operator=(const Object&) = delete;

  Object(Object&&) = delete;
  /// Takes over `other`'s reference, connection and settings; used by
  /// `narrow()`.
  Object& operator=(Object&& other)
  {
    if (this != &other) {
      data_ = std::move(other.data_);
      local_ref_cnt_ = other.local_ref_cnt_.load();
      timeout_ms_ = other.timeout_ms_;
      endpoint_ = std::move(other.endpoint_);
      preferred_transport_ = other.preferred_transport_;
      other.timeout_ms_ = other.timeout_ms_;
      other.preferred_transport_ = std::nullopt;
    }
    return *this;
  }

protected:
  Object() = default;
};

/// The runtime: listeners, sessions and POAs. Create it with `RpcBuilder`.
///
/// ```cpp
/// auto rpc = nprpc::RpcBuilder().with_tcp(15000).build();
/// auto poa = rpc->create_poa().build();
/// auto oid = poa->activate_object(new CalculatorImpl(),
///                                 nprpc::ObjectActivationFlags::tcp);
/// rpc->run();
/// ```
class NPRPC_API Rpc
{
public:
  /// Starts configuring a new POA.
  PoaBuilder create_poa() { return PoaBuilder(this); }
  /// Removes a POA created by `PoaBuilder::build()`.
  virtual void destroy_poa(Poa* poa) = 0;
  /// The Boost.Asio context the runtime's I/O runs on.
  virtual boost::asio::io_context& ioc() noexcept = 0;
  /// Runs the event loop on `thread_count` background threads and returns.
  virtual void start_thread_pool(size_t thread_count) noexcept = 0;
  /// Runs the event loop on the calling thread until `destroy()`.
  virtual void run() = 0;
  /// Stops every listener and the event loop, and frees the runtime.
  virtual void destroy() = 0;
  /// Adds (or replaces) an entry in host.json, which tells browser clients
  /// which objects to connect to. Throws `Exception` for an empty name.
  virtual void add_to_host_json(std::string_view name, const ObjectId& object_id) = 0;
  /// Removes every host.json entry.
  virtual void clear_host_json() = 0;
  /// Writes host.json to `output_path`, or to the HTTP root when empty, and
  /// returns the path written. Throws `Exception` if neither is set.
  virtual std::string produce_host_json(std::string_view output_path = {}) = 0;
  /// A proxy for the `npnameserver` at `nameserver_ip` (TCP port 15000 or
  /// WebSocket port 15001, whichever transports are compiled in).
  virtual ObjectPtr<common::Nameserver> get_nameserver(std::string_view nameserver_ip) = 0;
  /// The session calls to `obj` go through, or nullptr if none is open.
  virtual SessionContext* get_object_session_context(Object* obj) = 0;
  virtual ~Rpc() = default;
};

namespace impl {
struct BuildConfig {
  LogLevel log_level = static_cast<LogLevel>(NPRPC_DEFAULT_LOG_LEVEL_U32);
  uuid_t uuid;

  uint16_t tcp_port = 0;
  std::string hostname;

  // HTTP/HTTPS settings + WebSocket/SSL WebSocket settings
  uint16_t http_port = 0;
  bool http_ssl_enabled = false;
  bool http3_enabled = false;
  bool http_ssl_client_disable_verification = false;
  std::string http_cert_file;
  std::string http_key_file;
  std::string http_dhparams_file;
  uint32_t cert_watch_interval_sec = 0; // 0 = no polling
  std::string http_root_dir;
  std::vector<std::string> http_allowed_origins;
  size_t http_max_request_body_size = NPRPC_DEFAULT_HTTP_MAX_REQUEST_BODY_SIZE;
  bool http_websocket_compression_enabled = NPRPC_DEFAULT_HTTP_WEBSOCKET_COMPRESSION_ENABLED;
  size_t http_websocket_max_message_size = NPRPC_DEFAULT_HTTP_WEBSOCKET_MAX_MESSAGE_SIZE;
  size_t http_webtransport_max_message_size = NPRPC_DEFAULT_HTTP_WEBTRANSPORT_MAX_MESSAGE_SIZE;
  size_t http_websocket_max_active_sessions_per_ip = NPRPC_DEFAULT_HTTP_WEBSOCKET_MAX_ACTIVE_SESSIONS_PER_IP;
  size_t http_websocket_upgrades_per_ip_per_second = NPRPC_DEFAULT_HTTP_WEBSOCKET_UPGRADES_PER_IP_PER_SECOND;
  size_t http_websocket_upgrades_burst = NPRPC_DEFAULT_HTTP_WEBSOCKET_UPGRADES_BURST;
  size_t http_websocket_requests_per_session_per_second = NPRPC_DEFAULT_HTTP_WEBSOCKET_REQUESTS_PER_SESSION_PER_SECOND;
  size_t http_websocket_requests_burst = NPRPC_DEFAULT_HTTP_WEBSOCKET_REQUESTS_BURST;
  size_t http3_worker_count = NPRPC_DEFAULT_HTTP3_WORKER_COUNT;
  bool http3_compression_enabled = NPRPC_DEFAULT_HTTP3_COMPRESSION_ENABLED;
  size_t http3_compression_min_size = NPRPC_DEFAULT_HTTP3_COMPRESSION_MIN_SIZE;
  size_t http3_max_active_connections_per_ip = NPRPC_DEFAULT_HTTP3_MAX_ACTIVE_CONNECTIONS_PER_IP;
  size_t http3_max_new_connections_per_ip_per_second = NPRPC_DEFAULT_HTTP3_MAX_NEW_CONNECTIONS_PER_IP_PER_SECOND;
  size_t http3_max_new_connections_burst = NPRPC_DEFAULT_HTTP3_MAX_NEW_CONNECTIONS_BURST;
  size_t http_rpc_max_requests_per_ip_per_second = NPRPC_DEFAULT_HTTP_RPC_MAX_REQUESTS_PER_IP_PER_SECOND;
  size_t http_rpc_max_requests_burst = NPRPC_DEFAULT_HTTP_RPC_MAX_REQUESTS_BURST;
  size_t http_webtransport_connects_per_ip_per_second = NPRPC_DEFAULT_HTTP_WEBTRANSPORT_CONNECTS_PER_IP_PER_SECOND;
  size_t http_webtransport_connects_burst = NPRPC_DEFAULT_HTTP_WEBTRANSPORT_CONNECTS_BURST;
  size_t http_webtransport_requests_per_session_per_second = NPRPC_DEFAULT_HTTP_WEBTRANSPORT_REQUESTS_PER_SESSION_PER_SECOND;
  size_t http_webtransport_requests_burst = NPRPC_DEFAULT_HTTP_WEBTRANSPORT_REQUESTS_BURST;
  size_t http_webtransport_stream_opens_per_session_per_second = NPRPC_DEFAULT_HTTP_WEBTRANSPORT_STREAM_OPENS_PER_SESSION_PER_SECOND;
  size_t http_webtransport_stream_opens_burst = NPRPC_DEFAULT_HTTP_WEBTRANSPORT_STREAM_OPENS_BURST;
  // In-process page renderer; see
  // RpcBuilderHttp::with_page_handler.
  PageHandler page_handler;
  bool watch_files = NPRPC_DEFAULT_WATCH_FILES; // Enable inotify-based cache invalidation (dev mode)

  // QUIC settings
  uint16_t quic_port = 0;
  std::string quic_cert_file;
  std::string quic_key_file;
  std::string ssl_client_self_signed_cert_path;

  // SHM channels for npquicrouter integration.
  // shm_egress_channel — Http3Server writes GSO batches to /nprpc_<name>_s2c
  //   instead of calling sendmsg directly.  npquicrouter drains the ring and
  //   forwards as a single sendmsg(GSO) call, preserving kernel batching.
  //   Matches "shm_egress_channel" in npquicrouter's config.json.
  // shm_ingress_channel — Http3Server reads incoming QUIC datagrams from
  //   /nprpc_<name>_c2s written by npquicrouter instead of recvmsg.
  //   Matches "shm_ingress_channel" of the corresponding route entry in
  //   npquicrouter's config.json.
  std::string shm_egress_channel;
  std::string shm_ingress_channel;

  // Size of each shared-memory ring this server creates for an accepted
  // client, and the largest single message one will carry.  Two rings per
  // client, resident from creation — see config_default.hpp.
  size_t shm_ring_buffer_size = NPRPC_DEFAULT_SHM_RING_BUFFER_SIZE;
  size_t shm_max_message_size = NPRPC_DEFAULT_SHM_MAX_MESSAGE_SIZE;

  // TCP transport tuning
  bool use_epoll_tcp = false; // Use raw epoll server instead of Asio (Linux only)
  bool use_uring_tcp = false; // Use io_uring server instead of Asio (Linux only)
};

} // namespace impl

class RpcBuilderHttp;
class RpcBuilderQuic;
class RpcBuilderTcp;

/// Settings shared by every transport. Start from `RpcBuilder`, call
/// `with_tcp()`, `with_http()` or `with_quic()` for transport-specific ones,
/// and finish with `build()`.
///
/// The transport builders are returned by value but share one configuration,
/// so any of them can call `build()`:
///
/// ```cpp
/// auto rpc = nprpc::RpcBuilder()
///                .set_log_level(nprpc::LogLevel::warn)
///                .with_hostname("example.com")
///                .with_http(8443)
///                .ssl("cert.pem", "key.pem")
///                .root_dir("www")
///                .build();
/// ```
class RpcBuilderBase
{
protected:
  std::shared_ptr<impl::BuildConfig> cfg_;
  RpcBuilderBase(std::shared_ptr<impl::BuildConfig> cfg)
      : cfg_(std::move(cfg)) {};

public:
  /// Runtime log verbosity. Default: set at build time
  /// (`NPRPC_DEFAULT_LOG_LEVEL`).
  RpcBuilderBase& set_log_level(::nprpc::LogLevel level) noexcept
  {
    cfg_->log_level = level;
    return *this;
  }

#if defined(NPRPC_ENABLE_TCP) || defined(NPRPC_ENABLE_WEBSOCKET) || \
    defined(NPRPC_ENABLE_HTTP) || defined(NPRPC_ENABLE_HTTP3) || \
    defined(NPRPC_ENABLE_QUIC) || defined(NPRPC_ENABLE_SSL)
  /// Host name advertised in the URLs of objects activated here, so clients
  /// on other machines can reach them.
  RpcBuilderBase& with_hostname(std::string_view hostname) noexcept
  {
    cfg_->hostname = hostname;
    return *this;
  }
#endif

#if defined(NPRPC_ENABLE_SSL)
  /// Trusts the certificate at `cert_path` for outgoing TLS connections,
  /// e.g. a development server's self-signed one.
  RpcBuilderBase&
  enable_ssl_client_self_signed_cert(std::string_view cert_path) noexcept
  {
    cfg_->ssl_client_self_signed_cert_path = cert_path;
    return *this;
  }

  /// Accepts any certificate on outgoing TLS connections. For testing only.
  RpcBuilderBase& disable_ssl_client_verification() noexcept
  {
    cfg_->http_ssl_client_disable_verification = true;
    return *this;
  }
#endif
  /// How much shared memory each accepted client costs this server.
  ///
  /// Two rings of @p ring_bytes are created per client and both are resident
  /// for the life of the connection, so this is a per-client memory price
  /// paid whether the client is busy or idle.  Raise it for a server whose
  /// messages are large — the ring must be able to hold a whole one — and
  /// note that a single oversized call (an image, a document) is usually
  /// better carried in its own segment than paid for on every connection.
  ///
  /// @param ring_bytes size of each ring, per direction.
  /// @param max_message_bytes largest single message; must be smaller than
  ///        @p ring_bytes, and is clamped with a warning if it is not.
  ///
  /// Client processes need no matching call: the sizes are written into the
  /// ring header at creation and adopted by whoever opens it.
  RpcBuilderBase& shm_channel_sizes(size_t ring_bytes,
                                    size_t max_message_bytes) noexcept
  {
    cfg_->shm_ring_buffer_size = ring_bytes;
    cfg_->shm_max_message_size = max_message_bytes;
    return *this;
  }

#if defined(NPRPC_ENABLE_TCP)
  /// Listens for native TCP clients on `port`.
  RpcBuilderTcp with_tcp(uint16_t port) noexcept;
#endif

#if defined(NPRPC_ENABLE_HTTP) || defined(NPRPC_ENABLE_WEBSOCKET)
  /// Serves HTTP, WebSocket and (with `enable_http3`) HTTP/3 and
  /// WebTransport on `port`: RPC at `/rpc`, pages, and static files.
  RpcBuilderHttp with_http(uint16_t port) noexcept;
#endif

#if defined(NPRPC_ENABLE_QUIC)
  /// Listens for native QUIC clients on `port`.
  RpcBuilderQuic with_quic(uint16_t port) noexcept;
#endif

  /// Starts the runtime with this configuration. There is one per process.
  NPRPC_API Rpc* build();
};

#if defined(NPRPC_ENABLE_TCP)
/// TCP settings; see `RpcBuilderBase::with_tcp`.
class RpcBuilderTcp : public RpcBuilderBase
{
public:
  /// Prefer `RpcBuilderBase::with_tcp`.
  explicit RpcBuilderTcp(std::shared_ptr<impl::BuildConfig> cfg)
      : RpcBuilderBase(std::move(cfg))
  {
  }

  /// Use a raw epoll I/O loop instead of Boost.Asio for incoming TCP
  /// connections.  Removes per-read dispatch overhead from io_context.
  /// Linux only.  Default: off.
  RpcBuilderTcp& with_epoll() noexcept
  {
    cfg_->use_epoll_tcp = true;
    return *this;
  }

  /// Conditionally enable the epoll server.
  RpcBuilderTcp& with_epoll_if(bool condition) noexcept
  {
    if (condition) cfg_->use_epoll_tcp = true;
    return *this;
  }

  /// Use a raw io_uring I/O loop for incoming TCP connections (Linux only).
  RpcBuilderTcp& with_uring() noexcept
  {
    cfg_->use_uring_tcp = true;
    return *this;
  }

  /// Conditionally enable the io_uring server.
  RpcBuilderTcp& with_uring_if(bool condition) noexcept
  {
    if (condition) cfg_->use_uring_tcp = true;
    return *this;
  }
};
#endif

#if defined(NPRPC_ENABLE_HTTP) || defined(NPRPC_ENABLE_WEBSOCKET)
/// HTTP, WebSocket, HTTP/3 and WebTransport settings; see
/// `RpcBuilderBase::with_http`.
///
/// The `max_*_per_second` limits are token buckets per client IP or
/// session: `rate` tokens a second, holding at most `burst` (0 = `rate`).
class RpcBuilderHttp : public RpcBuilderBase
{
public:
  /// Prefer `RpcBuilderBase::with_http`.
  explicit RpcBuilderHttp(std::shared_ptr<impl::BuildConfig> cfg)
      : RpcBuilderBase(std::move(cfg))
  {
  }

  /// Browser origins allowed to call cross-origin, e.g.
  /// `{"https://app.example.com"}`. Replaces any earlier list.
  RpcBuilderHttp&
  allow_origins(std::initializer_list<std::string_view> origins) noexcept
  {
    cfg_->http_allowed_origins.clear();
    cfg_->http_allowed_origins.reserve(origins.size());

    for (auto origin : origins) {
      cfg_->http_allowed_origins.emplace_back(origin);
    }

    return *this;
  }

  /// `allow_origins` for a list built at run time.
  RpcBuilderHttp& allow_origins(std::vector<std::string> origins) noexcept
  {
    cfg_->http_allowed_origins = std::move(origins);
    return *this;
  }

  /// Enable inotify-based cache invalidation for the HTTP root directory.
  /// Any file updated under root_dir() is immediately evicted from the cache
  /// so the next request serves the fresh version from disk.
  /// Useful during development (e.g. after `npm run build`).
  /// On non-Linux platforms this is a no-op; mtime polling handles staleness.
  RpcBuilderHttp& watch_files() noexcept
  {
    cfg_->watch_files = true;
    return *this;
  }

  /// Enable SHM channel offload via npquicrouter.
  /// Exchange HTTP/3 datagrams with npquicrouter through its shared-memory
  /// rings instead of loopback UDP.
  ///
  /// The router creates the rings and recreates them each time it starts.
  /// The server follows: it checks once a second what the names point at and
  /// reattaches, logging a warning, so the two can start and restart in any
  /// order. Open QUIC connections survive a router restart.
  ///
  /// @param egress_channel  Name used to open /nprpc_<name>_s2c — Http3Server
  ///   writes GSO batches there instead of calling sendmsg directly.
  ///   Must match "shm_egress_channel" in npquicrouter's config.json.
  /// @param ingress_channel Name used to open /nprpc_<name>_c2s — Http3Server
  ///   reads incoming QUIC datagrams from there instead of recvmsg.
  ///   Must match the route's "shm_ingress_channel" in npquicrouter's
  ///   config.json.
  RpcBuilderHttp& shm_egress_channel(std::string_view egress_channel,
                                     std::string_view ingress_channel) noexcept
  {
    cfg_->shm_egress_channel  = egress_channel;
    cfg_->shm_ingress_channel = ingress_channel;
    return *this;
  }

  /// Limits RPC-over-HTTP requests per client IP.
  RpcBuilderHttp& max_http_rpc_requests_per_ip_per_second(
      size_t rate,
      size_t burst = 0) noexcept
  {
    cfg_->http_rpc_max_requests_per_ip_per_second = rate;
    cfg_->http_rpc_max_requests_burst = burst;
    return *this;
  }
#if defined(NPRPC_ENABLE_SSL)
  /// Serves HTTPS/WSS (and HTTP/3) with this certificate chain and key,
  /// both PEM. See also `watch_certificates` and `reload_certificates`.
  RpcBuilderHttp& ssl(std::string_view cert_file,
                      std::string_view key_file,
                      std::string_view dhparams_file = "") noexcept
  {
    cfg_->http_ssl_enabled = true;
    cfg_->http_cert_file = cert_file;
    cfg_->http_key_file = key_file;
    cfg_->http_dhparams_file = dhparams_file;
    return *this;
  }

  /// Poll the certificate and key files every `interval` and reload them
  /// in-process when they change on disk, so a certbot renewal is picked up
  /// without a restart. Zero (the default) disables polling; the certificate
  /// can still be reloaded on demand with `reload_certificates()`, which
  /// is the better fit when certbot can run a --deploy-hook.
  RpcBuilderHttp& watch_certificates(std::chrono::seconds interval) noexcept
  {
    cfg_->cert_watch_interval_sec = static_cast<uint32_t>(interval.count());
    return *this;
  }
#endif
#if defined(NPRPC_ENABLE_HTTP3)
  /// `enable_http3()` when `condition` holds.
  RpcBuilderHttp& enable_if_http3(bool condition) noexcept
  {
    if (condition) cfg_->http3_enabled = true;
    return *this;
  }

  /// Also serves HTTP/3 and WebTransport on the same port (UDP). Needs
  /// `ssl()`.
  RpcBuilderHttp& enable_http3() noexcept
  {
    cfg_->http3_enabled = true;
    return *this;
  }
#endif
  /// Render pages in this process.
  ///
  /// @p handler is consulted for every GET/HEAD/POST the RPC endpoint did not
  /// claim.  Returning std::nullopt falls through to the server's normal
  /// routing, so static assets keep their zero-copy path.
  RpcBuilderHttp& with_page_handler(PageHandler handler) noexcept
  {
    cfg_->page_handler = std::move(handler);
    return *this;
  }

  /// Directory served as static files, from an in-memory cache. Also
  /// where `Rpc::produce_host_json()` writes by default.
  RpcBuilderHttp& root_dir(std::string_view root_dir) noexcept
  {
    cfg_->http_root_dir = root_dir;
    return *this;
  }

  /// Largest HTTP request body accepted.
  RpcBuilderHttp& max_request_body_size(size_t bytes) noexcept
  {
    cfg_->http_max_request_body_size = bytes;
    return *this;
  }
#if defined(NPRPC_ENABLE_WEBSOCKET)
  /// Negotiates permessage-deflate on WebSocket connections.
  RpcBuilderHttp& websocket_compression(bool enabled = true) noexcept
  {
    cfg_->http_websocket_compression_enabled = enabled;
    return *this;
  }

  /// Limits concurrent WebSocket sessions per client IP.
  RpcBuilderHttp& max_websocket_sessions_per_ip(size_t count) noexcept
  {
    cfg_->http_websocket_max_active_sessions_per_ip = count;
    return *this;
  }

  /// Limits new WebSocket connections per client IP.
  RpcBuilderHttp& max_websocket_upgrades_per_ip_per_second(
      size_t rate,
      size_t burst = 0) noexcept
  {
    cfg_->http_websocket_upgrades_per_ip_per_second = rate;
    cfg_->http_websocket_upgrades_burst = burst;
    return *this;
  }

  /// Limits calls per WebSocket session.
  RpcBuilderHttp& max_websocket_requests_per_session_per_second(
      size_t rate,
      size_t burst = 0) noexcept
  {
    cfg_->http_websocket_requests_per_session_per_second = rate;
    cfg_->http_websocket_requests_burst = burst;
    return *this;
  }
#endif

#if defined(NPRPC_ENABLE_HTTP3)
  /// Largest WebSocket message accepted.
  RpcBuilderHttp& max_websocket_message_size(size_t bytes) noexcept
  {
    cfg_->http_websocket_max_message_size = bytes;
    return *this;
  }

  /// Largest WebTransport message accepted.
  RpcBuilderHttp& max_webtransport_message_size(size_t bytes) noexcept
  {
    cfg_->http_webtransport_max_message_size = bytes;
    return *this;
  }
  /// Set the number of dedicated HTTP/3 worker sockets/threads.
  /// Pass 0 to auto-size from hardware concurrency; default: 4.
  RpcBuilderHttp& http3_workers(size_t count) noexcept
  {
    cfg_->http3_worker_count = count;
    return *this;
  }

  /// Compress HTTP/3 responses with gzip or deflate, whichever the client's
  /// Accept-Encoding prefers. Applies to static files of text-like types
  /// (HTML, CSS, JS, JSON, SVG, WASM, ...) — each is compressed once and kept
  /// in the file cache — and to page-handler responses, which are compressed
  /// per request. RPC traffic, already-compressed media, and bodies smaller
  /// than @p min_size bytes are sent as-is.
  RpcBuilderHttp& http3_compression(
      bool enabled = true,
      size_t min_size = NPRPC_DEFAULT_HTTP3_COMPRESSION_MIN_SIZE) noexcept
  {
    cfg_->http3_compression_enabled = enabled;
    cfg_->http3_compression_min_size = min_size;
    return *this;
  }

  /// Limits concurrent HTTP/3 connections per client IP.
  RpcBuilderHttp& max_http3_connections_per_ip(size_t count) noexcept
  {
    cfg_->http3_max_active_connections_per_ip = count;
    return *this;
  }

  /// Limits new HTTP/3 connections per client IP.
  RpcBuilderHttp& max_http3_new_connections_per_ip_per_second(
      size_t rate,
      size_t burst = 0) noexcept
  {
    cfg_->http3_max_new_connections_per_ip_per_second = rate;
    cfg_->http3_max_new_connections_burst = burst;
    return *this;
  }

  /// Limits new WebTransport sessions per client IP.
  RpcBuilderHttp& max_webtransport_connects_per_ip_per_second(
      size_t rate,
      size_t burst = 0) noexcept
  {
    cfg_->http_webtransport_connects_per_ip_per_second = rate;
    cfg_->http_webtransport_connects_burst = burst;
    return *this;
  }

  /// Limits calls per WebTransport session.
  RpcBuilderHttp& max_webtransport_requests_per_session_per_second(
      size_t rate,
      size_t burst = 0) noexcept
  {
    cfg_->http_webtransport_requests_per_session_per_second = rate;
    cfg_->http_webtransport_requests_burst = burst;
    return *this;
  }

  /// Limits streams opened per WebTransport session.
  RpcBuilderHttp& max_webtransport_stream_opens_per_session_per_second(
      size_t rate,
      size_t burst = 0) noexcept
  {
    cfg_->http_webtransport_stream_opens_per_session_per_second = rate;
    cfg_->http_webtransport_stream_opens_burst = burst;
    return *this;
  }
#endif
};
#endif

#if defined(NPRPC_ENABLE_QUIC)
/// Native QUIC settings; see `RpcBuilderBase::with_quic`.
class RpcBuilderQuic : public RpcBuilderBase
{
public:
  /// Prefer `RpcBuilderBase::with_quic`.
  explicit RpcBuilderQuic(std::shared_ptr<impl::BuildConfig> cfg)
      : RpcBuilderBase(std::move(cfg))
  {
  }

  /// Certificate chain and key (PEM) for the QUIC handshake; required.
  RpcBuilderQuic& ssl(std::string_view cert_file,
                      std::string_view key_file) noexcept
  {
    cfg_->quic_cert_file = cert_file;
    cfg_->quic_key_file = key_file;
    return *this;
  }
};
#endif

#if defined(NPRPC_ENABLE_TCP)
inline RpcBuilderTcp RpcBuilderBase::with_tcp(uint16_t port) noexcept
{
  cfg_->tcp_port = port;
  return RpcBuilderTcp(cfg_);
}
#endif

#if defined(NPRPC_ENABLE_HTTP) || defined(NPRPC_ENABLE_WEBSOCKET)
inline RpcBuilderHttp RpcBuilderBase::with_http(uint16_t port) noexcept
{
  cfg_->http_port = port;
  return RpcBuilderHttp(cfg_);
}
#endif

#if defined(NPRPC_ENABLE_QUIC)
inline RpcBuilderQuic RpcBuilderBase::with_quic(uint16_t port) noexcept
{
  cfg_->quic_port = port;
  return RpcBuilderQuic(cfg_);
}
#endif
// Note: with_* return sub-builders by value but they all share the same
// shared_ptr<BuildConfig>, so calling build() on a stored sub-builder
// (e.g. auto b = builder.with_http(...); b.shm_egress_channel(...); b.build();)
// is safe regardless of the original RpcBuilder's lifetime.

/// Entry point for configuring and starting the runtime; see
/// `RpcBuilderBase` for the settings and an example.
class RpcBuilder : public RpcBuilderBase
{
public:
  /// A builder with default settings.
  NPRPC_API RpcBuilder();
};

/// Converts a generic `Object` to the proxy type `T` of its interface.
///
/// On success `obj` is consumed (set to nullptr) and the new proxy returned.
/// Returns nullptr, leaving `obj` alone, if the object does not implement
/// `T`'s interface.
template <class T>
  requires(std::is_base_of_v<Object, T>)
T* narrow(Object*& obj) noexcept
{
  static_assert(std::is_base_of_v<Object, T>);

  if (obj->get_class() != T::servant_t::_get_class())
    return nullptr;

  auto result = new T(0);
  static_cast<Object&>(*result) = std::move(*obj);

  delete obj;
  obj = nullptr;

  return result;
}

} // namespace nprpc

#include <iomanip>
#include <ostream>

/// Prints an object reference's fields, one per line.
inline std::ostream& operator<<(std::ostream& os, const nprpc::Object& obj)
{
  os << "object_id: " << std::hex << std::setw(16) << std::setfill('0')
     << obj.object_id() << "\npoa_idx: " << obj.poa_idx()
     << "\nflags: " << obj.flags() << "\nclass_id: " << obj.class_id()
     << "\nurls: " << obj.urls() << '\n';

  return os;
}

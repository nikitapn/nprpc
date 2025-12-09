// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/impl/shared_memory_channel.hpp>
#include <nprpc/impl/shared_memory_connection.hpp>
#include <nprpc/impl/udp_connection.hpp>
#ifdef NPRPC_QUIC_ENABLED
#include <nprpc/impl/quic_transport.hpp>
#endif
#include "logging.hpp"
#include <nprpc_nameserver.hpp>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <cassert>
#include <fstream>
#include <functional>

#ifdef _WIN32
#include <boost/asio/ssl/context.hpp>
#include <wincrypt.h>
namespace {
void add_windows_root_certs(boost::asio::ssl::context& ctx)
{
  HCERTSTORE hStore = CertOpenSystemStore(0, "ROOT");
  if (hStore == NULL) {
    return;
  }

  X509_STORE* store = X509_STORE_new();
  PCCERT_CONTEXT pContext = NULL;
  while ((pContext = CertEnumCertificatesInStore(hStore, pContext)) != NULL) {
    X509* x509 = d2i_X509(NULL, (const unsigned char**)&pContext->pbCertEncoded,
                          pContext->cbCertEncoded);
    if (x509 != NULL) {
      X509_STORE_add_cert(store, x509);
      X509_free(x509);
    }
  }

  CertFreeCertificateContext(pContext);
  CertCloseStore(hStore, 0);

  SSL_CTX_set_cert_store(ctx.native_handle(), store);
}
} // namespace
#endif // BOOST_OS_WINDOWS

namespace nprpc::impl {

NPRPC_API Rpc* RpcBuilderBase::build(boost::asio::io_context& ioc)
{
  if (impl::g_rpc)
    throw Exception("NPRPC has been previously initialized");

  // First check if the configuration is valid
  if (cfg_.http_ssl_enabled) {
    if (cfg_.http_cert_file.empty() || cfg_.http_key_file.empty()) {
      throw Exception(
          "HTTP SSL enabled but certificate or key file not specified");
    }
  }

  if (cfg_.http3_enabled) {
    if (cfg_.http_cert_file.empty() || cfg_.http_key_file.empty()) {
      throw Exception(
          "HTTP/3 enabled but certificate or key file not specified");
    }
    if (cfg_.http_root_dir.empty()) {
      throw Exception("HTTP/3 enabled but root directory not specified");
    }
  }

  if (cfg_.http_port != 0 && cfg_.http_root_dir.empty()) {
    NPRPC_LOG_INFO(
        "[nprpc][I] HTTP root directory not specified, only upgrading to "
        "WebSocket will be available.");
  }

  if (cfg_.quic_port != 0) {
    if (cfg_.quic_cert_file.empty() || cfg_.quic_key_file.empty()) {
      throw Exception("QUIC enabled but certificate or key file not specified");
    }
  }

  if (cfg_.http_ssl_enabled) {
    auto read_file_to_string = [](std::string const file) {
      std::ifstream is(file, std::ios_base::in);
      if (!is) {
        throw std::runtime_error("could not open certificate file: \"" + file +
                                 "\"");
      }
      return std::string(std::istreambuf_iterator<char>(is),
                         std::istreambuf_iterator<char>());
    };

    std::string const cert = read_file_to_string(cfg_.http_cert_file);
    std::string const key = read_file_to_string(cfg_.http_key_file);

    // ctx.set_password_callback(
    //     [](std::size_t, ssl::context_base::password_purpose) {
    //       return "test";
    //     });

    g_cfg.ssl_context_server.set_options(
        boost::asio::ssl::context::default_workarounds |
        boost::asio::ssl::context::no_sslv2 |
        boost::asio::ssl::context::no_sslv3 |
        boost::asio::ssl::context::no_tlsv1 |   // Also disable TLS 1.0
        boost::asio::ssl::context::no_tlsv1_1 | // Also disable TLS 1.1
        boost::asio::ssl::context::single_dh_use |
        boost::asio::ssl::context::no_compression // Prevent CRIME attacks
    );

    g_cfg.ssl_context_server.use_certificate_chain(
        boost::asio::buffer(cert.data(), cert.size()));

    g_cfg.ssl_context_server.use_private_key(
        boost::asio::buffer(key.data(), key.size()),
        boost::asio::ssl::context::file_format::pem);

    if (cfg_.http_dhparams_file.size() > 0) {
      std::string const dh = read_file_to_string(cfg_.http_dhparams_file);
      g_cfg.ssl_context_server.use_tmp_dh(
          boost::asio::buffer(dh.data(), dh.size()));
    }
  }

  // Configure SSL client settings based on RpcBuilder options
  if (cfg_.http_ssl_client_disable_verification) {
    NPRPC_LOG_INFO("SSL client verification disabled (for testing only)");
    g_cfg.ssl_context_client.set_verify_mode(ssl::verify_none);
  } else {
#ifdef _WIN32
    // On Windows, add system root certificates to the SSL context
    add_windows_root_certs(ctx_client);
#else
    // On other platforms, set default verification paths
    boost::system::error_code ec;
    g_cfg.ssl_context_client.set_default_verify_paths(ec);
    if (ec) {
      NPRPC_LOG_WARN(
          "Warning: Failed to set default SSL verification paths: {}",
          ec.message());
    } else {
      NPRPC_LOG_INFO("SSL client verification paths set successfully.");
    }
#endif // _WIN32
    if (!cfg_.ssl_client_self_signed_cert_path.empty()) {
      try {
        g_cfg.ssl_context_client.load_verify_file(
            cfg_.ssl_client_self_signed_cert_path);
        NPRPC_LOG_INFO("Loaded self-signed certificate for SSL client: {}",
                       cfg_.ssl_client_self_signed_cert_path);
      } catch (const std::exception& ex) {
        NPRPC_LOG_WARN("Warning: Failed to load self-signed certificate: {}",
                       ex.what());
        throw;
      }
    }
    g_cfg.ssl_context_client.set_verify_mode(ssl::verify_peer);
  }

  // Copy builder config to global config
  g_cfg.debug_level = cfg_.debug_level;
  g_cfg.hostname = cfg_.hostname;
  g_cfg.listen_tcp_port = cfg_.tcp_port;
  g_cfg.listen_http_port = cfg_.http_port;
  g_cfg.listen_udp_port = cfg_.udp_port;
  g_cfg.listen_quic_port = cfg_.quic_port;
  g_cfg.http3_enabled = cfg_.http3_enabled;
  g_cfg.ssr_enabled = cfg_.ssr_enabled;
  g_cfg.http_cert_file = cfg_.http_cert_file;
  g_cfg.http_key_file = cfg_.http_key_file;
  g_cfg.http_root_dir = cfg_.http_root_dir;
  g_cfg.ssr_handler_dir =
      cfg_.ssr_handler_dir.empty() ? cfg_.http_root_dir : cfg_.ssr_handler_dir;
  g_cfg.quic_cert_file = cfg_.quic_cert_file;
  g_cfg.quic_key_file = cfg_.quic_key_file;

  impl::g_rpc = new impl::RpcImpl(ioc);
  return impl::g_rpc;
}

Poa* RpcImpl::create_poa_impl(uint32_t objects_max,
                              PoaPolicy::Lifespan lifespan,
                              PoaPolicy::ObjectIdPolicy object_id_policy)
{
  std::lock_guard<std::mutex> lk(poas_mut_);

  auto it =
      std::find(std::begin(poas_created_), std::end(poas_created_), false);
  if (it == std::end(poas_created_))
    throw std::runtime_error("Maximum number of POAs reached");

  auto index = std::distance(std::begin(poas_created_), it);
  auto poa = std::make_shared<PoaImpl>(
      objects_max, static_cast<uint16_t>(index), lifespan, object_id_policy);
  poas_[index] = poa;
  (*it) = true; // Mark this POA as created

  return poa.get();
}

extern void init_socket(boost::asio::io_context& ioc);
extern void init_http_server(boost::asio::io_context& ioc);
extern void init_shared_memory_listener(boost::asio::io_context& ioc);
extern void init_udp_listener(boost::asio::io_context& ioc);

NPRPC_API Config g_cfg;
NPRPC_API RpcImpl* g_rpc;

// Forward declarations for cleanup
void stop_udp_listener();
void clear_udp_connections();
void stop_shared_memory_listener();
void stop_socket_listener();
void stop_http_server();
#ifdef NPRPC_HTTP3_ENABLED
void stop_http3_server();
#endif
#ifdef NPRPC_SSR_ENABLED
extern void init_ssr(boost::asio::io_context& ioc);
extern void stop_ssr();
#endif

void RpcImpl::destroy()
{
  // Stop all listeners first
  stop_socket_listener();
  stop_http_server();
  stop_udp_listener();
  clear_udp_connections();
  stop_shared_memory_listener();
#ifdef NPRPC_QUIC_ENABLED
  stop_quic_listener();
#endif
#ifdef NPRPC_HTTP3_ENABLED
  stop_http3_server();
#endif
#ifdef NPRPC_SSR_ENABLED
  stop_ssr();
#endif

  // Shutdown and clear open sessions to release their async operations
  // Move sessions out of the locked section to avoid deadlock:
  // destructor -> close() -> close_session() -> lock connections_mut_
  std::vector<std::shared_ptr<Session>> sessions_to_destroy;
  {
    std::lock_guard<std::mutex> lk(connections_mut_);
    for (auto& session : opened_sessions_) {
      session->shutdown();
    }
    sessions_to_destroy = std::move(opened_sessions_);
  }
  // Now destroy sessions outside the lock
  sessions_to_destroy.clear();

  delete this;
  g_rpc = nullptr;
}

void RpcImpl::destroy_poa(Poa* poa)
{
  if (!poa)
    return;

  std::lock_guard<std::mutex> lk(poas_mut_);

  auto idx = poa->get_index();
  if (idx >= poas_.size()) {
    throw std::out_of_range("Poa index out of range");
  }

  poas_[idx].reset();
  poas_created_[idx] = false;
}

NPRPC_API std::shared_ptr<Session>
RpcImpl::get_session(const EndPoint& endpoint)
{
  std::shared_ptr<Session> con;
  {
    std::lock_guard<std::mutex> lk(connections_mut_);
    if (auto it = std::find_if(opened_sessions_.begin(), opened_sessions_.end(),
                               [&endpoint](auto const& ptr) {
                                 return ptr->remote_endpoint() == endpoint;
                               });
        it != opened_sessions_.end()) {
      con = (*it);
    } else {
      switch (endpoint.type()) {
      case EndPointType::TcpTethered:
        throw nprpc::ExceptionCommFailure(
            "nprpc::impl::RpcImpl::get_session: Cannot create tethered "
            "TCP "
            "connection");
      case EndPointType::Tcp:
        con = std::make_shared<SocketConnection>(
            endpoint,
            boost::asio::ip::tcp::socket(boost::asio::make_strand(ioc_)));
        break;
      case EndPointType::WebSocket:
        con = make_client_plain_websocket_session(endpoint, ioc_);
        break;
      case EndPointType::SecuredWebSocket:
        con = make_client_ssl_websocket_session(endpoint, ioc_);
        break;
      case EndPointType::SharedMemory:
        con = std::make_shared<SharedMemoryConnection>(endpoint, ioc_);
        break;
#ifdef NPRPC_QUIC_ENABLED
      case EndPointType::Quic:
        con = make_quic_client_session(endpoint, ioc_);
        break;
#endif
      default:
        throw nprpc::ExceptionCommFailure(
            "nprpc::impl::RpcImpl::get_session: Unknown endpoint "
            "type: " +
            std::to_string(static_cast<int>(endpoint.type())));
      }

      opened_sessions_.push_back(con);
    }
  }
  return con;
}

NPRPC_API void RpcImpl::call(const EndPoint& endpoint,
                             flat_buffer& buffer,
                             uint32_t timeout_ms)
{
  get_session(endpoint)->send_receive(buffer, timeout_ms);
}

NPRPC_API void RpcImpl::send_udp(const EndPoint& endpoint, flat_buffer&& buffer)
{
  if (endpoint.type() == EndPointType::Udp) {
    // Use dedicated UDP socket for fire-and-forget
    auto udp_conn = get_udp_connection(ioc_, std::string(endpoint.hostname()),
                                       endpoint.port());
    udp_conn->send(std::move(buffer));
    return;
  }

  // Fallback: use existing transport but fire-and-forget style
  // This allows UDP-style interfaces to work over TCP/SharedMemory for
  // testing
  get_session(endpoint)->send_receive_async(std::move(buffer), std::nullopt, 0);
}

NPRPC_API void RpcImpl::send_unreliable(const EndPoint& endpoint,
                                        flat_buffer&& buffer)
{
  switch (endpoint.type()) {
  case EndPointType::Udp: {
    // UDP: Use dedicated socket for fire-and-forget
    auto udp_conn = get_udp_connection(ioc_, std::string(endpoint.hostname()),
                                       endpoint.port());
    udp_conn->send(std::move(buffer));
    break;
  }
  default:
    // All other transports: Use session's send_datagram (QUIC uses
    // DATAGRAM, others fall back)
    get_session(endpoint)->send_datagram(std::move(buffer));
    break;
  }
}

NPRPC_API void RpcImpl::call_udp_reliable(const EndPoint& endpoint,
                                          flat_buffer& buffer,
                                          uint32_t timeout_ms,
                                          uint32_t max_retries)
{
  if (endpoint.type() == EndPointType::Udp) {
    // Use dedicated UDP socket with reliable delivery
    auto udp_conn = get_udp_connection(ioc_, std::string(endpoint.hostname()),
                                       endpoint.port());

    // Use condition variable for lower overhead than promise/future
    std::mutex mtx;
    std::condition_variable cv;
    bool done = false;
    boost::system::error_code result_ec;

    udp_conn->send_reliable(
        buffer, // Pass reference - caller is blocked, no copy needed for
                // first send
        [&](const boost::system::error_code& ec, flat_buffer& response) {
          {
            std::lock_guard<std::mutex> lock(mtx);
            result_ec = ec;
            if (!ec) {
              buffer = std::move(response);
            }
            done = true;
          }
          cv.notify_one();
        },
        timeout_ms, max_retries);

    // Wait for completion
    {
      std::unique_lock<std::mutex> lock(mtx);
      cv.wait(lock, [&done] { return done; });
    }

    if (result_ec) {
      throw nprpc::Exception(std::string("UDP reliable call failed: ") +
                             result_ec.message());
    }
    return;
  }

  // Fallback: use existing transport (synchronous call)
  get_session(endpoint)->send_receive(buffer, timeout_ms * (max_retries + 1));
}

NPRPC_API void RpcImpl::call_udp_reliable_async(
    const EndPoint& endpoint,
    flat_buffer&& buffer,
    std::optional<std::function<void(const boost::system::error_code&,
                                     flat_buffer&)>>&& completion_handler,
    uint32_t timeout_ms,
    uint32_t max_retries)
{
  if (endpoint.type() == EndPointType::Udp) {
    auto udp_conn = get_udp_connection(ioc_, std::string(endpoint.hostname()),
                                       endpoint.port());

    udp_conn->send_reliable_async(
        std::move(buffer),
        [handler =
             std::move(completion_handler)](const boost::system::error_code& ec,
                                            flat_buffer& response) mutable {
          if (handler) {
            (*handler)(ec, response);
          }
        },
        timeout_ms, max_retries);
    return;
  }

  // Fallback: use existing transport (async call)
  get_session(endpoint)->send_receive_async(std::move(buffer),
                                            std::move(completion_handler),
                                            timeout_ms * (max_retries + 1));
}

NPRPC_API void RpcImpl::call_async(
    const EndPoint& endpoint,
    flat_buffer&& buffer,
    std::optional<std::function<void(const boost::system::error_code&,
                                     flat_buffer&)>>&& completion_handler,
    uint32_t timeout_ms)
{
  get_session(endpoint)->send_receive_async(
      std::move(buffer), std::move(completion_handler), timeout_ms);
}

NPRPC_API bool RpcImpl::prepare_zero_copy_buffer(SessionContext& ctx,
                                                 flat_buffer& buffer,
                                                 size_t max_size)
{
  if (!is_shared_memory(ctx.remote_endpoint))
    return false;

  // Check if we're on a server-side shared memory session
  // In that case, ctx.shm_channel is set and we should use it directly
  // instead of trying to create a client connection to the "remote" endpoint
  if (ctx.shm_channel) {
    // std::cout << "[nprpc][D] SERVER prepare_zero_copy_buffer for
    // max_size: "
    // << max_size << std::endl; Server-side: use the existing channel for
    // zero-copy response
    auto reservation = ctx.shm_channel->reserve_write(max_size);
    if (!reservation) {
      // std::cout << "[nprpc][D] SERVER reserve_write FAILED" <<
      // std::endl;
      return false;
    }

    // std::cout << "[nprpc][D] SERVER got reservation: data=" <<
    // (void*)reservation.data
    //           << " write_idx=" << reservation.write_idx << " max_size="
    //           << reservation.max_size << std::endl;

    buffer.set_view(reservation.data, 0, reservation.max_size,
                    &ctx.remote_endpoint, reservation.write_idx, true);
    return true;
  }

  // Client-side: get/create a connection to the server
  // std::cout << "[nprpc][D] prepare_zero_copy_buffer called on client-side
  // for endpoint: "
  //           << ctx.remote_endpoint.to_string() << " with max_size: " <<
  //           max_size << std::endl;

  try {
    auto session = get_session(ctx.remote_endpoint);

    if (dynamic_cast<SharedMemoryConnection*>(session.get()) == nullptr) {
      NPRPC_LOG_ERROR("prepare_zero_copy_buffer: Session is not a "
                      "SharedMemoryConnection but "
                      "{}, typeid: {}",
                      static_cast<int>(ctx.remote_endpoint.type()),
                      typeid(*session).name());
      std::abort();
    }

    auto* shm_conn = static_cast<SharedMemoryConnection*>(session.get());
    if (!shm_conn)
      return false;

    return shm_conn->prepare_write_buffer(buffer, max_size,
                                          &ctx.remote_endpoint);
  } catch (const std::exception& e) {
    NPRPC_LOG_ERROR("Error in prepare_zero_copy_buffer: {}", e.what());
    // Connection failed - fall back to normal buffer
    // The actual error will be thrown when call() is made
    return false;
  }
}

// Helper function called from flat_buffer::grow() when view mode buffer needs
// to expand
NPRPC_API bool prepare_zero_copy_buffer_grow(const EndPoint& endpoint,
                                             flat_buffer& buffer,
                                             size_t new_size,
                                             const void* existing_data,
                                             size_t existing_size)
{
  return false;
  // if (!RpcImpl::is_shared_memory(endpoint))
  //   return false;

  // // Check if we're on a server-side shared memory session
  // auto& ctx = get_context();
  // if (ctx.shm_channel) {
  //   // Server-side: use the existing channel
  //   auto reservation = ctx.shm_channel->reserve_write(new_size);
  //   if (!reservation)
  //     return false;

  //   buffer.set_view(reservation.data, 0, reservation.max_size,
  //                   &endpoint, reservation.write_idx, true);

  //   // Copy existing data to the new buffer
  //   if (existing_size > 0 && existing_data) {
  //     std::memcpy(buffer.data_ptr(), existing_data, existing_size);
  //     buffer.commit(existing_size);
  //   }

  //   return true;
  // }

  // // Client-side: get the existing connection
  // try {
  //   auto session = g_rpc->get_session(endpoint);
  //   auto* shm_conn = static_cast<SharedMemoryConnection*>(session.get());

  //   if (dynamic_cast<SharedMemoryConnection*>(session.get()) == nullptr) {
  //     std::cerr << "prepare_zero_copy_buffer_grow: Session is not a
  //     SharedMemoryConnection" << std::endl; std::abort();
  //   }

  //   // Request a new larger buffer from the shared memory ring
  //   // The old reservation will be abandoned (not committed)
  //   if (!shm_conn->prepare_write_buffer(buffer, new_size, &endpoint))
  //     return false;

  //   // Copy existing data to the new buffer
  //   if (existing_size > 0 && existing_data) {
  //     std::memcpy(buffer.data_ptr(), existing_data, existing_size);
  //     buffer.commit(existing_size);
  //   }

  //   return true;
  // } catch (const std::exception&) {
  //   return false;
  // }
}

NPRPC_API std::optional<ObjectGuard> RpcImpl::get_object(poa_idx_t poa_idx,
                                                         oid_t object_id)
{
  auto poa = g_rpc->get_poa(poa_idx);
  if (!poa)
    return std::nullopt;
  return poa->get_object(object_id);
}

bool RpcImpl::has_session(const EndPoint& endpoint) const noexcept
{
  std::lock_guard<std::mutex> lk(connections_mut_);
  return std::find_if(opened_sessions_.begin(), opened_sessions_.end(),
                      [endpoint](auto const& ptr) {
                        return ptr->remote_endpoint() == endpoint;
                      }) != opened_sessions_.end();
}

NPRPC_API SessionContext* RpcImpl::get_object_session_context(Object* obj)
{
  if (!obj)
    return nullptr;

  // We need to find the session context based on the endpoint
  auto session = g_rpc->get_session(obj->get_endpoint());
  if (session) {
    return &session->ctx();
  }

  return nullptr;
}

bool RpcImpl::close_session(Session* session)
{
  std::lock_guard<std::mutex> lk(connections_mut_);
  if (auto it = std::find_if(opened_sessions_.begin(), opened_sessions_.end(),
                             [session](auto const& ptr) {
                               return ptr->remote_endpoint() ==
                                      session->remote_endpoint();
                             });
      it != opened_sessions_.end()) {
    opened_sessions_.erase(it);
  } else {
    NPRPC_LOG_ERROR("Error: session not found");
    return false;
  }
  return true;
}

ObjectPtr<common::Nameserver>
RpcImpl::get_nameserver(std::string_view nameserver_ip)
{
  auto ip = std::string(nameserver_ip);
  ObjectPtr<common::Nameserver> obj(new common::Nameserver(0));
  detail::ObjectId& oid = obj->get_data();

  oid.object_id = 0ull;
  oid.poa_idx = 0;
  oid.flags = static_cast<nprpc::oflags_t>(detail::ObjectFlag::Persistent);
  oid.origin.fill(0);
  oid.class_id = common::INameserver_Servant::_get_class();
  oid.urls.assign("tcp://" + ip + ":15000;ws://" + ip + ":15001;");

  [[maybe_unused]] bool res = obj->select_endpoint();
  assert(res && "Nameserver must have a valid endpoint");

  return obj;
}

RpcImpl::RpcImpl(boost::asio::io_context& ioc) : ioc_{ioc}
{
  poas_created_.fill(false);

  init_socket(ioc_);
  init_http_server(ioc_);
  init_shared_memory_listener(ioc_);
  init_udp_listener(ioc_);
#ifdef NPRPC_QUIC_ENABLED
  init_quic(ioc_);
#endif
#ifdef NPRPC_HTTP3_ENABLED
  extern void init_http3_server(boost::asio::io_context & ioc);
  init_http3_server(ioc_);
#endif
#ifdef NPRPC_SSR_ENABLED
  init_ssr(ioc_);
#endif
}

void ReferenceListImpl::add_ref(ObjectServant* obj)
{
  // Check if we've exceeded the maximum references per session
  if (refs_.size() >= max_references_per_session) {
    NPRPC_LOG_ERROR("Maximum references per session exceeded ({}), "
                    "rejecting AddReference "
                    "for: {}",
                    max_references_per_session, obj->get_class());
    return;
  }

  if (auto it = std::find_if(begin(refs_), end(refs_),
                             [obj](auto& pair) { return pair.second == obj; });
      it != end(refs_)) {
    NPRPC_LOG_ERROR("duplicate reference: {}", obj->get_class());
    return;
  }

  refs_.push_back({{obj->poa_index(), obj->oid()}, obj});
  obj->add_ref();
}

bool ReferenceListImpl::remove_ref(poa_idx_t poa_idx, oid_t oid)
{
  if (auto it = std::find_if(begin(refs_), end(refs_),
                             [poa_idx, oid](auto& pair) {
                               return pair.first.poa_idx == poa_idx &&
                                      pair.first.object_id == oid;
                             });
      it != end(refs_)) {
    auto ptr = (*it).second;
    refs_.erase(it);
    ptr->release();
    return true;
  }
  return false;
}

ReferenceListImpl::~ReferenceListImpl()
{
  for (auto& ref : refs_)
    ref.second->release();
}

NPRPC_API Object* create_object_from_flat(detail::flat::ObjectId_Direct direct,
                                          EndPoint remote_endpoint)
{
  if (direct.object_id() == invalid_object_id)
    return nullptr;

  auto obj = std::unique_ptr<Object>(new Object());
  obj->local_ref_cnt_ = 1;

  auto& oid = obj->get_data();
  nprpc::detail::helpers::assign_from_flat_ObjectId(direct, oid);

  if (direct.flags() &
      static_cast<nprpc::oflags_t>(detail::ObjectFlag::Tethered)) {
    // should always use existing session
    oid.urls = remote_endpoint.to_string();
    obj->endpoint_ = remote_endpoint;
  } else {
    if (!obj->select_endpoint(remote_endpoint)) {
      // Something is malformed, we cannot select an endpoint
      throw nprpc::Exception(
          "Cannot select endpoint for object: " + std::string(oid.class_id) +
          ", available endpoints: " + obj->urls());
    }
  }

  return obj.release();
}

NPRPC_API void fill_guid(std::array<std::uint8_t, 16>& guid) noexcept
{
  auto& g = impl::g_cfg.uuid;
  std::copy(g.begin(), g.end(), guid.begin());
}

std::optional<ObjectGuard> PoaImpl::get_object(oid_t oid) noexcept
{
  if (auto* user = std::get_if<UserObjects>(&id_to_ptr_)) {
    if (oid >= max_objects_)
      return std::nullopt;
    auto* obj = user->slots[oid].load(std::memory_order_acquire);
    if (obj)
      return ObjectGuard(obj);
    return std::nullopt;
  }

  auto& sys = std::get<SystemObjects>(id_to_ptr_);
  if (auto obj = sys.get(oid))
    return ObjectGuard(obj);

  return std::nullopt;
}

ObjectId PoaImpl::finalize_activation(ObjectServant* obj,
                                      oid_t object_id,
                                      uint32_t activation_flags,
                                      SessionContext* ctx)
{
  ObjectId result;
  auto& oid = result.get_data();

  obj->poa_ = shared_from_this();
  obj->object_id_ = object_id;
  obj->activation_time_ = std::chrono::system_clock::now();

  oid.object_id = object_id;
  oid.poa_idx = get_index();
  oid.flags = 0;
  if (pl_lifespan_ == PoaPolicy::Lifespan::Persistent)
    oid.flags |= static_cast<oflags_t>(detail::ObjectFlag::Persistent);
  fill_guid(oid.origin);
  oid.class_id = obj->get_class();

  using namespace std::string_literals;
  const std::string default_url =
      g_cfg.hostname.empty() ? "127.0.0.1"s : g_cfg.hostname;

  if (activation_flags & ObjectActivationFlags::ALLOW_TCP) {
    oid.urls += (std::string(tcp_prefix) + default_url + ":" +
                 std::to_string(g_cfg.listen_tcp_port)) +
                ';';
  }

  if (activation_flags & ObjectActivationFlags::ALLOW_WEBSOCKET) {
    oid.urls += (std::string(ws_prefix) + default_url + ":" +
                 std::to_string(g_cfg.listen_http_port)) +
                ';';
  }

  if (activation_flags & ObjectActivationFlags::ALLOW_SSL_WEBSOCKET) {
    if (g_cfg.hostname.empty()) {
      throw std::runtime_error("SSL websocket requires a hostname");
    }
    oid.urls += (std::string(wss_prefix) + g_cfg.hostname + ":" +
                 std::to_string(g_cfg.listen_http_port)) +
                ';';
  }

  if (activation_flags & ObjectActivationFlags::ALLOW_HTTP) {
    oid.urls += (std::string(http_prefix) + default_url + ":" +
                 std::to_string(g_cfg.listen_http_port)) +
                ';';
  }

  if (activation_flags & ObjectActivationFlags::ALLOW_SECURED_HTTP) {
    if (g_cfg.hostname.empty()) {
      throw std::runtime_error("Secured HTTP requires a hostname");
    }
    oid.urls += (std::string(https_prefix) + g_cfg.hostname + ":" +
                 std::to_string(g_cfg.listen_http_port)) +
                ';';
  }

  if (activation_flags & ObjectActivationFlags::ALLOW_SHARED_MEMORY) {
    oid.urls += (std::string(mem_prefix) + g_server_listener_uuid) + ';';
  }

  if (activation_flags & ObjectActivationFlags::ALLOW_UDP) {
    if (g_cfg.listen_udp_port == 0) {
      throw std::runtime_error("UDP port not configured");
    }
    oid.urls += (std::string(udp_prefix) + default_url + ":" +
                 std::to_string(g_cfg.listen_udp_port)) +
                ';';
  }

  if (activation_flags & ObjectActivationFlags::ALLOW_QUIC) {
    if (g_cfg.listen_quic_port == 0) {
      throw std::runtime_error(
          "QUIC port not configured. Use set_listen_quic_port()");
    }
    oid.urls += (std::string(quic_prefix) + default_url + ":" +
                 std::to_string(g_cfg.listen_quic_port)) +
                ';';
  }

  if (pl_lifespan_ == PoaPolicy::Lifespan::Transient) {
    if (!ctx) {
      throw std::runtime_error("Object created with transient policy "
                               "requires session context for "
                               "activation");
    }
    ctx->ref_list.add_ref(obj);
  }

  if (activation_flags & ObjectActivationFlags::SESSION_SPECIFIC) {
    obj->session_ctx_ = ctx;
    oid.flags |= static_cast<oflags_t>(detail::ObjectFlag::Tethered);
  }

  return result;
}

ObjectId PoaImpl::activate_object(ObjectServant* obj,
                                  uint32_t activation_flags,
                                  SessionContext* ctx)
{
  if (std::holds_alternative<UserObjects>(id_to_ptr_)) {
    throw Exception("POA requires user-supplied object IDs; call "
                    "activate_object_with_id");
  }

  auto& sys = std::get<SystemObjects>(id_to_ptr_);
  auto object_id_internal = sys.add(obj);
  if (object_id_internal == invalid_object_id)
    throw Exception("Poa fixed size has been exceeded");

  return finalize_activation(obj, object_id_internal, activation_flags, ctx);
}

ObjectId PoaImpl::activate_object_with_id(oid_t object_id,
                                          ObjectServant* obj,
                                          uint32_t activation_flags,
                                          SessionContext* ctx)
{
  auto* user = std::get_if<UserObjects>(&id_to_ptr_);
  if (!user) {
    throw Exception("POA is configured for system-generated object IDs");
  }
  if (object_id >= max_objects_) {
    throw Exception("Object id exceeds max_objects for this POA");
  }

  ObjectServant* expected = nullptr;
  if (!user->slots[object_id].compare_exchange_strong(
          expected, obj, std::memory_order_acq_rel)) {
    throw Exception("Object id already in use");
  }

  try {
    return finalize_activation(obj, object_id, activation_flags, ctx);
  } catch (...) {
    user->slots[object_id].store(nullptr, std::memory_order_release);
    throw;
  }
}

void PoaImpl::deactivate_object(oid_t object_id)
{
  ObjectServant* obj = nullptr;

  if (auto* user = std::get_if<UserObjects>(&id_to_ptr_)) {
    if (object_id < max_objects_) {
      obj = user->slots[object_id].exchange(nullptr, std::memory_order_acq_rel);
    }
  } else {
    auto& sys = std::get<SystemObjects>(id_to_ptr_);
    obj = sys.get(object_id);
    if (obj) {
      sys.remove(object_id);
    }
  }

  if (obj) {
    obj->to_delete_.store(true);
  } else {
    std::cerr << "deactivate_object: object not found. id = " << object_id
              << '\n';
  }
}

void PoaImpl::delete_object(ObjectServant* obj)
{
  if (obj->in_use_cnt_.load(std::memory_order_acquire) == 0) {
    obj->destroy();
  } else {
    std::cerr << "delete_object: object is in use. id = " << obj->oid() << '\n';
    boost::asio::post(impl::g_rpc->ioc(),
                      std::bind(&PoaImpl::delete_object, obj));
  }
}

} // namespace nprpc::impl

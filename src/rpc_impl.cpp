#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/impl/shared_memory_connection.hpp>
#include <nprpc/impl/udp_connection.hpp>
#include <nprpc_nameserver.hpp>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <cassert>
#include <future>

namespace nprpc::impl {

Poa* RpcImpl::create_poa_impl(uint32_t objects_max, PoaPolicy::Lifespan lifespan) {
  std::lock_guard<std::mutex> lk(poas_mut_);

  auto it = std::find(std::begin(poas_created_), std::end(poas_created_), false);
  if (it == std::end(poas_created_))
    throw std::runtime_error("Maximum number of POAs reached");

  auto index = std::distance(std::begin(poas_created_), it);
  auto poa = std::make_shared<PoaImpl>(objects_max, static_cast<uint16_t>(index), lifespan);
  poas_[index] = poa;
  (*it) = true; // Mark this POA as created

  return poa.get();
}

extern void init_socket(boost::asio::io_context& ioc);
extern void init_http_server(boost::asio::io_context& ioc);
extern void init_shared_memory_listener(boost::asio::io_context& ioc);
extern void init_udp_listener(boost::asio::io_context& ioc);

NPRPC_API Config   g_cfg;
NPRPC_API RpcImpl* g_orb;

// Forward declarations for cleanup
void stop_udp_listener();
void clear_udp_connections();
void stop_shared_memory_listener();
void stop_socket_listener();
void stop_http_server();

void RpcImpl::destroy()
{
  // Stop all listeners first
  stop_socket_listener();
  stop_http_server();
  stop_udp_listener();
  clear_udp_connections();
  stop_shared_memory_listener();
  
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
  g_orb = nullptr;
}

void RpcImpl::destroy_poa(Poa* poa)
{
  if (!poa) return;

  std::lock_guard<std::mutex> lk(poas_mut_);

  auto idx = poa->get_index();
  if (idx >= poas_.size()) {
    throw std::out_of_range("Poa index out of range");
  }

  poas_[idx].reset();
  poas_created_[idx] = false;
}


NPRPC_API std::shared_ptr<Session> RpcImpl::get_session(
  const EndPoint& endpoint)
{
  std::shared_ptr<Session> con;
  {
    std::lock_guard<std::mutex> lk(connections_mut_);
    if (auto it = std::find_if(opened_sessions_.begin(), opened_sessions_.end(),
      [&endpoint](auto const& ptr) { return ptr->remote_endpoint() == endpoint; })
      ; it != opened_sessions_.end())
    {
      con = (*it);
    } else {
      switch(endpoint.type()) {
       case EndPointType::TcpTethered: 
          throw nprpc::ExceptionCommFailure("nprpc::impl::RpcImpl::get_session: Cannot create tethered TCP connection");
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
      default:
        throw nprpc::ExceptionCommFailure("nprpc::impl::RpcImpl::get_session: Unknown endpoint type: " + 
                                          std::to_string(static_cast<int>(endpoint.type())));
      }

      opened_sessions_.push_back(con);
    }
  }
  return con;
}

NPRPC_API void RpcImpl::call(
  const EndPoint& endpoint, flat_buffer& buffer, uint32_t timeout_ms)
{
  get_session(endpoint)->send_receive(buffer, timeout_ms);
}

NPRPC_API void RpcImpl::send_udp(
  const EndPoint& endpoint, flat_buffer&& buffer)
{
  if (endpoint.type() == EndPointType::Udp) {
    // Use dedicated UDP socket for fire-and-forget
    auto udp_conn = get_udp_connection(ioc_, std::string(endpoint.hostname()), endpoint.port());
    udp_conn->send(std::move(buffer));
    return;
  }
  
  // Fallback: use existing transport but fire-and-forget style
  // This allows UDP-style interfaces to work over TCP/SharedMemory for testing
  get_session(endpoint)->send_receive_async(
    std::move(buffer), std::nullopt, 0);
}

NPRPC_API void RpcImpl::call_udp_reliable(
  const EndPoint& endpoint, 
  flat_buffer& buffer,
  uint32_t timeout_ms,
  uint32_t max_retries)
{
  if (endpoint.type() == EndPointType::Udp) {
    // Use dedicated UDP socket with reliable delivery
    auto udp_conn = get_udp_connection(ioc_, std::string(endpoint.hostname()), endpoint.port());
    
    // Use condition variable for lower overhead than promise/future
    std::mutex mtx;
    std::condition_variable cv;
    bool done = false;
    boost::system::error_code result_ec;
    
    udp_conn->send_reliable(
      buffer,  // Pass reference - caller is blocked, no copy needed for first send
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
      timeout_ms,
      max_retries
    );
    
    // Wait for completion
    {
      std::unique_lock<std::mutex> lock(mtx);
      cv.wait(lock, [&done] { return done; });
    }
    
    if (result_ec) {
      throw nprpc::Exception(std::string("UDP reliable call failed: ") + result_ec.message());
    }
    return;
  }
  
  // Fallback: use existing transport (synchronous call)
  get_session(endpoint)->send_receive(buffer, timeout_ms * (max_retries + 1));
}

NPRPC_API void RpcImpl::call_udp_reliable_async(
  const EndPoint& endpoint,
  flat_buffer&& buffer,
  std::optional<std::function<void(const boost::system::error_code&, flat_buffer&)>>&& completion_handler,
  uint32_t timeout_ms,
  uint32_t max_retries)
{
  if (endpoint.type() == EndPointType::Udp) {
    auto udp_conn = get_udp_connection(ioc_, std::string(endpoint.hostname()), endpoint.port());
    
    udp_conn->send_reliable_async(
      std::move(buffer),
      [handler = std::move(completion_handler)](const boost::system::error_code& ec, flat_buffer& response) mutable {
        if (handler) {
          (*handler)(ec, response);
        }
      },
      timeout_ms,
      max_retries
    );
    return;
  }
  
  // Fallback: use existing transport (async call)
  get_session(endpoint)->send_receive_async(
    std::move(buffer), std::move(completion_handler), timeout_ms * (max_retries + 1));
}

NPRPC_API void RpcImpl::call_async(
  const EndPoint&                                    endpoint,
  flat_buffer&&                                      buffer,
  std::optional<std::function<void(const boost::system::error_code&,
                                   flat_buffer&)>>&& completion_handler,
  uint32_t                                           timeout_ms)
{
  get_session(endpoint)->send_receive_async(
    std::move(buffer), std::move(completion_handler), timeout_ms);
}

NPRPC_API bool RpcImpl::prepare_zero_copy_buffer(
  const EndPoint& endpoint, flat_buffer& buffer, size_t max_size)
{
  if (!is_shared_memory(endpoint)) {
    return false;
  }
  
  try {
    auto session = get_session(endpoint);
    auto* shm_conn = dynamic_cast<SharedMemoryConnection*>(session.get());
    if (!shm_conn) {
      return false;
    }
    
    return shm_conn->prepare_write_buffer(buffer, max_size);
  } catch (const std::exception& e) {
    // Connection failed - fall back to normal buffer
    // The actual error will be thrown when call() is made
    return false;
  }
}

NPRPC_API std::optional<ObjectGuard> RpcImpl::get_object(
  poa_idx_t poa_idx, oid_t object_id)
{
  auto poa = g_orb->get_poa(poa_idx);
  if (!poa) return std::nullopt;
  return poa->get_object(object_id);
}

bool RpcImpl::has_session(
  const EndPoint& endpoint) const noexcept
{
  std::lock_guard<std::mutex> lk(connections_mut_);
  return std::find_if(opened_sessions_.begin(), opened_sessions_.end(),
    [endpoint](auto const& ptr) { return ptr->remote_endpoint() == endpoint; }) != opened_sessions_.end();
}


NPRPC_API SessionContext* RpcImpl::get_object_session_context(Object* obj)
{
  if (!obj) return nullptr;

  // We need to find the session context based on the endpoint
  auto session = g_orb->get_session(obj->get_endpoint());
  if (session) {
    return &session->ctx();
  }

  return nullptr;
}

bool RpcImpl::close_session(Session* session)
{
  std::lock_guard<std::mutex> lk(connections_mut_);
  if (auto it = std::find_if(opened_sessions_.begin(), opened_sessions_.end(),
    [session](auto const& ptr) { return ptr->remote_endpoint() == session->remote_endpoint(); })
    ; it != opened_sessions_.end())
  {
    opened_sessions_.erase(it);
  } else {
    std::cerr << "Error: session not found\n";
    return false;
  }
  return true;
}

ObjectPtr<common::Nameserver> RpcImpl::get_nameserver(
  std::string_view nameserver_ip)
{
  auto ip = std::string(nameserver_ip);
  ObjectPtr<common::Nameserver> obj(new common::Nameserver(0));
  detail::ObjectId& oid = obj->get_data();

  oid.object_id = 0ull;
  oid.poa_idx   = 0;
  oid.flags     = static_cast<nprpc::oflags_t>(detail::ObjectFlag::Persistent);
  oid.origin.fill(0);
  oid.class_id  = common::INameserver_Servant::_get_class();
  oid.urls.assign(
    "tcp://" + ip + ":15000;"
    "ws://"  + ip + ":15001;"
  );

  [[maybe_unused]] bool res = obj->select_endpoint();
  assert(res && "Nameserver must have a valid endpoint");

  return obj;
}

RpcImpl::RpcImpl(
  boost::asio::io_context& ioc)
    : ioc_ {ioc}
{
  poas_created_.fill(false);

  init_socket(ioc_);
  init_http_server(ioc_);
  init_shared_memory_listener(ioc_);
  init_udp_listener(ioc_);
}

void ReferenceListImpl::add_ref(ObjectServant* obj)
{
  // Check if we've exceeded the maximum references per session
  if (refs_.size() >= max_references_per_session) {
    std::cerr << "Maximum references per session exceeded (" << max_references_per_session 
              << "), rejecting AddReference for: " << obj->get_class() << '\n';
    return;
  }

  if (auto it = std::find_if(begin(refs_), end(refs_), [obj](auto& pair) { return pair.second == obj; })
    ; it != end(refs_)) 
  {
    std::cerr << "duplicate reference: " << obj->get_class() << '\n';
    return;
  }

  refs_.push_back({{obj->poa_index(), obj->oid()}, obj});
  obj->add_ref();
}

bool ReferenceListImpl::remove_ref(
  poa_idx_t poa_idx, oid_t oid)
{
  if (auto it = std::find_if(begin(refs_), end(refs_),
    [poa_idx, oid](auto& pair) { return pair.first.poa_idx == poa_idx && pair.first.object_id == oid; })
    ; it != end(refs_))
  {
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

NPRPC_API Object* create_object_from_flat(
  detail::flat::ObjectId_Direct direct,
  EndPoint remote_endpoint)
{
  if (direct.object_id() == invalid_object_id)
    return nullptr;

  auto  obj = std::unique_ptr<Object>(new Object());
  obj->local_ref_cnt_ = 1;

  auto& oid = obj->get_data();
  nprpc::detail::helpers::assign_from_flat_ObjectId(direct, oid);

  if (direct.flags() & static_cast<nprpc::oflags_t>(detail::ObjectFlag::Tethered)) 
  {
    // should always use existing session
    oid.urls       = remote_endpoint.to_string();
    obj->endpoint_ = remote_endpoint;
  } else {
    if (!obj->select_endpoint(remote_endpoint)) {
      // Something is malformed, we cannot select an endpoint
      // maybe I need to wrap `dispatch` in a try-catch block? I think I've already done it in `handle_request`
      throw nprpc::Exception(
        "Cannot select endpoint for object: " + std::string(oid.class_id) + ", available endpoints: " + obj->urls());
    }
  }

  return obj.release();
}

NPRPC_API void fill_guid(std::array<std::uint8_t, 16>& guid) noexcept {
  auto& g = impl::g_cfg.uuid;
  std::copy(g.begin(), g.end(), guid.begin());
}

ObjectId PoaImpl::activate_object(
    ObjectServant*  obj,
    uint32_t        activation_flags,
    SessionContext* ctx)
{
  ObjectId result;
  auto& oid                = result.get_data();
  auto  object_id_internal = id_to_ptr_.add(obj);

  if (object_id_internal == invalid_object_id)
    throw Exception("Poa fixed size has been exceeded");

  obj->poa_             = shared_from_this();
  obj->object_id_       = object_id_internal;
  obj->activation_time_ = std::chrono::system_clock::now();

  oid.object_id = object_id_internal;
  oid.poa_idx   = get_index();
  oid.flags     = 0;
  if (pl_lifespan_ == PoaPolicy::Lifespan::Persistent)
    oid.flags |= static_cast<oflags_t>(detail::ObjectFlag::Persistent);
  fill_guid(oid.origin);
  oid.class_id  = obj->get_class();

  using namespace std::string_literals;
  // hostname is preferred over localhost
  const std::string default_url =
    g_cfg.hostname.empty() ? "127.0.0.1"s : g_cfg.hostname;

  if (activation_flags & ObjectActivationFlags::ALLOW_TCP) {
    oid.urls += (std::string(tcp_prefix) + default_url + ":" +
                 std::to_string(g_cfg.listen_tcp_port)) + ';';
  }

  if (activation_flags & ObjectActivationFlags::ALLOW_WEBSOCKET) {
    oid.urls += (std::string(ws_prefix) + default_url + ":" +
                 std::to_string(g_cfg.listen_http_port)) + ';';
  }

  if (activation_flags & ObjectActivationFlags::ALLOW_SSL_WEBSOCKET) {
    if (g_cfg.hostname.empty()) {
      throw std::runtime_error("SSL websocket requires a hostname");
    }
    oid.urls += (std::string(wss_prefix) + g_cfg.hostname + ":" +
                 std::to_string(g_cfg.listen_http_port)) + ';';
  }

  if (activation_flags & ObjectActivationFlags::ALLOW_HTTP) {
    oid.urls += (std::string(http_prefix) + default_url + ":" +
                 std::to_string(g_cfg.listen_http_port)) + ';';
  }

  if (activation_flags & ObjectActivationFlags::ALLOW_SECURED_HTTP) {
    if (g_cfg.hostname.empty()) {
      throw std::runtime_error("Secured HTTP requires a hostname");
    }
    oid.urls += (std::string(https_prefix) + g_cfg.hostname + ":" +
                 std::to_string(g_cfg.listen_http_port)) + ';';
  }

  if (activation_flags & ObjectActivationFlags::ALLOW_SHARED_MEMORY) {
    oid.urls += (std::string(mem_prefix) + g_server_listener_uuid) + ';';
  }

  if (activation_flags & ObjectActivationFlags::ALLOW_UDP) {
    if (g_cfg.listen_udp_port == 0) {
      throw std::runtime_error("UDP port not configured. Use set_listen_udp_port()");
    }
    oid.urls += (std::string(udp_prefix) + default_url + ":" +
                 std::to_string(g_cfg.listen_udp_port)) + ';';
  }

  if (pl_lifespan_ == PoaPolicy::Lifespan::Transient) {
    // std::lock_guard<std::mutex> lk(g_orb->new_activated_objects_mut_);
    // g_orb->new_activated_objects_.push_back(obj);
    if (!ctx) throw std::runtime_error(
      "Object created with transient policy requires session context for activation");

    ctx->ref_list.add_ref(obj);
  }

  if (activation_flags & ObjectActivationFlags::SESSION_SPECIFIC) {
    obj->session_ctx_ = ctx;
    oid.flags |= static_cast<oflags_t>(detail::ObjectFlag::Tethered);
  }

  return result;
}

void PoaImpl::deactivate_object(oid_t object_id)
{
  auto obj = id_to_ptr_.get(object_id);
  if (obj) {
    obj->to_delete_.store(true);
    id_to_ptr_.remove(object_id);
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
    std::cerr << "delete_object: object is in use. id = " << obj->oid()
              << '\n';
    boost::asio::post(impl::g_orb->ioc(),
                      std::bind(&PoaImpl::delete_object, obj));
  }
}

} // namespace nprpc::impl

#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/nprpc_nameserver.hpp>

#include <cassert>

namespace nprpc::impl {

Poa* RpcImpl::create_poa_impl(uint32_t objects_max, PoaPolicy::Lifespan lifespan) {
  auto idx = poa_size_.fetch_add(1);
  auto poa = new PoaImpl(objects_max, idx);
  poas_[idx] = std::unique_ptr<PoaImpl>(poa);

  // Set the policy
  poa->pl_lifespan_ = lifespan;

  return poa;
}

extern void init_socket(boost::asio::io_context& ioc);
extern void init_http_server(boost::asio::io_context& ioc);

NPRPC_API Config   g_cfg;
NPRPC_API RpcImpl* g_orb;

void RpcImpl::destroy()
{
  delete this;
  g_orb = nullptr;
}

NPRPC_API std::shared_ptr<Session> RpcImpl::get_session(
  const EndPoint& endpoint)
{
  std::shared_ptr<Session> con;
  {
    std::lock_guard<std::mutex> lk(connections_mut_);
    if (auto it = std::find_if(opened_sessions_.begin(), opened_sessions_.end(),
                                [&endpoint](auto const& ptr) {
                                  return ptr->remote_endpoint() == endpoint;
                                }); it != opened_sessions_.end()) 
    {
      con = (*it);
    } else {
      switch(endpoint.type()) {
       case EndPointType::TcpTethered: 
          throw nprpc::ExceptionCommFailure();
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
        // con = std::make_shared<SharedMemoryConnection>(
        //   endpoint,
        //   ioc_
        // );
        break;
      default:
        throw nprpc::ExceptionCommFailure();
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
  return std::find_if(opened_sessions_.begin(),
                      opened_sessions_.end(),
                      [endpoint](auto const& ptr) {
                        return ptr->remote_endpoint() == endpoint;
                      }) != opened_sessions_.end();
}

bool RpcImpl::close_session(
  Session* session)
{
  std::lock_guard<std::mutex> lk(connections_mut_);
  if (auto founded = std::find_if(opened_sessions_.begin(),
                                  opened_sessions_.end(),
                                  [session](auto const& ptr) {
                                    return ptr->remote_endpoint() ==
                                           session->remote_endpoint();
                                  });
      founded != opened_sessions_.end()) {
    opened_sessions_.erase(founded);
  } else {
    std::cerr << "Error: session was not found\n";
    return false;
  }
  return true;
}

ObjectPtr<Nameserver> RpcImpl::get_nameserver(
  std::string_view nameserver_ip)
{
  auto ip = std::string(nameserver_ip);
  ObjectPtr<Nameserver> obj(new Nameserver(0));
  detail::ObjectId& oid = obj->get_data();

  oid.object_id = 0ull;
  oid.poa_idx   = 0;
  oid.flags     = (1 << static_cast<int>(detail::ObjectFlag::Lifespan));
  oid.origin.fill(0);
  oid.class_id  = INameserver_Servant::_get_class();
  oid.urls.assign(
    // "tcp://" + ip + ":15000;"
    "ws://"  + ip + ":15001;"
  );

  obj->select_endpoint();

  return obj;
}

RpcImpl::RpcImpl(
  boost::asio::io_context& ioc)
    : ioc_ {ioc}
{
  init_socket(ioc_);
  init_http_server(ioc_);
}

void ReferenceListImpl::add_ref(ObjectServant* obj)
{
  if (auto it =
        std::find_if(begin(refs_),
                     end(refs_),
                     [obj](auto& pair) { return pair.second == obj; });
      it != end(refs_)) {
    std::cerr << "duplicate reference: " << obj->get_class() << '\n';
    return;
  }

  refs_.push_back({{obj->poa_index(), obj->oid()}, obj});
  obj->add_ref();
}

bool ReferenceListImpl::remove_ref(
  poa_idx_t poa_idx, oid_t oid)
{
  if (auto it = std::find_if(begin(refs_),
                             end(refs_),
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
  for (auto& ref : refs_) {
    ref.second->release();
  }
}


NPRPC_API Object* create_object_from_flat(
  detail::flat::ObjectId_Direct direct,
  EndPoint remote_endpoint)
{
  if (direct.object_id() == invalid_object_id)
    return nullptr;

  // assert(remote_endpoint.type() == EndPointType::TcpTethered);

  auto  obj = new Object();
  obj->local_ref_cnt_ = 1;

  auto& oid = obj->get_data();
  nprpc_base::flat::assign_from_flat_ObjectId(direct, oid);

  if (direct.flags() & (1 << static_cast<std::underlying_type_t<detail::ObjectFlag>>(detail::ObjectFlag::Tethered))) 
  {
    // should always use existing session
    oid.urls       = remote_endpoint.to_string();
    obj->endpoint_ = remote_endpoint;
  } else {
    obj->select_endpoint(remote_endpoint);
  }

  return obj;
}

NPRPC_API void fill_guid(std::array<std::uint8_t, 16>& guid) noexcept {
  auto& g = impl::g_cfg.uuid;
  std::copy(g.begin(), g.end(), guid.begin());
}

} // namespace nprpc::impl

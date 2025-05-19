// Copyright (c) 2021 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by
// LICENSING file in the topmost directory

#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/impl/uuid.hpp>
#include <nprpc/nprpc_nameserver.hpp>

#include <fstream>
#include <functional>

using namespace nprpc;

namespace nprpc::impl {

extern void init_socket(boost::asio::io_context& ioc);
extern void init_http_server(boost::asio::io_context& ioc);

NPRPC_API Config   g_cfg;
NPRPC_API RpcImpl* g_orb;

void RpcImpl::destroy()
{
  delete this;
  g_orb = nullptr;
}

Poa* RpcImpl::create_poa(
  uint32_t objects_max, std::initializer_list<Policy*> policy_list)
{
  auto idx   = poa_size_.fetch_add(1);
  auto poa   = new PoaImpl(objects_max, idx);
  poas_[idx] = std::unique_ptr<PoaImpl>(poa);
  for (auto policy : policy_list) {
    policy->apply_policy(poa);
  }
  return poa;
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

class ReferenceListImpl
{
  std::vector<std::pair<nprpc::detail::ObjectIdLocal, ObjectServant*>> refs_;

 public:
  void add_ref(
    ObjectServant* obj)
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

  bool remove_ref(
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

  ~ReferenceListImpl()
  {
    for (auto& ref : refs_) {
      ref.second->release();
    }
  }
};

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

}  // namespace nprpc::impl

namespace nprpc {

NPRPC_API RpcBuilder::RpcBuilder() {
	auto& uuid = impl::SharedUUID::instance().get();
  memcpy(cfg_.uuid.data(), &uuid, 16);
}

NPRPC_API Rpc* RpcBuilder::build(boost::asio::io_context& ioc)
{
  using namespace impl;

  if (impl::g_orb) 
    throw Exception("rpc has been previously initialized");

  auto& ctx = cfg_.ssl_context;

  if (use_ssl_websocket_server_) {
    ctx = ssl::context{ssl::context::tlsv12};

    auto read_file_to_string = [](std::string const file) {
      std::ifstream is(file, std::ios_base::in);
      if (!is) { throw std::runtime_error("could not open certificate file: \"" + file + "\""); }
      return std::string(std::istreambuf_iterator<char>(is),
                         std::istreambuf_iterator<char>());
    };

    std::string const cert = read_file_to_string(ssl_public_key_path_);
    std::string const key = read_file_to_string(ssl_secret_key_path_);

    // ctx.set_password_callback(
    //     [](std::size_t, ssl::context_base::password_purpose) {
    //       return "test";
    //     });

    ctx.set_options(
        boost::asio::ssl::context::default_workarounds |
        boost::asio::ssl::context::no_sslv2 |
        boost::asio::ssl::context::no_sslv3 |
        (ssl_dh_params_path_.size() > 0 ? ssl::context::single_dh_use : 0));

    ctx.use_certificate_chain(
        boost::asio::buffer(cert.data(), cert.size()));

    ctx.use_private_key(
        boost::asio::buffer(key.data(), key.size()),
        boost::asio::ssl::context::file_format::pem);

    if (ssl_dh_params_path_.size() > 0) {
      std::string const dh = read_file_to_string(ssl_dh_params_path_);
      ctx.use_tmp_dh(
          boost::asio::buffer(dh.data(), dh.size()));
    }
  }

  ctx.set_default_verify_paths();
  ctx.set_verify_mode(ssl::verify_peer);

  impl::g_cfg = std::move(cfg_);
  impl::g_orb = new impl::RpcImpl(ioc);
  return impl::g_orb;
}

void Policy_Lifespan::apply_policy(
  Poa* poa)
{
  static_cast<impl::PoaImpl*>(poa)->pl_lifespan = this->policy_;
}

uint32_t ObjectServant::release() noexcept
{
  if (static_cast<impl::PoaImpl*>(poa_)->pl_lifespan ==
      Policy_Lifespan::Type::Persistent) {
    return 1;
  }

  assert(is_unused() == false);

  auto cnt = ref_cnt_.fetch_sub(1, std::memory_order_acquire) - 1;
  if (cnt == 0) {
    static_cast<impl::PoaImpl*>(poa_)->deactivate_object(object_id_);
    impl::PoaImpl::delete_object(this);
  }

  return cnt;
}

NPRPC_API uint32_t Object::add_ref()
{
  auto const cnt = local_ref_cnt_.fetch_add(1, std::memory_order_release);
  if (policy_lifespan() == Policy_Lifespan::Type::Persistent || cnt)
    return cnt + 1;

  flat_buffer buf;

  auto constexpr msg_size =
    sizeof(impl::Header) + sizeof(::nprpc::detail::flat::ObjectIdLocal);
  auto mb = buf.prepare(msg_size);
  buf.commit(msg_size);

  static_cast<impl::Header*>(mb.data())->size   = msg_size - 4;
  static_cast<impl::Header*>(mb.data())->msg_id = impl::MessageId::AddReference;
  static_cast<impl::Header*>(mb.data())->msg_type = impl::MessageType::Request;

  ::nprpc::detail::flat::ObjectIdLocal_Direct msg(buf, sizeof(impl::Header));
  msg.object_id() = object_id();
  msg.poa_idx()   = poa_idx();

  nprpc::impl::g_orb->call_async(get_endpoint(), std::move(buf), std::nullopt);

  return cnt + 1;
}

NPRPC_API uint32_t Object::release()
{
  auto cnt = --local_ref_cnt_;
  if (cnt != 0) return cnt;

  if (policy_lifespan() == Policy_Lifespan::Type::Transient) {
    const auto& endpoint = get_endpoint();

    if (endpoint.type() == EndPointType::TcpTethered &&
        ::nprpc::impl::g_orb->has_session(endpoint) == false) {
      // session was closed and cannot be created.
    } else {
      flat_buffer buf;

      auto constexpr msg_size =
        sizeof(impl::Header) + sizeof(::nprpc::detail::flat::ObjectIdLocal);
      auto mb = buf.prepare(msg_size);
      buf.commit(msg_size);

      static_cast<impl::Header*>(mb.data())->size = msg_size - 4;
      static_cast<impl::Header*>(mb.data())->msg_id =
        impl::MessageId::ReleaseObject;
      static_cast<impl::Header*>(mb.data())->msg_type =
        impl::MessageType::Request;

      ::nprpc::detail::flat::ObjectIdLocal_Direct msg(buf,
                                                      sizeof(impl::Header));
      msg.object_id() = object_id();
      msg.poa_idx()   = poa_idx();

      try {
        ::nprpc::impl::g_orb->call_async(
          endpoint,
          std::move(buf),
          [](const boost::system::error_code&, flat_buffer&) {
            // if (!ec) {
            // auto std_reply = nprpc::impl::handle_standart_reply(buf);
            // if (std_reply == false) {
            //	std::cerr << "received an unusual reply for function with no
            // output arguments" << std::endl;
            // }
            //}
          });
      } catch (Exception& ex) {
        std::cerr << ex.what() << '\n';
      }
    }
  }

  delete this;

  return 0;
}

NPRPC_API void Object::select_endpoint(std::optional<EndPoint> remote_endpoint)
{
  std::string& urls = data_.urls;
  auto start = [&urls, this, &remote_endpoint] {
    const auto same_machine = is_same_origin(impl::g_cfg.uuid);
    auto try_replace_ip = [&] (size_t pos, std::string_view prefix) {
      if (same_machine || !remote_endpoint )
        return;

      auto start = pos + prefix.length();
      auto end = urls.find(':', start);
      auto ipv4_str = urls.substr(start, end - start);

      boost::system::error_code ec;
      auto ipv4_addr = nprpc::impl::net::ip::make_address_v4(ipv4_str, ec);
      if ((!ec && ipv4_addr.to_uint() == 0x7F000001) || ipv4_str == "localhost") {
        // Change ip from localhost or 127.0.0.1 to ip of the remote endpoint
        auto remote_ip = remote_endpoint->hostname();
        urls = urls.substr(0, start) + std::string(remote_ip) + urls.substr(end);
      }
    };
    
    size_t pos = std::string::npos, pos2 = std::string::npos;
    if (same_machine && ((pos = urls.find(mem_prefix)) != std::string::npos)) {
      // Prefer shared memory if possible
      return pos;
    }

    if ((pos = urls.find(tcp_prefix)) != std::string::npos)
      try_replace_ip(pos, tcp_prefix);
    
    if ((pos2 = urls.find(ws_prefix)) != std::string::npos)
      try_replace_ip(pos2, ws_prefix);

    if (pos != std::string::npos)
      return pos;

    if (pos2 != std::string::npos)
      return pos2;

    if ((pos = urls.find(wss_prefix)) == std::string::npos)
      throw std::runtime_error("No valid endpoint found");

    return pos;
    } ();

  auto end = urls.find(';', start);
  auto size = (end != std::string::npos) ? end - start : std::string::npos;
  endpoint_ = EndPoint(urls.substr(start, size));
}

NPRPC_API uint32_t ObjectServant::add_ref() noexcept
{
  auto cnt = ref_cnt_.fetch_add(1, std::memory_order_release) + 1;

  //	if (cnt == 1 && static_cast<impl::PoaImpl*>(poa())->pl_lifespan ==
  // Policy_Lifespan::Transient) { 		std::lock_guard<std::mutex>
  // lk(impl::g_orb->new_activated_objects_mut_); 		auto& list =
  // impl::g_orb->new_activated_objects_; list.erase(std::find(begin(list),
  // end(list), this));
  //	}

  return cnt;
}

ReferenceList::ReferenceList() noexcept
{
  impl_ = new impl::ReferenceListImpl();
}

ReferenceList::~ReferenceList() { delete impl_; }

void ReferenceList::add_ref(
  ObjectServant* obj)
{
  impl_->add_ref(obj);
}

bool ReferenceList::remove_ref(
  poa_idx_t poa_idx, oid_t oid)
{
  return impl_->remove_ref(poa_idx, oid);
}

}  // namespace nprpc

#include <nprpc/serialization/nvp.hpp>

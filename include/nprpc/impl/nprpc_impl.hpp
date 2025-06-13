// Copyright (c) 2021 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by
// LICENSING file in the topmost directory

#pragma once

#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/core/exchange.hpp>
#include <mutex>
#include <cassert>
#include <iostream>
#include <iomanip>
#include <stdexcept>
#include <mutex>
#include <optional>
#include <deque>

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>

#include <nprpc/nprpc.hpp>
#include <nprpc/impl/session.hpp>
#include <nprpc/impl/id_to_ptr.hpp>
#include <nprpc/impl/websocket_session.hpp>

namespace nprpc::impl {

NPRPC_API void fill_guid(std::array<std::uint8_t, 16>& guid) noexcept;

class RpcImpl;
class PoaImpl;

NPRPC_API extern Config   g_cfg;
NPRPC_API extern RpcImpl* g_orb;

struct IOWork {
  virtual void operator()()                       = 0;
  virtual void on_failed(
    const boost::system::error_code& ec) noexcept = 0;
  virtual void on_executed() noexcept             = 0;
  virtual flat_buffer& buffer() noexcept          = 0;
  virtual ~IOWork() = default;
};

template<typename T>
class CommonConnection {
  T* derived() noexcept
  {
    return static_cast<T*>(this);
  }
protected:
  std::deque<std::unique_ptr<IOWork>> wq_;

  void add_work(std::unique_ptr<IOWork>&& w) {
    boost::asio::post(derived()->get_executor(), [
      w{std::move(w)}, this
    ] () mutable {
      // work may or may not hold shared_ptr to this:
      // blocking case - it will reference to this,
      // async case - it will hold shared_ptr to this
      wq_.push_back(std::move(w));
      if (wq_.size() == 1) (*wq_.front())();
    });
  }

  flat_buffer& current_rx_buffer() noexcept
  {
    assert(wq_.size() > 0);
    return wq_.front()->buffer();
  }

  void pop_and_execute_next_task()
  {
    wq_.pop_front();
    if (wq_.empty() == false) (*wq_.front())();
  }
};

class SocketConnection 
  : public Session
  , public CommonConnection<SocketConnection>
  , public std::enable_shared_from_this<SocketConnection>
{
  // this endpoint is used to reconnect
  // if the connection is lost
  boost::asio::ip::tcp::endpoint    endpoint_;
  boost::asio::ip::tcp::socket      socket_;
  uint32_t                          rx_size_ = 0;

  void reconnect();

 protected:
  virtual void timeout_action() final
  {
    boost::system::error_code ec;
    socket_.cancel(ec);
    if (ec) fail(ec, "socket::cancel()");
  }

 public:
  auto get_executor() noexcept
  {
    return socket_.get_executor();
  }

  void send_receive(flat_buffer& buffer, uint32_t timeout_ms) override;

  void send_receive_async(
    flat_buffer&&                                      buffer,
    std::optional<std::function<void(const boost::system::error_code&,
                                     flat_buffer&)>>&& completion_handler,
    uint32_t                                           timeout_ms) override;

  void on_read_size(const boost::system::error_code& ec, size_t len);
  void on_read_body(const boost::system::error_code& ec, size_t len);
  void do_read_size();
  void do_read_body();

  template<typename WriteHandler>
  void write_async(
    const flat_buffer& buf, WriteHandler&& handler)
  {
    timeout_timer_.expires_from_now(timeout_);
    boost::asio::async_write(socket_, buf.cdata(), std::forward<WriteHandler>(handler));
  }

  SocketConnection(const EndPoint&                endpoint,
                   boost::asio::ip::tcp::socket&& socket);
};

class RpcImpl : public Rpc
{
  friend nprpc::PoaBuilder;

  static constexpr size_t                  max_poa_objects = 6;
  static constexpr std::uint16_t           invalid_port    = -1;

  boost::asio::io_context&                 ioc_;
  std::mutex                               poas_mut_;
  std::array<
    std::unique_ptr<PoaImpl>,
    max_poa_objects>                       poas_;
  std::array<bool, max_poa_objects>        poas_created_;
  mutable std::mutex                       connections_mut_;
  std::vector<std::shared_ptr<Session>>    opened_sessions_;

 public:
  uint16_t port() const noexcept { return g_cfg.listen_tcp_port; }
  uint16_t websocket_port() const noexcept { return g_cfg.listen_http_port; }

  void add_connection(
    std::shared_ptr<Session>&& session)
  {
    opened_sessions_.push_back(std::move(session));
  }

  bool      has_session(const EndPoint& endpoint) const noexcept;
  NPRPC_API std::shared_ptr<Session> get_session(const EndPoint& endpoint);
  NPRPC_API void                     call(const EndPoint& endpoint,
                                          flat_buffer&    buffer,
                                          uint32_t        timeout_ms = 2500);

  NPRPC_API void call_async(
    const EndPoint&                                    endpoint,
    flat_buffer&&                                      buffer,
    std::optional<std::function<void(const boost::system::error_code&,
                                     flat_buffer&)>>&& completion_handler,
    uint32_t                                           timeout_ms = 2500);

  NPRPC_API std::optional<ObjectGuard> get_object(poa_idx_t poa_idx, oid_t oid);

  NPRPC_API SessionContext* get_object_session_context(Object* obj) override;

  boost::asio::io_context& ioc() noexcept { return ioc_; }
  // void start() override;
  void                          destroy() override;
  void                          destroy_poa(Poa* poa) override;
  bool                          close_session(Session* con);
  virtual ObjectPtr<Nameserver> get_nameserver(
    std::string_view nameserver_ip) override;
  //	void check_unclaimed_objects(boost::system::error_code ec);

  PoaImpl* get_poa(
    uint16_t idx)
  {
    if (idx >= max_poa_objects) {
      throw std::out_of_range("Poa index out of range");
    }
    return poas_[idx].get();
  }

  RpcImpl(boost::asio::io_context& ioc);
protected:
 Poa* create_poa_impl(uint32_t max_objects, PoaPolicy::Lifespan lifespan);
};

class ObjectGuard
{
  ObjectServant* obj_;

 public:
  ObjectServant* get() noexcept
  {
    return !obj_->to_delete_.load() ? obj_ : nullptr;
  }

  explicit ObjectGuard(
    ObjectServant* obj) noexcept
      : obj_(obj)
  {
    ++obj->in_use_cnt_;
  }

  ObjectGuard(
    ObjectGuard&& other) noexcept
      : obj_(boost::exchange(other.obj_, nullptr))
  {
  }

  ~ObjectGuard()
  {
    if (obj_) --obj_->in_use_cnt_;
  }

  ObjectGuard(const ObjectGuard&)            = delete;
  ObjectGuard& operator=(const ObjectGuard&) = delete;
};

class PoaImpl : public Poa
{
  friend RpcImpl;
  IdToPtr<ObjectServant*> id_to_ptr_;

  explicit PoaImpl(
    uint32_t objects_max, uint16_t idx)
      : Poa(idx), id_to_ptr_ {objects_max}
  {
  }

  PoaPolicy::Lifespan pl_lifespan_;
 public:
  auto get_lifespan() const noexcept
  {
    return pl_lifespan_;
  }

  virtual ~PoaImpl()
  {
    if (pl_lifespan_ == PoaPolicy::Lifespan::Persistent)
      return;
    // TODO: remove references
  }

  std::optional<ObjectGuard> get_object(
    oid_t oid) noexcept
  {
    auto obj = id_to_ptr_.get(oid);
    if (obj) return ObjectGuard(obj);
    return std::nullopt;
  }

  virtual ObjectId activate_object(
    ObjectServant*  obj,
    uint32_t        activation_flags,
    SessionContext* ctx)
  {
    ObjectId result;
    auto& oid                = result.get_data();
    auto  object_id_internal = id_to_ptr_.add(obj);

    if (object_id_internal == invalid_object_id)
      throw Exception("Poa fixed size has been exceeded");

    obj->poa_             = this;
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
      oid.urls +=
        (std::string(tcp_prefix) + default_url + ":" + std::to_string(g_cfg.listen_tcp_port)) + ';';
    }

    if (activation_flags & ObjectActivationFlags::ALLOW_WEBSOCKET) {
      oid.urls +=
        (std::string(ws_prefix) + default_url + ":" + std::to_string(g_cfg.listen_http_port)) +
        ';';
    }

    if (activation_flags & ObjectActivationFlags::ALLOW_SSL_WEBSOCKET) {
      if (g_cfg.hostname.empty()) {
        throw std::runtime_error("SSL websocket requires hostname");
      }
      oid.urls += (std::string(wss_prefix) + g_cfg.hostname + ":" +
                   std::to_string(g_cfg.listen_http_port));
    }

    if (activation_flags & ObjectActivationFlags::ALLOW_MEMORY_MAPPED) {
      /* TODO: implement shared memory
      oid.urls +=
        (std::string(mem_prefix) + g_cfg.http_root_dir + ":" +
         std::to_string(g_cfg.shared_memory_port)) +
        ';';
        */
    }

    if (pl_lifespan_ == PoaPolicy::Lifespan::Transient) {
      // std::lock_guard<std::mutex> lk(g_orb->new_activated_objects_mut_);
      // g_orb->new_activated_objects_.push_back(obj);
      if (!ctx)
        throw std::runtime_error(
          "Object created with transient policy requires session context for "
          "activation");

      ctx->ref_list.add_ref(obj);
    }

    if (activation_flags & ObjectActivationFlags::SESSION_SPECIFIC) {
      obj->session_ctx_ = ctx;
      oid.flags |= static_cast<oflags_t>(detail::ObjectFlag::Tethered);
    }

    return result;
  }

  virtual void deactivate_object(
    oid_t object_id) override
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

  static void delete_object(
    ObjectServant* obj)
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
};

inline void make_simple_answer(
  flat_buffer& buf, MessageId id, uint32_t request_id = 0)
{
  assert(id == MessageId::Success             || 
    id == MessageId::Error_ObjectNotExist     ||
    id == MessageId::Error_CommFailure        ||
    id == MessageId::Error_UnknownFunctionIdx ||
    id == MessageId::Error_UnknownMessageId   ||
    id == MessageId::Error_BadAccess          ||
    id == MessageId::Error_BadInput
  );

  static_assert(std::is_standard_layout_v<impl::flat::Header>,
    "impl::flat::Header must be POD type");

  if (!request_id && buf.size() >= sizeof(impl::flat::Header)) {
    auto header = static_cast<const impl::flat::Header*>(buf.cdata().data());
    request_id = header->request_id;
  }

  // clear the read buffer
  buf.consume(buf.size());

  auto mb = buf.prepare(sizeof(impl::flat::Header));
  auto header = static_cast<impl::flat::Header*>(mb.data());
  header->size        = sizeof(impl::flat::Header) - 4;
  header->msg_id      = id;
  header->msg_type    = impl::MessageType::Answer;
  header->request_id  = request_id;

  buf.commit(sizeof(impl::flat::Header));
}

inline void dump_message(
  flat_buffer& buffer, bool rx)
{
  auto cb   = buffer.cdata();
  auto size = cb.size();
  auto data = (unsigned char*)cb.data();

  std::cout << (rx ? "rx. size: " : "tx. size: ") << size << "\n";
  std::cout << std::hex;
  for (size_t i = 0; i < size; ++i) {
    std::cout << std::setfill('0') << std::setw(2) << (int)data[i];
  }
  std::cout << std::dec << std::endl;
}

//  0 - Success
//  1 - exception
// -1 - not handled
inline int handle_standart_reply(
  flat_buffer& buf)
{
  if (buf.size() < sizeof(impl::flat::Header))
    throw ExceptionBadInput();
  auto header = static_cast<const impl::flat::Header*>(buf.cdata().data());
  assert(header->size == buf.size() - 4);
  switch (header->msg_id) {
    case MessageId::Success:
      return 0;
    case MessageId::Exception:
      return 1;
    case MessageId::Error_ObjectNotExist:
      throw ExceptionObjectNotExist();
    case MessageId::Error_CommFailure:
      throw ExceptionCommFailure();
    case MessageId::Error_UnknownFunctionIdx:
      throw ExceptionUnknownFunctionIndex();
    case MessageId::Error_UnknownMessageId:
      throw ExceptionUnknownMessageId();
    case MessageId::Error_BadAccess:
      throw ExceptionBadAccess();
    case MessageId::Error_BadInput:
      throw ExceptionBadInput();
    default:
      return -1;
  }
}

inline void Session::close()
{
  closed_ = true;
  impl::g_orb->close_session(this);
}

class ReferenceListImpl
{
  std::vector<std::pair<nprpc::detail::ObjectIdLocal, ObjectServant*>> refs_;
 public:
  ~ReferenceListImpl();

  void add_ref(ObjectServant* obj);
  bool remove_ref(poa_idx_t poa_idx, oid_t oid);
};



}  // namespace nprpc::impl

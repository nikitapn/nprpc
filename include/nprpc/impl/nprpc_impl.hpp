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
#include <condition_variable>

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
NPRPC_API extern std::string g_server_listener_uuid;

// Forward declarations of work types
struct SocketConnectionWork;
struct SharedMemoryConnectionWork;

// Traits to get work type (defined after connection classes)
template<typename T> struct connection_work_traits;

template<typename T>
class CommonConnection {
  T* derived() noexcept
  {
    return static_cast<T*>(this);
  }
protected:
  using work_type = typename connection_work_traits<T>::type;
  std::deque<work_type> wq_;

  void add_work(work_type&& w) {
    boost::asio::post(derived()->get_executor(), [
      w{std::move(w)}, this
    ] () mutable {
      wq_.push_back(std::move(w));
      if (wq_.size() == 1) wq_.front()();
    });
  }

  flat_buffer& current_rx_buffer() noexcept
  {
    assert(wq_.size() > 0);
    return wq_.front().buffer();
  }

public:
  void pop_and_execute_next_task()
  {
    wq_.pop_front();
    if (wq_.empty() == false) wq_.front()();
  }
};

class SocketConnection;  // Forward declaration

// SocketConnection's work type - unified for sync and async
struct SocketConnectionWork {
  flat_buffer buf_storage;  // Owns buffer for async, unused for sync
  flat_buffer* buf_ptr;      // Points to buffer (external for sync, buf_storage for async)
  SocketConnection* conn_ptr;  // Raw pointer for sync
  std::shared_ptr<SocketConnection> conn_shared;  // Shared ptr for async
  uint32_t timeout_ms;
  
  // Sync-only members
  std::mutex* mtx_ptr = nullptr;
  std::condition_variable* cv_ptr = nullptr;
  bool* done_ptr = nullptr;
  boost::system::error_code* result_ptr = nullptr;
  
  // Async-only member
  std::optional<std::function<void(const boost::system::error_code&, flat_buffer&)>> async_handler;

  // Sync constructor
  SocketConnectionWork(flat_buffer& buf, SocketConnection& conn, uint32_t timeout,
                      std::mutex& mtx, std::condition_variable& cv, bool& done, 
                      boost::system::error_code& result)
    : buf_ptr(&buf)
    , conn_ptr(&conn)
    , timeout_ms(timeout)
    , mtx_ptr(&mtx)
    , cv_ptr(&cv)
    , done_ptr(&done)
    , result_ptr(&result)
  {
  }

  // Async constructor
  SocketConnectionWork(flat_buffer&& buf, std::shared_ptr<SocketConnection> conn, uint32_t timeout,
                      std::optional<std::function<void(const boost::system::error_code&, flat_buffer&)>>&& handler)
    : buf_storage(std::move(buf))
    , buf_ptr(&buf_storage)
    , conn_shared(std::move(conn))
    , conn_ptr(conn_shared.get())
    , timeout_ms(timeout)
    , async_handler(std::move(handler))
  {
  }

  // Make it movable
  SocketConnectionWork(SocketConnectionWork&&) = default;
  SocketConnectionWork& operator=(SocketConnectionWork&&) = default;
  SocketConnectionWork(const SocketConnectionWork&) = delete;
  SocketConnectionWork& operator=(const SocketConnectionWork&) = delete;

  void operator()() noexcept;

  inline void on_failed(const boost::system::error_code& ec) noexcept {
    if (mtx_ptr) {
      // Sync path
      {
        std::lock_guard<std::mutex> lock(*mtx_ptr);
        *result_ptr = ec;
        *done_ptr = true;
      }
      cv_ptr->notify_one();
    } else {
      // Async path
      if (async_handler) async_handler.value()(ec, *buf_ptr);
    }
  }

  inline void on_executed() noexcept {
    if (mtx_ptr) {
      // Sync path
      {
        std::lock_guard<std::mutex> lock(*mtx_ptr);
        *result_ptr = boost::system::error_code{};
        *done_ptr = true;
      }
      cv_ptr->notify_one();
    } else {
      // Async path
      if (async_handler) async_handler.value()(boost::system::error_code{}, *buf_ptr);
    }
  }

  inline flat_buffer& buffer() noexcept { return *buf_ptr; }
};

class SharedMemoryConnection;  // Forward declaration
class SharedMemoryChannel;     // Forward declaration

// SharedMemoryConnection's work type - unified for sync and async
struct SharedMemoryConnectionWork {
  flat_buffer buf_storage;  // Owns buffer for async, unused for sync
  flat_buffer* buf_ptr;      // Points to buffer (external for sync, buf_storage for async)
  SharedMemoryConnection* conn_ptr;  // Raw pointer for sync
  std::shared_ptr<SharedMemoryConnection> conn_shared;  // Shared ptr for async
  uint32_t timeout_ms;
  
  // Sync-only members
  std::mutex* mtx_ptr = nullptr;
  std::condition_variable* cv_ptr = nullptr;
  bool* done_ptr = nullptr;
  boost::system::error_code* result_ptr = nullptr;
  
  // Async-only member
  std::optional<std::function<void(const boost::system::error_code&, flat_buffer&)>> async_handler;

  // Sync constructor
  SharedMemoryConnectionWork(flat_buffer& buf, SharedMemoryConnection& conn, uint32_t timeout,
                            std::mutex& mtx, std::condition_variable& cv, bool& done, 
                            boost::system::error_code& result);

  // Async constructor
  SharedMemoryConnectionWork(flat_buffer&& buf, std::shared_ptr<SharedMemoryConnection> conn, uint32_t timeout,
                            std::optional<std::function<void(const boost::system::error_code&, flat_buffer&)>>&& handler);

  // Make it movable
  SharedMemoryConnectionWork(SharedMemoryConnectionWork&&) = default;
  SharedMemoryConnectionWork& operator=(SharedMemoryConnectionWork&&) = default;
  SharedMemoryConnectionWork(const SharedMemoryConnectionWork&) = delete;
  SharedMemoryConnectionWork& operator=(const SharedMemoryConnectionWork&) = delete;

  void operator()() noexcept;
  void on_failed(const boost::system::error_code& ec) noexcept;
  void on_executed() noexcept;
  flat_buffer& buffer() noexcept { return *buf_ptr; }
};

// Traits specializations
template<>
struct connection_work_traits<SocketConnection> {
  using type = SocketConnectionWork;
};

template<>
struct connection_work_traits<SharedMemoryConnection> {
  using type = SharedMemoryConnectionWork;
};

class SocketConnection
  : public Session
  , public CommonConnection<SocketConnection>
  , public std::enable_shared_from_this<SocketConnection>
{
private:
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

inline void SocketConnectionWork::operator()() noexcept {
  conn_ptr->set_timeout(timeout_ms);
  conn_ptr->write_async(*buf_ptr, [this](const boost::system::error_code& ec, size_t bytes_transferred) {
    boost::ignore_unused(bytes_transferred);
    if (ec) {
      on_failed(ec);
      conn_ptr->pop_and_execute_next_task();
      return;
    }
    conn_ptr->do_read_size();
  });
}

class RpcImpl : public Rpc
{
  friend PoaBuilder;

  static constexpr size_t                  max_poa_objects = 6;
  static constexpr std::uint16_t           invalid_port    = -1;

  boost::asio::io_context&                 ioc_;
  std::mutex                               poas_mut_;
  std::array<
    std::shared_ptr<PoaImpl>,
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
  virtual ObjectPtr<common::Nameserver> get_nameserver(
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

class PoaImpl
  : public Poa
  , public std::enable_shared_from_this<PoaImpl>
{
  IdToPtr<ObjectServant*> id_to_ptr_;
  PoaPolicy::Lifespan     pl_lifespan_;
 public:
  auto get_lifespan() const noexcept
  {
    return pl_lifespan_;
  }

  virtual ~PoaImpl()
  {
  }

  std::optional<ObjectGuard> get_object(oid_t oid) noexcept
  {
    auto obj = id_to_ptr_.get(oid);
    if (obj) return ObjectGuard(obj);
    return std::nullopt;
  }

  ObjectId activate_object(
    ObjectServant*  obj,
    uint32_t        activation_flags,
    SessionContext* ctx) override;

  void deactivate_object(oid_t object_id) override;
 
  static void delete_object(ObjectServant* obj);

  PoaImpl(uint32_t objects_max, uint16_t idx, PoaPolicy::Lifespan lifespan)
      : Poa(idx)
      , id_to_ptr_{objects_max}
      , pl_lifespan_{lifespan}
  {
  }
};

inline void make_simple_answer(
  flat_buffer& buf, MessageId id, uint32_t request_id = 0)
{
  assert(
    id == MessageId::Success                  ||
    id == MessageId::Error_PoaNotExist        ||
    id == MessageId::Error_ObjectNotExist     ||
    id == MessageId::Error_CommFailure        ||
    id == MessageId::Error_UnknownFunctionIdx ||
    id == MessageId::Error_UnknownMessageId   ||
    id == MessageId::Error_BadAccess          ||
    id == MessageId::Error_BadInput
  );

  static_assert(std::is_standard_layout_v<impl::flat::Header>,
    "impl::flat::Header must be a standard layout type");

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

  std::cout << "[nprpc] Message HEX16: " << (rx ? "rx. size: " : "tx. size: ") << size << "\n";
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
  std::vector<std::pair<detail::ObjectIdLocal, ObjectServant*>> refs_;
 public:
  ~ReferenceListImpl();

  void add_ref(ObjectServant* obj);
  bool remove_ref(poa_idx_t poa_idx, oid_t oid);
};



}  // namespace nprpc::impl

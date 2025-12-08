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
#include <optional>
#include <deque>
#include <variant>
#include <atomic>
#include <memory>

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>

#include <nprpc/nprpc.hpp>
#include <nprpc/impl/session.hpp>
#include <nprpc/impl/id_to_ptr.hpp>
#include <nprpc/impl/websocket_session.hpp>

namespace nprpc::impl {

struct Config {
  DebugLevel                 debug_level = ::nprpc::DebugLevel::DebugLevel_Critical;
  uuid_t                     uuid;
  std::string                hostname;
  std::string                listen_address    = "0.0.0.0";
  uint16_t                   listen_tcp_port   = 0;
  uint16_t                   listen_http_port  = 0;  // Used for both HTTP/1.1 and HTTP/3
  uint16_t                   listen_udp_port   = 0;
  uint16_t                   listen_quic_port  = 0;
  bool                       http3_enabled     = false;  // Enable HTTP/3 on same port as HTTP
  bool                       ssr_enabled       = false;  // Enable node worker for SSR
  std::string                http_cert_file;             // TLS cert for HTTP/3
  std::string                http_key_file;              // TLS key for HTTP/3
  std::string                quic_cert_file;
  std::string                quic_key_file;
  std::string                http_root_dir;
  std::string                ssr_handler_dir;            // Path to SSR handler (index.js)
  ssl::context               ssl_context_server{ssl::context::tlsv13_server};
  ssl::context               ssl_context_client{ssl::context::tlsv13_client};
};

NPRPC_API void fill_guid(std::array<std::uint8_t, 16>& guid) noexcept;

class RpcImpl;
class PoaImpl;

NPRPC_API extern Config   g_cfg;
NPRPC_API extern RpcImpl* g_rpc;
NPRPC_API extern std::string g_server_listener_uuid;

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
  std::deque<std::shared_ptr<IOWork>> wq_;

  void add_work(std::shared_ptr<IOWork> w)
  {
    boost::asio::post(derived()->get_executor(), [w{std::move(w)}, this] () mutable
    {
      // work may or may not hold shared_ptr to this:
      // blocking case - it will reference to this,
      // async case - it will hold shared_ptr to this
      wq_.push_back(std::move(w));
      if (wq_.size() == 1)
        (*wq_.front())();
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
    if (wq_.empty() == false)
      (*wq_.front())();
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

  // UNTESTED
  void shutdown() override {
    Session::shutdown();
    boost::system::error_code ec;
    socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
    socket_.close(ec);
  }

  SocketConnection(const EndPoint&                endpoint,
                   boost::asio::ip::tcp::socket&& socket);
};

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

  /**
   * @brief Send UDP datagram (fire-and-forget, no reply expected)
   * 
   * Used for unreliable UDP transport where no acknowledgment is needed.
   * The buffer is sent and immediately discarded.
   * 
   * @param endpoint Target UDP endpoint
   * @param buffer Message buffer to send (moved)
   */
  NPRPC_API void send_udp(const EndPoint& endpoint, flat_buffer&& buffer);

  /**
   * @brief Send reliable UDP call and wait for response
   * 
   * Used for [reliable] UDP methods that need acknowledgment and response.
   * Implements timeout and automatic retransmission.
   * 
   * @param endpoint Target UDP endpoint
   * @param buffer Message buffer to send (input/output - response overwrites)
   * @param timeout_ms Timeout per attempt in milliseconds
   * @param max_retries Maximum retransmit attempts before failure
   */
  NPRPC_API void call_udp_reliable(const EndPoint& endpoint, 
                                   flat_buffer& buffer,
                                   uint32_t timeout_ms = 500,
                                   uint32_t max_retries = 3);

  /**
   * @brief Send async reliable UDP call with completion handler
   * 
   * Used for [reliable] async UDP methods. Buffer is moved (copied for retransmit).
   * Handler is called when response is received or on timeout.
   * 
   * @param endpoint Target UDP endpoint
   * @param buffer Message buffer to send (moved - copied internally for retransmit)
   * @param completion_handler Called on completion with error code and response buffer
   * @param timeout_ms Timeout per attempt in milliseconds
   * @param max_retries Maximum retransmit attempts before failure
   */
  NPRPC_API void call_udp_reliable_async(
    const EndPoint& endpoint,
    flat_buffer&& buffer,
    std::optional<std::function<void(const boost::system::error_code&, flat_buffer&)>>&& completion_handler,
    uint32_t timeout_ms = 500,
    uint32_t max_retries = 3);

  /**
   * @brief Send unreliable message (fire-and-forget, no reply expected)
   * 
   * Used for [unreliable] methods across all transports:
   * - UDP: Uses UDP datagram (existing send_udp)
   * - QUIC: Uses QUIC DATAGRAM extension (RFC 9221)
   * - TCP/WebSocket: Falls back to regular async call (reliable)
   * 
   * @param endpoint Target endpoint
   * @param buffer Message buffer to send (moved)
   */
  NPRPC_API void send_unreliable(const EndPoint& endpoint, flat_buffer&& buffer);

  /**
   * @brief Check if endpoint uses shared memory transport
   */
  static bool is_shared_memory(const EndPoint& endpoint) noexcept {
    return endpoint.type() == EndPointType::SharedMemory;
  }
  
  /**
   * @brief Prepare a zero-copy buffer for shared memory call
   * 
   * If endpoint is shared memory, returns a buffer in view mode pointing
   * directly into the send ring buffer. Otherwise returns false and
   * buffer remains unchanged (caller should use normal heap buffer).
   * 
   * @param endpoint Target endpoint
   * @param buffer Buffer to set up (will be set to view mode if shared memory)
   * @param max_size Maximum message size to reserve
   * @return true if buffer was set up for zero-copy, false otherwise
   */
  NPRPC_API bool prepare_zero_copy_buffer(SessionContext& ctx,
                                          flat_buffer& buffer,
                                          size_t max_size);

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
 Poa* create_poa_impl(uint32_t max_objects,
                      PoaPolicy::Lifespan lifespan,
                      PoaPolicy::ObjectIdPolicy object_id_policy);
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
  // User-supplied IDs: simple array indexed by object_id (0..max_objects_-1), lock-free
  struct UserObjects {
    std::unique_ptr<std::atomic<ObjectServant*>[]> slots;
    explicit UserObjects(uint32_t max_objects)
      : slots{std::make_unique<std::atomic<ObjectServant*>[]>(max_objects)}
    {
      for (uint32_t i = 0; i < max_objects; ++i) {
        slots[i].store(nullptr, std::memory_order_relaxed);
      }
    }
  };
  using SystemObjects = IdToPtr<ObjectServant*>;

  const uint32_t max_objects_;
  std::variant<SystemObjects, UserObjects> id_to_ptr_;
  PoaPolicy::Lifespan pl_lifespan_;

 public:
  auto get_lifespan() const noexcept
  {
    return pl_lifespan_;
  }

  bool is_user_supplied_policy() const noexcept
  {
    return std::holds_alternative<UserObjects>(id_to_ptr_);
  }

  virtual ~PoaImpl()
  {
  }

  std::optional<ObjectGuard> get_object(oid_t oid) noexcept;

  ObjectId activate_object(
    ObjectServant*  obj,
    uint32_t        activation_flags,
    SessionContext* ctx) override;

  ObjectId activate_object_with_id(
    oid_t           object_id,
    ObjectServant*  obj,
    uint32_t        activation_flags,
    SessionContext* ctx) override;

  void deactivate_object(oid_t object_id) override;
 
  static void delete_object(ObjectServant* obj);

  PoaImpl(uint32_t objects_max,
          uint16_t idx,
          PoaPolicy::Lifespan lifespan,
          PoaPolicy::ObjectIdPolicy object_id_policy)
      : Poa(idx)
      , max_objects_{objects_max}
      , id_to_ptr_{object_id_policy == PoaPolicy::ObjectIdPolicy::UserSupplied
                     ? std::variant<SystemObjects, UserObjects>{std::in_place_type<UserObjects>, objects_max}
                     : std::variant<SystemObjects, UserObjects>{std::in_place_type<SystemObjects>, objects_max}}
      , pl_lifespan_{lifespan}
  {
  }

 private:
  ObjectId finalize_activation(ObjectServant*  obj,
                               oid_t           object_id,
                               uint32_t        activation_flags,
                               SessionContext* ctx);
};

inline void make_simple_answer(SessionContext& ctx,
                               MessageId id,
                               uint32_t request_id = 0)
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

  constexpr size_t header_size = sizeof(impl::flat::Header);

  assert(ctx.rx_buffer != nullptr);
  assert(ctx.tx_buffer != nullptr);
  auto& bin = *ctx.rx_buffer;
  auto& bout = *ctx.tx_buffer;

  if (!request_id && bin.size() >= header_size) {
    auto header = static_cast<const impl::flat::Header*>(bin.cdata().data());
    request_id = header->request_id;
  }

  // clear tx buffer
  bout.consume(bout.size());
  if (!::nprpc::impl::g_rpc->prepare_zero_copy_buffer(ctx, bout, header_size))
    bout.prepare(header_size);

  auto mb = bout.data();
  auto header = static_cast<impl::flat::Header*>(mb.data());
  header->size        = sizeof(impl::flat::Header) - 4;
  header->msg_id      = id;
  header->msg_type    = impl::MessageType::Answer;
  header->request_id  = request_id;

  bout.commit(header_size);
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
  closed_.store(true);
  impl::g_rpc->close_session(this);
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

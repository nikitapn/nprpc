// Copyright (c) 2021 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by
// LICENSING file in the topmost directory

#pragma once

#include "export.hpp"

#include <array>
#include <string_view>
#include <initializer_list>
#include <stdexcept>
#include <atomic>
#include <chrono>
#include <optional>

#include <boost/asio/io_context.hpp>
#include <boost/beast/core/flat_buffer.hpp>

#include <nprpc/common.hpp>
#include <nprpc/nprpc_base.hpp>
#include <nprpc/basic.hpp>
#include <nprpc/buffer.hpp>
#include <nprpc/object_ptr.hpp>
#include <nprpc/utils.hpp>
#include <nprpc/endpoint.hpp>
#include <nprpc/session_context.h>
#include <nprpc/serialization/serialization.h>

namespace nprpc {

class Rpc;
class Nameserver;
class Poa;
class ObjectServant;
class Object;

constexpr nprpc::oid_t invalid_object_id = (uint64_t)-1;

namespace impl {

class RpcImpl;
class PoaImpl;
class ObjectGuard;

NPRPC_API Object* create_object_from_flat(detail::flat::ObjectId_Direct oid,
                                          EndPoint remote_endpoint);
}  // namespace impl

class ObjectId
{
  friend impl::PoaImpl;
 protected:
  detail::ObjectId data_;
 public:
  template <typename Archive>
  void serialize(Archive& ar)
  {
    ar & NVP2("object_id", data_.object_id);
    ar & NVP2("poa_idx", data_.poa_idx);
    ar & NVP2("flags", data_.flags);
    ar & NVP2("origin", data_.origin);
    ar & NVP2("class_id", data_.class_id);
    ar & NVP2("urls", data_.urls);
  }

  void assign_from_direct(const nprpc::detail::flat::ObjectId_Direct& other)
  {
    nprpc_base::flat::assign_from_flat_ObjectId(const_cast<nprpc::detail::flat::ObjectId_Direct&>(other), data_);
  }
  
  static void assign_to_direct(
    const nprpc::ObjectId& oid, 
    detail::flat::ObjectId_Direct& direct)
  {
    nprpc_base::flat::assign_from_cpp_ObjectId(direct, oid.data_);
  }

  auto object_id() const noexcept { return data_.object_id; }
  auto poa_idx() const noexcept { return data_.poa_idx; }
  auto flags() const noexcept { return data_.flags; }
  const auto& origin() const noexcept { return data_.origin; }
  const auto& class_id() const noexcept { return data_.class_id; }
  const auto& urls() const noexcept { return data_.urls; }

  bool is_same_origin(const uuid_t& other) const noexcept
  {
    return origin() == other;
  }

  auto& get_data() noexcept { return data_; }
  const auto& get_data() const noexcept { return data_; }
};


namespace PoaPolicy {
enum class Lifespan {
  Transient  = 0,
  Persistent = 1
};
} // namespace PoaPolicy



namespace ObjectActivationFlags {
enum Enum : uint32_t {
  // I'm not sure if this flag is needed
  NONE                     = 0,
  // Means that evil hackers will not get access to this object
  // from any other session, besides from the one it was created in
  SESSION_SPECIFIC         = 1 << 0,
  ALLOW_TCP                = 1 << 1,
  ALLOW_WEBSOCKET          = 1 << 2,
  ALLOW_SSL_WEBSOCKET      = 1 << 3,
  // TODO: implement memory mapped files transport
  ALLOW_MEMORY_MAPPED      = 1 << 4,
  ALLOW_ALL =
    ALLOW_TCP | ALLOW_WEBSOCKET | ALLOW_SSL_WEBSOCKET /* | ALLOW_SHARED_MEMORY */,
};
}

class NPRPC_API PoaBuilder {
  uint32_t objects_max_ = 1000;
  PoaPolicy::Lifespan lifespan_policy_ = PoaPolicy::Lifespan::Transient;
  Rpc* rpc_ = nullptr;

public:
  explicit PoaBuilder(Rpc* rpc) : rpc_(rpc) {}

  // Set maximum number of objects this POA can handle
  PoaBuilder& with_max_objects(uint32_t max) {
    objects_max_ = max;
    return *this;
  }

  // Set lifespan policy
  PoaBuilder& with_lifespan(PoaPolicy::Lifespan policy) {
    lifespan_policy_ = policy;
    return *this;
  }

  // Build the POA
  Poa* build();
};

class NPRPC_API Poa
{
  poa_idx_t idx_;

 public:  
  virtual ObjectId activate_object(
    ObjectServant*  obj,
    uint32_t        activation_flags,
    SessionContext* ctx = nullptr)                    = 0;
  virtual void     deactivate_object(oid_t object_id) = 0;

  poa_idx_t get_index() const noexcept { return idx_; }
  
  Poa(
    poa_idx_t idx)
      : idx_ {idx}
  {
  }
  
  virtual ~Poa() = default;
};

class ObjectServant
{
  friend impl::PoaImpl;
  friend impl::ObjectGuard;

  Poa*  poa_;
  oid_t object_id_;

  std::atomic_uint32_t                  ref_cnt_ {0};
  std::atomic_uint32_t                  in_use_cnt_ {0};
  std::atomic_bool                      to_delete_ {false};
  std::chrono::system_clock::time_point activation_time_;
  SessionContext*                       session_ctx_ = nullptr;

 public:
  virtual std::string_view get_class() const noexcept = 0;
  virtual void             dispatch(nprpc::Buffers&        bufs,
                                    nprpc::SessionContext& ctx,
                                    bool                   from_parent) = 0;
  virtual void             destroy() noexcept { delete this; }

  Poa*               poa() const noexcept { return poa_; }
  oid_t              oid() const noexcept { return object_id_; }
  poa_idx_t          poa_index() const noexcept { return poa_->get_index(); }
  NPRPC_API uint32_t add_ref() noexcept;
  auto activation_time() const noexcept { return activation_time_; }
  bool is_unused() const noexcept { return ref_cnt_.load() == 0; }
  bool is_deleted() const noexcept { return to_delete_.load(); }
  bool validate_session(SessionContext& ctx) const noexcept
  {
    return (!session_ctx_ || session_ctx_ == &ctx);
  }
  uint32_t release() noexcept;
  virtual ~ObjectServant() = default;
};

class Object : public ObjectId
{
  friend impl::RpcImpl;
  template <typename T>
  friend T* narrow(Object*&) noexcept;
  friend NPRPC_API Object* impl::create_object_from_flat(
    detail::flat::ObjectId_Direct oid, EndPoint remote_endpoint);

  std::atomic_uint32_t local_ref_cnt_ = 0;
  uint32_t             timeout_ms_    = 1000;
  EndPoint             endpoint_;
 public:
  std::string_view get_class() const noexcept { return class_id(); };

  PoaPolicy::Lifespan policy_lifespan() const noexcept
  {
    return static_cast<PoaPolicy::Lifespan>(
      flags() & (1 << static_cast<
        std::underlying_type_t<detail::ObjectFlag>>(detail::ObjectFlag::Lifespan))
    );
  }

  NPRPC_API uint32_t add_ref();
  NPRPC_API uint32_t release();
  NPRPC_API void     select_endpoint(std::optional<EndPoint> remote_endpoint = std::nullopt);
  
  uint32_t set_timeout(uint32_t timeout_ms) noexcept
  {
    return boost::exchange(timeout_ms_, timeout_ms);
  }

  uint32_t get_timeout() const noexcept { return timeout_ms_; }

  const EndPoint& get_endpoint() const noexcept { return endpoint_; }

  NPRPC_API virtual ~Object() = default;

  Object(const Object&)            = delete;
  Object& operator=(const Object&) = delete;

  Object(Object&&)                 = delete;
  Object& operator=(Object&& other) {
    if (this != &other) {
      data_                = std::move(other.data_);
      local_ref_cnt_       = other.local_ref_cnt_.load();
      timeout_ms_          = other.timeout_ms_;
      endpoint_            = std::move(other.endpoint_);
      other.timeout_ms_    = other.timeout_ms_;
    }
    return *this;
  }

 protected:
  Object() = default;
};

class NPRPC_API Rpc
{
 public:
  // Create a new POA builder
  PoaBuilder create_poa() { return PoaBuilder(this); }
  virtual void destroy() = 0;
  virtual ObjectPtr<Nameserver> get_nameserver(std::string_view nameserver_ip) = 0;
  virtual ~Rpc() = default;
};

namespace impl {
struct Config {
  DebugLevel                 debug_level = DebugLevel::DebugLevel_Critical;
  uuid_t                     uuid;
  std::string                hostname;
  std::string                listen_address    = "0.0.0.0";
  uint16_t                   listen_tcp_port   = 0;
  uint16_t                   listen_http_port  = 0;
  std::string                http_root_dir;
  std::vector<std::string>   spa_links;
  ssl::context               ssl_context{ssl::context::tlsv12_client};
};
} // namespace detail

class RpcBuilder {
  impl::Config cfg_;
  bool         use_ssl_websocket_server_ = false;
  std::string  ssl_public_key_path_;
  std::string  ssl_secret_key_path_;
  std::string  ssl_dh_params_path_;
 public:
  NPRPC_API RpcBuilder();

  RpcBuilder& set_debug_level(DebugLevel level) noexcept
  {
    cfg_.debug_level = level;
    return *this;
  }

  RpcBuilder& set_hostname(std::string_view hostname) noexcept
  {
    cfg_.hostname = hostname;
    return *this;
  }

  RpcBuilder& set_listen_address(std::string_view listen_address) noexcept
  {
    cfg_.listen_address = listen_address;
    return *this;
  }

  RpcBuilder& set_listen_tcp_port(uint16_t port) noexcept
  {
    cfg_.listen_tcp_port = port;
    return *this;
  }

  RpcBuilder& set_listen_http_port(uint16_t port) noexcept
  {
    cfg_.listen_http_port = port;
    return *this;
  }

  RpcBuilder& set_http_root_dir(std::string_view dir) noexcept
  {
    cfg_.http_root_dir = dir;
    return *this;
  }

  RpcBuilder& enable_ssl_server(
    std::string_view public_key_path,
    std::string_view private_key_path,
    std::string_view dh_params_path = "") noexcept
  {
    use_ssl_websocket_server_ = true;
    ssl_public_key_path_      = public_key_path;
    ssl_secret_key_path_      = private_key_path;
    ssl_dh_params_path_       = dh_params_path;
    return *this;
  }

  NPRPC_API Rpc* build(boost::asio::io_context& ioc);
};



template<typename T>
concept ObjectDerrived = std::is_base_of_v<Object, T>;

template<ObjectDerrived T>
T* narrow(Object*& obj) noexcept
{
  static_assert(std::is_base_of_v<Object, T>);

  if (obj->get_class() != T::servant_t::_get_class()) return nullptr;

  auto result = new T(0);
  static_cast<Object&>(*result) = std::move(*obj);

  delete obj;
  obj = nullptr;

  return result;
}

}  // namespace nprpc

#include <ostream>
#include <iomanip>

inline std::ostream& operator<<(
  std::ostream& os, const nprpc::Object& obj)
{
  os << "object_id: " << std::hex << std::setw(16) << std::setfill('0')
     << obj.object_id() << "\npoa_idx: " << obj.poa_idx()
     << "\nflags: " << obj.flags()
     << "\nclass_id: " << obj.class_id() << "\nurls: " << obj.urls()
     << '\n';

  return os;
}

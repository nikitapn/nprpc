#ifndef NPRPC_BASE_
#define NPRPC_BASE_

#include <nprpc/flat.hpp>

namespace nprpc { 
using poa_idx_t = uint16_t;
using oid_t = uint64_t;
using ifs_idx_t = uint8_t;
using fn_idx_t = uint8_t;
namespace detail { 
struct ObjectIdLocal {
  nprpc::poa_idx_t poa_idx;
  nprpc::oid_t object_id;
};

namespace flat {
struct ObjectIdLocal {
  uint16_t poa_idx;
  uint64_t object_id;
};

class ObjectIdLocal_Direct {
  boost::beast::flat_buffer& buffer_;
  const size_t offset_;

  auto& base() noexcept { return *reinterpret_cast<ObjectIdLocal*>(reinterpret_cast<std::byte*>(buffer_.data().data()) + offset_); }
  auto const& base() const noexcept { return *reinterpret_cast<const ObjectIdLocal*>(reinterpret_cast<const std::byte*>(buffer_.data().data()) + offset_); }
public:
  void* __data() noexcept { return (void*)&base(); }
  ObjectIdLocal_Direct(boost::beast::flat_buffer& buffer, size_t offset)
    : buffer_(buffer)
    , offset_(offset)
  {
  }
  const uint16_t& poa_idx() const noexcept { return base().poa_idx;}
  uint16_t& poa_idx() noexcept { return base().poa_idx;}
  const uint64_t& object_id() const noexcept { return base().object_id;}
  uint64_t& object_id() noexcept { return base().object_id;}
};
} // namespace flat

struct ObjectId {
  uint32_t ip4;
  uint16_t port;
  nprpc::poa_idx_t poa_idx;
  nprpc::oid_t object_id;
  uint32_t flags;
  std::string class_id;
};

namespace flat {
struct ObjectId {
  uint32_t ip4;
  uint16_t port;
  uint16_t poa_idx;
  uint64_t object_id;
  uint32_t flags;
  ::flat::String class_id;
};

class ObjectId_Direct {
  boost::beast::flat_buffer& buffer_;
  const size_t offset_;

  auto& base() noexcept { return *reinterpret_cast<ObjectId*>(reinterpret_cast<std::byte*>(buffer_.data().data()) + offset_); }
  auto const& base() const noexcept { return *reinterpret_cast<const ObjectId*>(reinterpret_cast<const std::byte*>(buffer_.data().data()) + offset_); }
public:
  void* __data() noexcept { return (void*)&base(); }
  ObjectId_Direct(boost::beast::flat_buffer& buffer, size_t offset)
    : buffer_(buffer)
    , offset_(offset)
  {
  }
  const uint32_t& ip4() const noexcept { return base().ip4;}
  uint32_t& ip4() noexcept { return base().ip4;}
  const uint16_t& port() const noexcept { return base().port;}
  uint16_t& port() noexcept { return base().port;}
  const uint16_t& poa_idx() const noexcept { return base().poa_idx;}
  uint16_t& poa_idx() noexcept { return base().poa_idx;}
  const uint64_t& object_id() const noexcept { return base().object_id;}
  uint64_t& object_id() noexcept { return base().object_id;}
  const uint32_t& flags() const noexcept { return base().flags;}
  uint32_t& flags() noexcept { return base().flags;}
  void class_id(const char* str) { new (&base().class_id) ::flat::String(buffer_, str); }
  void class_id(const std::string& str) { new (&base().class_id) ::flat::String(buffer_, str); }
  auto class_id() noexcept { return (::flat::Span<char>)base().class_id; }
};
} // namespace flat

struct CallHeader {
  nprpc::poa_idx_t poa_idx;
  nprpc::ifs_idx_t interface_idx;
  nprpc::fn_idx_t function_idx;
  nprpc::oid_t object_id;
};

namespace flat {
struct CallHeader {
  uint16_t poa_idx;
  uint8_t interface_idx;
  uint8_t function_idx;
  uint64_t object_id;
};

class CallHeader_Direct {
  boost::beast::flat_buffer& buffer_;
  const size_t offset_;

  auto& base() noexcept { return *reinterpret_cast<CallHeader*>(reinterpret_cast<std::byte*>(buffer_.data().data()) + offset_); }
  auto const& base() const noexcept { return *reinterpret_cast<const CallHeader*>(reinterpret_cast<const std::byte*>(buffer_.data().data()) + offset_); }
public:
  void* __data() noexcept { return (void*)&base(); }
  CallHeader_Direct(boost::beast::flat_buffer& buffer, size_t offset)
    : buffer_(buffer)
    , offset_(offset)
  {
  }
  const uint16_t& poa_idx() const noexcept { return base().poa_idx;}
  uint16_t& poa_idx() noexcept { return base().poa_idx;}
  const uint8_t& interface_idx() const noexcept { return base().interface_idx;}
  uint8_t& interface_idx() noexcept { return base().interface_idx;}
  const uint8_t& function_idx() const noexcept { return base().function_idx;}
  uint8_t& function_idx() noexcept { return base().function_idx;}
  const uint64_t& object_id() const noexcept { return base().object_id;}
  uint64_t& object_id() noexcept { return base().object_id;}
};
} // namespace flat

} // namespace detail

} // namespace nprpc


#endif
#ifndef NPRPC_BASE_
#define NPRPC_BASE_

#include <nprpc/flat.hpp>
#include <nprpc/exception.hpp>

namespace nprpc { 
using poa_idx_t = uint16_t;
using oid_t = uint64_t;
using ifs_idx_t = uint8_t;
using fn_idx_t = uint8_t;
class ExceptionCommFailure : public ::nprpc::Exception {
public:
  ExceptionCommFailure() : ::nprpc::Exception("ExceptionCommFailure") {} 
};

namespace flat {
struct ExceptionCommFailure {
  uint32_t __ex_id;
};

class ExceptionCommFailure_Direct {
  ::nprpc::flat_buffer& buffer_;
  const std::uint32_t offset_;

  auto& base() noexcept { return *reinterpret_cast<ExceptionCommFailure*>(reinterpret_cast<std::byte*>(buffer_.data().data()) + offset_); }
  auto const& base() const noexcept { return *reinterpret_cast<const ExceptionCommFailure*>(reinterpret_cast<const std::byte*>(buffer_.data().data()) + offset_); }
public:
  uint32_t offset() const noexcept { return offset_; }
  void* __data() noexcept { return (void*)&base(); }
  ExceptionCommFailure_Direct(::nprpc::flat_buffer& buffer, std::uint32_t offset)
    : buffer_(buffer)
    , offset_(offset)
  {
  }
  const uint32_t& __ex_id() const noexcept { return base().__ex_id;}
  uint32_t& __ex_id() noexcept { return base().__ex_id;}
};
} // namespace flat

class ExceptionTimeout : public ::nprpc::Exception {
public:
  ExceptionTimeout() : ::nprpc::Exception("ExceptionTimeout") {} 
};

namespace flat {
struct ExceptionTimeout {
  uint32_t __ex_id;
};

class ExceptionTimeout_Direct {
  ::nprpc::flat_buffer& buffer_;
  const std::uint32_t offset_;

  auto& base() noexcept { return *reinterpret_cast<ExceptionTimeout*>(reinterpret_cast<std::byte*>(buffer_.data().data()) + offset_); }
  auto const& base() const noexcept { return *reinterpret_cast<const ExceptionTimeout*>(reinterpret_cast<const std::byte*>(buffer_.data().data()) + offset_); }
public:
  uint32_t offset() const noexcept { return offset_; }
  void* __data() noexcept { return (void*)&base(); }
  ExceptionTimeout_Direct(::nprpc::flat_buffer& buffer, std::uint32_t offset)
    : buffer_(buffer)
    , offset_(offset)
  {
  }
  const uint32_t& __ex_id() const noexcept { return base().__ex_id;}
  uint32_t& __ex_id() noexcept { return base().__ex_id;}
};
} // namespace flat

class ExceptionObjectNotExist : public ::nprpc::Exception {
public:
  ExceptionObjectNotExist() : ::nprpc::Exception("ExceptionObjectNotExist") {} 
};

namespace flat {
struct ExceptionObjectNotExist {
  uint32_t __ex_id;
};

class ExceptionObjectNotExist_Direct {
  ::nprpc::flat_buffer& buffer_;
  const std::uint32_t offset_;

  auto& base() noexcept { return *reinterpret_cast<ExceptionObjectNotExist*>(reinterpret_cast<std::byte*>(buffer_.data().data()) + offset_); }
  auto const& base() const noexcept { return *reinterpret_cast<const ExceptionObjectNotExist*>(reinterpret_cast<const std::byte*>(buffer_.data().data()) + offset_); }
public:
  uint32_t offset() const noexcept { return offset_; }
  void* __data() noexcept { return (void*)&base(); }
  ExceptionObjectNotExist_Direct(::nprpc::flat_buffer& buffer, std::uint32_t offset)
    : buffer_(buffer)
    , offset_(offset)
  {
  }
  const uint32_t& __ex_id() const noexcept { return base().__ex_id;}
  uint32_t& __ex_id() noexcept { return base().__ex_id;}
};
} // namespace flat

class ExceptionUnknownFunctionIndex : public ::nprpc::Exception {
public:
  ExceptionUnknownFunctionIndex() : ::nprpc::Exception("ExceptionUnknownFunctionIndex") {} 
};

namespace flat {
struct ExceptionUnknownFunctionIndex {
  uint32_t __ex_id;
};

class ExceptionUnknownFunctionIndex_Direct {
  ::nprpc::flat_buffer& buffer_;
  const std::uint32_t offset_;

  auto& base() noexcept { return *reinterpret_cast<ExceptionUnknownFunctionIndex*>(reinterpret_cast<std::byte*>(buffer_.data().data()) + offset_); }
  auto const& base() const noexcept { return *reinterpret_cast<const ExceptionUnknownFunctionIndex*>(reinterpret_cast<const std::byte*>(buffer_.data().data()) + offset_); }
public:
  uint32_t offset() const noexcept { return offset_; }
  void* __data() noexcept { return (void*)&base(); }
  ExceptionUnknownFunctionIndex_Direct(::nprpc::flat_buffer& buffer, std::uint32_t offset)
    : buffer_(buffer)
    , offset_(offset)
  {
  }
  const uint32_t& __ex_id() const noexcept { return base().__ex_id;}
  uint32_t& __ex_id() noexcept { return base().__ex_id;}
};
} // namespace flat

class ExceptionUnknownMessageId : public ::nprpc::Exception {
public:
  ExceptionUnknownMessageId() : ::nprpc::Exception("ExceptionUnknownMessageId") {} 
};

namespace flat {
struct ExceptionUnknownMessageId {
  uint32_t __ex_id;
};

class ExceptionUnknownMessageId_Direct {
  ::nprpc::flat_buffer& buffer_;
  const std::uint32_t offset_;

  auto& base() noexcept { return *reinterpret_cast<ExceptionUnknownMessageId*>(reinterpret_cast<std::byte*>(buffer_.data().data()) + offset_); }
  auto const& base() const noexcept { return *reinterpret_cast<const ExceptionUnknownMessageId*>(reinterpret_cast<const std::byte*>(buffer_.data().data()) + offset_); }
public:
  uint32_t offset() const noexcept { return offset_; }
  void* __data() noexcept { return (void*)&base(); }
  ExceptionUnknownMessageId_Direct(::nprpc::flat_buffer& buffer, std::uint32_t offset)
    : buffer_(buffer)
    , offset_(offset)
  {
  }
  const uint32_t& __ex_id() const noexcept { return base().__ex_id;}
  uint32_t& __ex_id() noexcept { return base().__ex_id;}
};
} // namespace flat

class ExceptionUnsecuredObject : public ::nprpc::Exception {
public:
  std::string class_id;

  ExceptionUnsecuredObject() : ::nprpc::Exception("ExceptionUnsecuredObject") {} 
  ExceptionUnsecuredObject(std::string _class_id)
    : ::nprpc::Exception("ExceptionUnsecuredObject")
    , class_id(_class_id)
  {
  }
};

namespace flat {
struct ExceptionUnsecuredObject {
  uint32_t __ex_id;
  ::nprpc::flat::String class_id;
};

class ExceptionUnsecuredObject_Direct {
  ::nprpc::flat_buffer& buffer_;
  const std::uint32_t offset_;

  auto& base() noexcept { return *reinterpret_cast<ExceptionUnsecuredObject*>(reinterpret_cast<std::byte*>(buffer_.data().data()) + offset_); }
  auto const& base() const noexcept { return *reinterpret_cast<const ExceptionUnsecuredObject*>(reinterpret_cast<const std::byte*>(buffer_.data().data()) + offset_); }
public:
  uint32_t offset() const noexcept { return offset_; }
  void* __data() noexcept { return (void*)&base(); }
  ExceptionUnsecuredObject_Direct(::nprpc::flat_buffer& buffer, std::uint32_t offset)
    : buffer_(buffer)
    , offset_(offset)
  {
  }
  const uint32_t& __ex_id() const noexcept { return base().__ex_id;}
  uint32_t& __ex_id() noexcept { return base().__ex_id;}
  void class_id(const char* str) { new (&base().class_id) ::nprpc::flat::String(buffer_, str); }
  void class_id(const std::string& str) { new (&base().class_id) ::nprpc::flat::String(buffer_, str); }
  auto class_id() noexcept { return (::nprpc::flat::Span<char>)base().class_id; }
  auto class_id() const noexcept { return (::nprpc::flat::Span<const char>)base().class_id; }
  auto class_id_d() noexcept {     return ::nprpc::flat::String_Direct1(buffer_, offset_ + offsetof(ExceptionUnsecuredObject, class_id));  }
};
} // namespace flat

class ExceptionBadAccess : public ::nprpc::Exception {
public:
  ExceptionBadAccess() : ::nprpc::Exception("ExceptionBadAccess") {} 
};

namespace flat {
struct ExceptionBadAccess {
  uint32_t __ex_id;
};

class ExceptionBadAccess_Direct {
  ::nprpc::flat_buffer& buffer_;
  const std::uint32_t offset_;

  auto& base() noexcept { return *reinterpret_cast<ExceptionBadAccess*>(reinterpret_cast<std::byte*>(buffer_.data().data()) + offset_); }
  auto const& base() const noexcept { return *reinterpret_cast<const ExceptionBadAccess*>(reinterpret_cast<const std::byte*>(buffer_.data().data()) + offset_); }
public:
  uint32_t offset() const noexcept { return offset_; }
  void* __data() noexcept { return (void*)&base(); }
  ExceptionBadAccess_Direct(::nprpc::flat_buffer& buffer, std::uint32_t offset)
    : buffer_(buffer)
    , offset_(offset)
  {
  }
  const uint32_t& __ex_id() const noexcept { return base().__ex_id;}
  uint32_t& __ex_id() noexcept { return base().__ex_id;}
};
} // namespace flat

class ExceptionBadInput : public ::nprpc::Exception {
public:
  ExceptionBadInput() : ::nprpc::Exception("ExceptionBadInput") {} 
};

namespace flat {
struct ExceptionBadInput {
  uint32_t __ex_id;
};

class ExceptionBadInput_Direct {
  ::nprpc::flat_buffer& buffer_;
  const std::uint32_t offset_;

  auto& base() noexcept { return *reinterpret_cast<ExceptionBadInput*>(reinterpret_cast<std::byte*>(buffer_.data().data()) + offset_); }
  auto const& base() const noexcept { return *reinterpret_cast<const ExceptionBadInput*>(reinterpret_cast<const std::byte*>(buffer_.data().data()) + offset_); }
public:
  uint32_t offset() const noexcept { return offset_; }
  void* __data() noexcept { return (void*)&base(); }
  ExceptionBadInput_Direct(::nprpc::flat_buffer& buffer, std::uint32_t offset)
    : buffer_(buffer)
    , offset_(offset)
  {
  }
  const uint32_t& __ex_id() const noexcept { return base().__ex_id;}
  uint32_t& __ex_id() noexcept { return base().__ex_id;}
};
} // namespace flat

enum class DebugLevel : uint32_t {
  DebugLevel_Critical,
  DebugLevel_InactiveTimeout,
  DebugLevel_EveryCall,
  DebugLevel_EveryMessageContent,
  DebugLevel_TraceAll
};
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
  ::nprpc::flat_buffer& buffer_;
  const std::uint32_t offset_;

  auto& base() noexcept { return *reinterpret_cast<ObjectIdLocal*>(reinterpret_cast<std::byte*>(buffer_.data().data()) + offset_); }
  auto const& base() const noexcept { return *reinterpret_cast<const ObjectIdLocal*>(reinterpret_cast<const std::byte*>(buffer_.data().data()) + offset_); }
public:
  uint32_t offset() const noexcept { return offset_; }
  void* __data() noexcept { return (void*)&base(); }
  ObjectIdLocal_Direct(::nprpc::flat_buffer& buffer, std::uint32_t offset)
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

enum class ObjectFlag : uint32_t {
  Policy_Lifespan = 0,
  WebObject = 1,
  Secured = 2
};
struct ObjectId {
  nprpc::oid_t object_id;
  uint32_t ip4;
  uint16_t port;
  uint16_t websocket_port;
  nprpc::poa_idx_t poa_idx;
  uint32_t flags;
  std::string class_id;
  std::string hostname;
};

namespace flat {
struct ObjectId {
  uint64_t object_id;
  uint32_t ip4;
  uint16_t port;
  uint16_t websocket_port;
  uint16_t poa_idx;
  uint32_t flags;
  ::nprpc::flat::String class_id;
  ::nprpc::flat::String hostname;
};

class ObjectId_Direct {
  ::nprpc::flat_buffer& buffer_;
  const std::uint32_t offset_;

  auto& base() noexcept { return *reinterpret_cast<ObjectId*>(reinterpret_cast<std::byte*>(buffer_.data().data()) + offset_); }
  auto const& base() const noexcept { return *reinterpret_cast<const ObjectId*>(reinterpret_cast<const std::byte*>(buffer_.data().data()) + offset_); }
public:
  uint32_t offset() const noexcept { return offset_; }
  void* __data() noexcept { return (void*)&base(); }
  ObjectId_Direct(::nprpc::flat_buffer& buffer, std::uint32_t offset)
    : buffer_(buffer)
    , offset_(offset)
  {
  }
  const uint64_t& object_id() const noexcept { return base().object_id;}
  uint64_t& object_id() noexcept { return base().object_id;}
  const uint32_t& ip4() const noexcept { return base().ip4;}
  uint32_t& ip4() noexcept { return base().ip4;}
  const uint16_t& port() const noexcept { return base().port;}
  uint16_t& port() noexcept { return base().port;}
  const uint16_t& websocket_port() const noexcept { return base().websocket_port;}
  uint16_t& websocket_port() noexcept { return base().websocket_port;}
  const uint16_t& poa_idx() const noexcept { return base().poa_idx;}
  uint16_t& poa_idx() noexcept { return base().poa_idx;}
  const uint32_t& flags() const noexcept { return base().flags;}
  uint32_t& flags() noexcept { return base().flags;}
  void class_id(const char* str) { new (&base().class_id) ::nprpc::flat::String(buffer_, str); }
  void class_id(const std::string& str) { new (&base().class_id) ::nprpc::flat::String(buffer_, str); }
  auto class_id() noexcept { return (::nprpc::flat::Span<char>)base().class_id; }
  auto class_id() const noexcept { return (::nprpc::flat::Span<const char>)base().class_id; }
  auto class_id_d() noexcept {     return ::nprpc::flat::String_Direct1(buffer_, offset_ + offsetof(ObjectId, class_id));  }
  void hostname(const char* str) { new (&base().hostname) ::nprpc::flat::String(buffer_, str); }
  void hostname(const std::string& str) { new (&base().hostname) ::nprpc::flat::String(buffer_, str); }
  auto hostname() noexcept { return (::nprpc::flat::Span<char>)base().hostname; }
  auto hostname() const noexcept { return (::nprpc::flat::Span<const char>)base().hostname; }
  auto hostname_d() noexcept {     return ::nprpc::flat::String_Direct1(buffer_, offset_ + offsetof(ObjectId, hostname));  }
};
} // namespace flat

} // namespace detail

namespace impl { 
enum class MessageId : uint32_t {
  FunctionCall = 0,
  BlockResponse,
  AddReference,
  ReleaseObject,
  Success,
  Exception,
  Error_PoaNotExist,
  Error_ObjectNotExist,
  Error_CommFailure,
  Error_UnknownFunctionIdx,
  Error_UnknownMessageId,
  Error_BadAccess,
  Error_BadInput
};
enum class MessageType : uint32_t {
  Request = 0,
  Answer
};
struct Header {
  uint32_t size;
  MessageId msg_id;
  MessageType msg_type;
  uint32_t reserved;
};

namespace flat {
struct Header {
  uint32_t size;
  MessageId msg_id;
  MessageType msg_type;
  uint32_t reserved;
};

class Header_Direct {
  ::nprpc::flat_buffer& buffer_;
  const std::uint32_t offset_;

  auto& base() noexcept { return *reinterpret_cast<Header*>(reinterpret_cast<std::byte*>(buffer_.data().data()) + offset_); }
  auto const& base() const noexcept { return *reinterpret_cast<const Header*>(reinterpret_cast<const std::byte*>(buffer_.data().data()) + offset_); }
public:
  uint32_t offset() const noexcept { return offset_; }
  void* __data() noexcept { return (void*)&base(); }
  Header_Direct(::nprpc::flat_buffer& buffer, std::uint32_t offset)
    : buffer_(buffer)
    , offset_(offset)
  {
  }
  const uint32_t& size() const noexcept { return base().size;}
  uint32_t& size() noexcept { return base().size;}
  const MessageId& msg_id() const noexcept { return base().msg_id;}
  MessageId& msg_id() noexcept { return base().msg_id;}
  const MessageType& msg_type() const noexcept { return base().msg_type;}
  MessageType& msg_type() noexcept { return base().msg_type;}
  const uint32_t& reserved() const noexcept { return base().reserved;}
  uint32_t& reserved() noexcept { return base().reserved;}
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
  ::nprpc::flat_buffer& buffer_;
  const std::uint32_t offset_;

  auto& base() noexcept { return *reinterpret_cast<CallHeader*>(reinterpret_cast<std::byte*>(buffer_.data().data()) + offset_); }
  auto const& base() const noexcept { return *reinterpret_cast<const CallHeader*>(reinterpret_cast<const std::byte*>(buffer_.data().data()) + offset_); }
public:
  uint32_t offset() const noexcept { return offset_; }
  void* __data() noexcept { return (void*)&base(); }
  CallHeader_Direct(::nprpc::flat_buffer& buffer, std::uint32_t offset)
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

} // namespace impl

} // namespace nprpc

namespace nprpc_base::helper {
} // namespace nprpc_base::helper

#endif
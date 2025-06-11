#ifndef __NPRPC_NPRPC_BASE_HPP__
#define __NPRPC_NPRPC_BASE_HPP__

#include <nprpc/flat.hpp>
#include <nprpc/exception.hpp>
#include <string_view>

namespace nprpc { 
using oid_t = uint64_t;
using poa_idx_t = uint16_t;
using oflags_t = uint16_t;
using uuid_t = std::array<uint8_t,16>;
using ifs_idx_t = uint8_t;
using fn_idx_t = uint8_t;
class ExceptionCommFailure : public ::nprpc::Exception {
public:
  std::string what;

  ExceptionCommFailure() : ::nprpc::Exception("ExceptionCommFailure") {} 
  ExceptionCommFailure(std::string _what)
    : ::nprpc::Exception("ExceptionCommFailure")
    , what(_what)
  {
  }
};

namespace flat {
struct ExceptionCommFailure {
  uint32_t __ex_id;
  ::nprpc::flat::String what;
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
  void what(const char* str) { new (&base().what) ::nprpc::flat::String(buffer_, str); }
  void what(const std::string& str) { new (&base().what) ::nprpc::flat::String(buffer_, str); }
  auto what() noexcept { return (::nprpc::flat::Span<char>)base().what; }
  auto what() const noexcept { return (::nprpc::flat::Span<const char>)base().what; }
  auto what_d() noexcept {     return ::nprpc::flat::String_Direct1(buffer_, offset_ + offsetof(ExceptionCommFailure, what));  }
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
enum class EndPointType : uint32_t {
  Tcp,
  TcpTethered,
  WebSocket,
  SecuredWebSocket,
  SharedMemory
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
  Persistent = 1,
  Tethered = 2
};
struct ObjectId {
  nprpc::oid_t object_id;
  nprpc::poa_idx_t poa_idx;
  nprpc::oflags_t flags;
  nprpc::uuid_t origin;
  std::string class_id;
  std::string urls;
};

namespace flat {
struct ObjectId {
  uint64_t object_id;
  uint16_t poa_idx;
  uint16_t flags;
  ::nprpc::flat::Array<uint8_t,16> origin;
  ::nprpc::flat::String class_id;
  ::nprpc::flat::String urls;
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
  const uint16_t& poa_idx() const noexcept { return base().poa_idx;}
  uint16_t& poa_idx() noexcept { return base().poa_idx;}
  const uint16_t& flags() const noexcept { return base().flags;}
  uint16_t& flags() noexcept { return base().flags;}
  auto origin() noexcept { return (::nprpc::flat::Span<uint8_t>)base().origin; }
  const auto origin() const noexcept { return (::nprpc::flat::Span<const uint8_t>)base().origin; }
  void class_id(const char* str) { new (&base().class_id) ::nprpc::flat::String(buffer_, str); }
  void class_id(const std::string& str) { new (&base().class_id) ::nprpc::flat::String(buffer_, str); }
  auto class_id() noexcept { return (::nprpc::flat::Span<char>)base().class_id; }
  auto class_id() const noexcept { return (::nprpc::flat::Span<const char>)base().class_id; }
  auto class_id_d() noexcept {     return ::nprpc::flat::String_Direct1(buffer_, offset_ + offsetof(ObjectId, class_id));  }
  void urls(const char* str) { new (&base().urls) ::nprpc::flat::String(buffer_, str); }
  void urls(const std::string& str) { new (&base().urls) ::nprpc::flat::String(buffer_, str); }
  auto urls() noexcept { return (::nprpc::flat::Span<char>)base().urls; }
  auto urls() const noexcept { return (::nprpc::flat::Span<const char>)base().urls; }
  auto urls_d() noexcept {     return ::nprpc::flat::String_Direct1(buffer_, offset_ + offsetof(ObjectId, urls));  }
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
  uint32_t request_id;
};

namespace flat {
struct Header {
  uint32_t size;
  MessageId msg_id;
  MessageType msg_type;
  uint32_t request_id;
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
  const uint32_t& request_id() const noexcept { return base().request_id;}
  uint32_t& request_id() noexcept { return base().request_id;}
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
namespace nprpc::detail::helpers {
inline void assign_from_flat_ObjectId(nprpc::detail::flat::ObjectId_Direct& src, nprpc::detail::ObjectId& dest) {
dest.object_id = src.object_id();
dest.poa_idx = src.poa_idx();
dest.flags = src.flags();
{
  auto span = src.origin();
  memcpy(dest.origin.data(), span.data(), 1 * span.size());
}
dest.class_id = (std::string_view)src.class_id();
dest.urls = (std::string_view)src.urls();
}
inline void assign_from_cpp_ObjectId(nprpc::detail::flat::ObjectId_Direct& dest, const nprpc::detail::ObjectId& src) {
dest.object_id() = src.object_id;
dest.poa_idx() = src.poa_idx;
dest.flags() = src.flags;
memcpy(dest.origin().data(), src.origin.data(), src.origin.size() * 1);
dest.class_id(src.class_id);
dest.urls(src.urls);
}
} // namespace detail::flat

#endif
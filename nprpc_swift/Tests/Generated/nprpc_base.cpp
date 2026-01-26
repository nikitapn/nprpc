#include "nprpc_base.hpp"
#include <nprpc/impl/nprpc_impl.hpp>

void nprpc_base_throw_exception(::nprpc::flat_buffer& buf);

namespace nprpc {

namespace {

} // 

namespace detail { 
#ifdef NPRPC_SWIFT_BRIDGE
// C++ marshal/unmarshal for Swift bridge
static void marshal_ObjectIdLocal(void* buffer, int offset, const ObjectIdLocal& data) {
  auto* ptr = static_cast<std::byte*>(buffer) + offset;
  // TODO: marshal poa_idx (type 9)
  // TODO: marshal object_id (type 9)
}

static ObjectIdLocal unmarshal_ObjectIdLocal(const void* buffer, int offset) {
  const auto* ptr = static_cast<const std::byte*>(buffer) + offset;
  return ObjectIdLocal{
    /*.poa_idx = */ {}/*TODO*/,
    /*.object_id = */ {}/*TODO*/
  };
}
#endif // NPRPC_SWIFT_BRIDGE

#ifdef NPRPC_SWIFT_BRIDGE
// C++ marshal/unmarshal for Swift bridge
static void marshal_ObjectId(void* buffer, int offset, const ObjectId& data) {
  auto* ptr = static_cast<std::byte*>(buffer) + offset;
  // TODO: marshal object_id (type 9)
  // TODO: marshal poa_idx (type 9)
  // TODO: marshal flags (type 9)
  // TODO: marshal origin (type 9)
  // TODO: marshal class_id (type 3)
  // TODO: marshal urls (type 3)
}

static ObjectId unmarshal_ObjectId(const void* buffer, int offset) {
  const auto* ptr = static_cast<const std::byte*>(buffer) + offset;
  return ObjectId{
    /*.object_id = */ {}/*TODO*/,
    /*.poa_idx = */ {}/*TODO*/,
    /*.flags = */ {}/*TODO*/,
    /*.origin = */ {}/*TODO*/,
    /*.class_id = */ {}/*TODO*/,
    /*.urls = */ {}/*TODO*/
  };
}
#endif // NPRPC_SWIFT_BRIDGE

} // namespace detail

namespace impl { 
#ifdef NPRPC_SWIFT_BRIDGE
// C++ marshal/unmarshal for Swift bridge
static void marshal_Header(void* buffer, int offset, const Header& data) {
  auto* ptr = static_cast<std::byte*>(buffer) + offset;
  *reinterpret_cast<uint32_t*>(ptr + 0) = data.size;
  *reinterpret_cast<int32_t*>(ptr + 4) = static_cast<int32_t>(data.msg_id);
  *reinterpret_cast<int32_t*>(ptr + 8) = static_cast<int32_t>(data.msg_type);
  *reinterpret_cast<uint32_t*>(ptr + 12) = data.request_id;
}

static Header unmarshal_Header(const void* buffer, int offset) {
  const auto* ptr = static_cast<const std::byte*>(buffer) + offset;
  return Header{
    /*.size = */ *reinterpret_cast<const uint32_t*>(ptr + 0),
    /*.msg_id = */ static_cast<MessageId>(*reinterpret_cast<const int32_t*>(ptr + 4)),
    /*.msg_type = */ static_cast<MessageType>(*reinterpret_cast<const int32_t*>(ptr + 8)),
    /*.request_id = */ *reinterpret_cast<const uint32_t*>(ptr + 12)
  };
}
#endif // NPRPC_SWIFT_BRIDGE

#ifdef NPRPC_SWIFT_BRIDGE
// C++ marshal/unmarshal for Swift bridge
static void marshal_CallHeader(void* buffer, int offset, const CallHeader& data) {
  auto* ptr = static_cast<std::byte*>(buffer) + offset;
  // TODO: marshal poa_idx (type 9)
  // TODO: marshal interface_idx (type 9)
  // TODO: marshal function_idx (type 9)
  // TODO: marshal object_id (type 9)
}

static CallHeader unmarshal_CallHeader(const void* buffer, int offset) {
  const auto* ptr = static_cast<const std::byte*>(buffer) + offset;
  return CallHeader{
    /*.poa_idx = */ {}/*TODO*/,
    /*.interface_idx = */ {}/*TODO*/,
    /*.function_idx = */ {}/*TODO*/,
    /*.object_id = */ {}/*TODO*/
  };
}
#endif // NPRPC_SWIFT_BRIDGE

#ifdef NPRPC_SWIFT_BRIDGE
// C++ marshal/unmarshal for Swift bridge
static void marshal_StreamInit(void* buffer, int offset, const StreamInit& data) {
  auto* ptr = static_cast<std::byte*>(buffer) + offset;
  *reinterpret_cast<uint64_t*>(ptr + 0) = data.stream_id;
  // TODO: marshal poa_idx (type 9)
  // TODO: marshal interface_idx (type 9)
  // TODO: marshal object_id (type 9)
  // TODO: marshal func_idx (type 9)
}

static StreamInit unmarshal_StreamInit(const void* buffer, int offset) {
  const auto* ptr = static_cast<const std::byte*>(buffer) + offset;
  return StreamInit{
    /*.stream_id = */ *reinterpret_cast<const uint64_t*>(ptr + 0),
    /*.poa_idx = */ {}/*TODO*/,
    /*.interface_idx = */ {}/*TODO*/,
    /*.object_id = */ {}/*TODO*/,
    /*.func_idx = */ {}/*TODO*/
  };
}
#endif // NPRPC_SWIFT_BRIDGE

#ifdef NPRPC_SWIFT_BRIDGE
// C++ marshal/unmarshal for Swift bridge
static void marshal_StreamChunk(void* buffer, int offset, const StreamChunk& data) {
  auto* ptr = static_cast<std::byte*>(buffer) + offset;
  *reinterpret_cast<uint64_t*>(ptr + 0) = data.stream_id;
  *reinterpret_cast<uint64_t*>(ptr + 8) = data.sequence;
  // TODO: marshal data (type 2)
  *reinterpret_cast<uint32_t*>(ptr + 16) = data.window_size;
}

static StreamChunk unmarshal_StreamChunk(const void* buffer, int offset) {
  const auto* ptr = static_cast<const std::byte*>(buffer) + offset;
  return StreamChunk{
    /*.stream_id = */ *reinterpret_cast<const uint64_t*>(ptr + 0),
    /*.sequence = */ *reinterpret_cast<const uint64_t*>(ptr + 8),
    /*.data = */ {}/*TODO*/,
    /*.window_size = */ *reinterpret_cast<const uint32_t*>(ptr + 16)
  };
}
#endif // NPRPC_SWIFT_BRIDGE

#ifdef NPRPC_SWIFT_BRIDGE
// C++ marshal/unmarshal for Swift bridge
static void marshal_StreamComplete(void* buffer, int offset, const StreamComplete& data) {
  auto* ptr = static_cast<std::byte*>(buffer) + offset;
  *reinterpret_cast<uint64_t*>(ptr + 0) = data.stream_id;
  *reinterpret_cast<uint64_t*>(ptr + 8) = data.final_sequence;
}

static StreamComplete unmarshal_StreamComplete(const void* buffer, int offset) {
  const auto* ptr = static_cast<const std::byte*>(buffer) + offset;
  return StreamComplete{
    /*.stream_id = */ *reinterpret_cast<const uint64_t*>(ptr + 0),
    /*.final_sequence = */ *reinterpret_cast<const uint64_t*>(ptr + 8)
  };
}
#endif // NPRPC_SWIFT_BRIDGE

#ifdef NPRPC_SWIFT_BRIDGE
// C++ marshal/unmarshal for Swift bridge
static void marshal_StreamError(void* buffer, int offset, const StreamError& data) {
  auto* ptr = static_cast<std::byte*>(buffer) + offset;
  *reinterpret_cast<uint64_t*>(ptr + 0) = data.stream_id;
  *reinterpret_cast<uint32_t*>(ptr + 8) = data.error_code;
  // TODO: marshal error_data (type 2)
}

static StreamError unmarshal_StreamError(const void* buffer, int offset) {
  const auto* ptr = static_cast<const std::byte*>(buffer) + offset;
  return StreamError{
    /*.stream_id = */ *reinterpret_cast<const uint64_t*>(ptr + 0),
    /*.error_code = */ *reinterpret_cast<const uint32_t*>(ptr + 8),
    /*.error_data = */ {}/*TODO*/
  };
}
#endif // NPRPC_SWIFT_BRIDGE

#ifdef NPRPC_SWIFT_BRIDGE
// C++ marshal/unmarshal for Swift bridge
static void marshal_StreamCancel(void* buffer, int offset, const StreamCancel& data) {
  auto* ptr = static_cast<std::byte*>(buffer) + offset;
  *reinterpret_cast<uint64_t*>(ptr + 0) = data.stream_id;
}

static StreamCancel unmarshal_StreamCancel(const void* buffer, int offset) {
  const auto* ptr = static_cast<const std::byte*>(buffer) + offset;
  return StreamCancel{
    /*.stream_id = */ *reinterpret_cast<const uint64_t*>(ptr + 0)
  };
}
#endif // NPRPC_SWIFT_BRIDGE

} // namespace impl

} // module nprpc

void nprpc_base_throw_exception(::nprpc::flat_buffer& buf) { 
  switch(*(uint32_t*)( (char*)buf.data().data() + sizeof(::nprpc::impl::Header)) ) {
  case 0:
  {
    ::nprpc::flat::ExceptionCommFailure_Direct ex_flat(buf, sizeof(::nprpc::impl::Header));
    ::nprpc::ExceptionCommFailure ex;
ex.what = (std::string_view)ex_flat.what();
    throw ex;
  }
  case 1:
  {
    ::nprpc::flat::ExceptionTimeout_Direct ex_flat(buf, sizeof(::nprpc::impl::Header));
    ::nprpc::ExceptionTimeout ex;
    throw ex;
  }
  case 2:
  {
    ::nprpc::flat::ExceptionObjectNotExist_Direct ex_flat(buf, sizeof(::nprpc::impl::Header));
    ::nprpc::ExceptionObjectNotExist ex;
    throw ex;
  }
  case 3:
  {
    ::nprpc::flat::ExceptionUnknownFunctionIndex_Direct ex_flat(buf, sizeof(::nprpc::impl::Header));
    ::nprpc::ExceptionUnknownFunctionIndex ex;
    throw ex;
  }
  case 4:
  {
    ::nprpc::flat::ExceptionUnknownMessageId_Direct ex_flat(buf, sizeof(::nprpc::impl::Header));
    ::nprpc::ExceptionUnknownMessageId ex;
    throw ex;
  }
  case 5:
  {
    ::nprpc::flat::ExceptionUnsecuredObject_Direct ex_flat(buf, sizeof(::nprpc::impl::Header));
    ::nprpc::ExceptionUnsecuredObject ex;
ex.class_id = (std::string_view)ex_flat.class_id();
    throw ex;
  }
  case 6:
  {
    ::nprpc::flat::ExceptionBadAccess_Direct ex_flat(buf, sizeof(::nprpc::impl::Header));
    ::nprpc::ExceptionBadAccess ex;
    throw ex;
  }
  case 7:
  {
    ::nprpc::flat::ExceptionBadInput_Direct ex_flat(buf, sizeof(::nprpc::impl::Header));
    ::nprpc::ExceptionBadInput ex;
    throw ex;
  }
  default:
    throw std::runtime_error("unknown rpc exception");
  }
}

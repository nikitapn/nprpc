#include "nprpc_base.hpp"
#include <nprpc/impl/nprpc_impl.hpp>

void nprpc_base_throw_exception(::nprpc::flat_buffer& buf);

namespace nprpc {

namespace {

} // 

namespace detail { 
} // namespace detail

namespace impl { 
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

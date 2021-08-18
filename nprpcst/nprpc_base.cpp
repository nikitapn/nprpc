#include "nprpc_base.hpp"
#include "nprpc_base_m.hpp"
#include <nprpc/nprpc_impl.hpp>

void nprpc_base_throw_exception(boost::beast::flat_buffer& buf);

namespace nprpc { 
namespace detail { 
} // namespace detail

namespace impl { 
} // namespace impl

} // namespace nprpc


void nprpc_base_throw_exception(boost::beast::flat_buffer& buf) { 
  switch(*(uint32_t*)( (char*)buf.data().data() + sizeof(::nprpc::impl::Header)) ) {
  case 0:
  {
    nprpc::flat::ExceptionCommFailure_Direct ex_flat(buf, sizeof(::nprpc::impl::Header));
    nprpc::ExceptionCommFailure ex;
    throw ex;
  }
  case 1:
  {
    nprpc::flat::ExceptionTimeout_Direct ex_flat(buf, sizeof(::nprpc::impl::Header));
    nprpc::ExceptionTimeout ex;
    throw ex;
  }
  case 2:
  {
    nprpc::flat::ExceptionObjectNotExist_Direct ex_flat(buf, sizeof(::nprpc::impl::Header));
    nprpc::ExceptionObjectNotExist ex;
    throw ex;
  }
  case 3:
  {
    nprpc::flat::ExceptionUnknownFunctionIndex_Direct ex_flat(buf, sizeof(::nprpc::impl::Header));
    nprpc::ExceptionUnknownFunctionIndex ex;
    throw ex;
  }
  case 4:
  {
    nprpc::flat::ExceptionUnknownMessageId_Direct ex_flat(buf, sizeof(::nprpc::impl::Header));
    nprpc::ExceptionUnknownMessageId ex;
    throw ex;
  }
  default:
    throw std::runtime_error("unknown rpc exception");
  }
}

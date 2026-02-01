#include "nprpc_nameserver.hpp"
#include <nprpc/impl/nprpc_impl.hpp>

void nprpc_nameserver_throw_exception(::nprpc::flat_buffer& buf);

namespace nprpc::common {

namespace {
struct nprpc_nameserver_M1 {
  ::nprpc::detail::flat::ObjectId _1;
  ::nprpc::flat::String _2;
};

class nprpc_nameserver_M1_Direct {
  ::nprpc::flat_buffer& buffer_;
  const std::uint32_t offset_;

  auto& base() noexcept { return *reinterpret_cast<nprpc_nameserver_M1*>(reinterpret_cast<std::byte*>(buffer_.data().data()) + offset_); }
  auto const& base() const noexcept { return *reinterpret_cast<const nprpc_nameserver_M1*>(reinterpret_cast<const std::byte*>(buffer_.data().data()) + offset_); }
public:
  uint32_t offset() const noexcept { return offset_; }
  void* __data() noexcept { return (void*)&base(); }
  nprpc_nameserver_M1_Direct(::nprpc::flat_buffer& buffer, std::uint32_t offset)
    : buffer_(buffer)
    , offset_(offset)
  {
  }
  auto _1() noexcept { return ::nprpc::detail::flat::ObjectId_Direct(buffer_, offset_ + offsetof(nprpc_nameserver_M1, _1)); }
  void _2(const char* str) { new (&base()._2) ::nprpc::flat::String(buffer_, str); }
  void _2(const std::string& str) { new (&base()._2) ::nprpc::flat::String(buffer_, str); }
  auto _2() noexcept { return (::nprpc::flat::Span<char>)base()._2; }
  auto _2() const noexcept { return (::nprpc::flat::Span<const char>)base()._2; }
  auto _2_d() noexcept { return ::nprpc::flat::String_Direct1(buffer_, offset_ + offsetof(nprpc_nameserver_M1, _2)); }
};

struct nprpc_nameserver_M2 {
  ::nprpc::flat::String _1;
};

class nprpc_nameserver_M2_Direct {
  ::nprpc::flat_buffer& buffer_;
  const std::uint32_t offset_;

  auto& base() noexcept { return *reinterpret_cast<nprpc_nameserver_M2*>(reinterpret_cast<std::byte*>(buffer_.data().data()) + offset_); }
  auto const& base() const noexcept { return *reinterpret_cast<const nprpc_nameserver_M2*>(reinterpret_cast<const std::byte*>(buffer_.data().data()) + offset_); }
public:
  uint32_t offset() const noexcept { return offset_; }
  void* __data() noexcept { return (void*)&base(); }
  nprpc_nameserver_M2_Direct(::nprpc::flat_buffer& buffer, std::uint32_t offset)
    : buffer_(buffer)
    , offset_(offset)
  {
  }
  void _1(const char* str) { new (&base()._1) ::nprpc::flat::String(buffer_, str); }
  void _1(const std::string& str) { new (&base()._1) ::nprpc::flat::String(buffer_, str); }
  auto _1() noexcept { return (::nprpc::flat::Span<char>)base()._1; }
  auto _1() const noexcept { return (::nprpc::flat::Span<const char>)base()._1; }
  auto _1_d() noexcept { return ::nprpc::flat::String_Direct1(buffer_, offset_ + offsetof(nprpc_nameserver_M2, _1)); }
};

struct nprpc_nameserver_M3 {
  ::nprpc::flat::Boolean _1;
  ::nprpc::detail::flat::ObjectId _2;
};

class nprpc_nameserver_M3_Direct {
  ::nprpc::flat_buffer& buffer_;
  const std::uint32_t offset_;

  auto& base() noexcept { return *reinterpret_cast<nprpc_nameserver_M3*>(reinterpret_cast<std::byte*>(buffer_.data().data()) + offset_); }
  auto const& base() const noexcept { return *reinterpret_cast<const nprpc_nameserver_M3*>(reinterpret_cast<const std::byte*>(buffer_.data().data()) + offset_); }
public:
  uint32_t offset() const noexcept { return offset_; }
  void* __data() noexcept { return (void*)&base(); }
  nprpc_nameserver_M3_Direct(::nprpc::flat_buffer& buffer, std::uint32_t offset)
    : buffer_(buffer)
    , offset_(offset)
  {
  }
  const ::nprpc::flat::Boolean& _1() const noexcept { return base()._1;}
  ::nprpc::flat::Boolean& _1() noexcept { return base()._1;}
  auto _2() noexcept { return ::nprpc::detail::flat::ObjectId_Direct(buffer_, offset_ + offsetof(nprpc_nameserver_M3, _2)); }
};


} // 

void Nameserver::Bind(const ObjectId& obj, const std::string& name) {
  ::nprpc::flat_buffer buf;
  auto session = ::nprpc::impl::g_rpc->get_session(this->get_endpoint());
  if (!::nprpc::impl::g_rpc->prepare_zero_copy_buffer(session->ctx(), buf, 216))
    buf.prepare(216);
  {
    buf.commit(88);
    static_cast<::nprpc::impl::Header*>(buf.data().data())->msg_id = ::nprpc::impl::MessageId::FunctionCall;
  static_cast<::nprpc::impl::Header*>(buf.data().data())->msg_type =::nprpc::impl::MessageType::Request;
  }
  ::nprpc::impl::flat::CallHeader_Direct __ch(buf, sizeof(::nprpc::impl::Header));
  __ch.object_id() = this->object_id();
  __ch.poa_idx() = this->poa_idx();
  __ch.interface_idx() = interface_idx_;
  __ch.function_idx() = 0;
  nprpc_nameserver_M1_Direct _(buf,32);
  {
    auto tmp = _._1();
    ::nprpc::detail::helpers::assign_from_cpp_ObjectId(tmp, obj.get_data());
  }
  _._2(name);
  static_cast<::nprpc::impl::Header*>(buf.data().data())->size = static_cast<uint32_t>(buf.size() - 4);
  session->send_receive(buf, this->get_timeout());
  auto std_reply = ::nprpc::impl::handle_standart_reply(buf);
  if (std_reply != 0) {
    throw ::nprpc::Exception("Unknown Error");
  }
}

bool Nameserver::Resolve(const std::string& name, Object*& obj) {
  ::nprpc::flat_buffer buf;
  auto session = ::nprpc::impl::g_rpc->get_session(this->get_endpoint());
  if (!::nprpc::impl::g_rpc->prepare_zero_copy_buffer(session->ctx(), buf, 168))
    buf.prepare(168);
  {
    buf.commit(40);
    static_cast<::nprpc::impl::Header*>(buf.data().data())->msg_id = ::nprpc::impl::MessageId::FunctionCall;
  static_cast<::nprpc::impl::Header*>(buf.data().data())->msg_type =::nprpc::impl::MessageType::Request;
  }
  ::nprpc::impl::flat::CallHeader_Direct __ch(buf, sizeof(::nprpc::impl::Header));
  __ch.object_id() = this->object_id();
  __ch.poa_idx() = this->poa_idx();
  __ch.interface_idx() = interface_idx_;
  __ch.function_idx() = 1;
  nprpc_nameserver_M2_Direct _(buf,32);
  _._1(name);
  static_cast<::nprpc::impl::Header*>(buf.data().data())->size = static_cast<uint32_t>(buf.size() - 4);
  session->send_receive(buf, this->get_timeout());
  auto std_reply = ::nprpc::impl::handle_standart_reply(buf);
  if (std_reply != -1) {
    throw ::nprpc::Exception("Unknown Error");
  }
  nprpc_nameserver_M3_Direct out(buf, sizeof(::nprpc::impl::Header));
    obj = ::nprpc::impl::create_object_from_flat(out._2(), this->get_endpoint());
    bool __ret_value;
    __ret_value = (bool)out._1();
  return __ret_value;
}

void INameserver_Servant::dispatch(::nprpc::SessionContext& ctx, [[maybe_unused]] bool from_parent) {
  assert(ctx.rx_buffer != nullptr);
  auto* header = static_cast<::nprpc::impl::Header*>(ctx.rx_buffer->data().data());
  if (header->msg_id == ::nprpc::impl::MessageId::StreamInitialization) {
    ::nprpc::impl::flat::StreamInit_Direct init(*ctx.rx_buffer, sizeof(::nprpc::impl::Header));
    switch(init.func_idx()) {
      default:
        // Error
        break;
    }
    return;
  }
  ::nprpc::impl::flat::CallHeader_Direct __ch(*ctx.rx_buffer, sizeof(::nprpc::impl::Header));
  switch(__ch.function_idx()) {
    case 0: {
      assert(ctx.rx_buffer != nullptr);
      nprpc_nameserver_M1_Direct ia(*ctx.rx_buffer, 32);
      Bind(::nprpc::impl::create_object_from_flat(ia._1(), ctx.remote_endpoint), ia._2());
      ::nprpc::impl::make_simple_answer(ctx, nprpc::impl::MessageId::Success);
      break;
    }
    case 1: {
      assert(ctx.rx_buffer != nullptr);
      nprpc_nameserver_M2_Direct ia(*ctx.rx_buffer, 32);
      assert(ctx.tx_buffer != nullptr);
      auto& obuf = *ctx.tx_buffer;
      obuf.consume(obuf.size());
      if (!::nprpc::impl::g_rpc->prepare_zero_copy_buffer(ctx, obuf, 200))
         obuf.prepare(200);
      obuf.commit(72);
      nprpc_nameserver_M3_Direct oa(obuf,16);
      bool __ret_val;
      __ret_val = Resolve(ia._1(), oa._2());
      oa._1() = __ret_val;
      static_cast<::nprpc::impl::Header*>(obuf.data().data())->size = static_cast<uint32_t>(obuf.size() - 4);
      static_cast<::nprpc::impl::Header*>(obuf.data().data())->msg_id = ::nprpc::impl::MessageId::BlockResponse;
      static_cast<::nprpc::impl::Header*>(obuf.data().data())->msg_type = ::nprpc::impl::MessageType::Answer;
      break;
    }
    default:
      ::nprpc::impl::make_simple_answer(ctx, ::nprpc::impl::MessageId::Error_UnknownFunctionIdx);
  }
}

} // module nprpc::common

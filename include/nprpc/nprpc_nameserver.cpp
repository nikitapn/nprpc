#include "nprpc_nameserver.hpp"
#include <nprpc/impl/nprpc_impl.hpp>

void nprpc_nameserver_throw_exception(::nprpc::flat_buffer& buf);

namespace {
struct nprpc_nameserver_M1 {
  nprpc::detail::flat::ObjectId _1;
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
  auto _1() noexcept { return nprpc::detail::flat::ObjectId_Direct(buffer_, offset_ + offsetof(nprpc_nameserver_M1, _1)); }
  void _2(const char* str) { new (&base()._2) ::nprpc::flat::String(buffer_, str); }
  void _2(const std::string& str) { new (&base()._2) ::nprpc::flat::String(buffer_, str); }
  auto _2() noexcept { return (::nprpc::flat::Span<char>)base()._2; }
  auto _2() const noexcept { return (::nprpc::flat::Span<const char>)base()._2; }
  auto _2_d() noexcept {     return ::nprpc::flat::String_Direct1(buffer_, offset_ + offsetof(nprpc_nameserver_M1, _2));  }
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
  auto _1_d() noexcept {     return ::nprpc::flat::String_Direct1(buffer_, offset_ + offsetof(nprpc_nameserver_M2, _1));  }
};

struct nprpc_nameserver_M3 {
  ::nprpc::flat::Boolean _1;
  nprpc::detail::flat::ObjectId _2;
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
  auto _2() noexcept { return nprpc::detail::flat::ObjectId_Direct(buffer_, offset_ + offsetof(nprpc_nameserver_M3, _2)); }
};


} // 

namespace nprpc { 
void nprpc::Nameserver::Bind(/*in*/const ObjectId& obj, /*in*/const std::string& name) {
  ::nprpc::flat_buffer buf;
  {
    auto mb = buf.prepare(208);
    buf.commit(80);
    static_cast<::nprpc::impl::Header*>(mb.data())->msg_id = ::nprpc::impl::MessageId::FunctionCall;
    static_cast<::nprpc::impl::Header*>(mb.data())->msg_type = ::nprpc::impl::MessageType::Request;
  }
  ::nprpc::impl::flat::CallHeader_Direct __ch(buf, sizeof(::nprpc::impl::Header));
  __ch.object_id() = this->_data().object_id;
  __ch.poa_idx() = this->_data().poa_idx;
  __ch.interface_idx() = interface_idx_;
  __ch.function_idx() = 0;
  nprpc_nameserver_M1_Direct _(buf,32);
  memcpy(_._1().__data(), &obj._data(), 24);
  _._1().class_id(obj._data().class_id);
  _._1().hostname(obj._data().hostname);
  _._2(name);
  static_cast<::nprpc::impl::Header*>(buf.data().data())->size = static_cast<uint32_t>(buf.size() - 4);
  ::nprpc::impl::g_orb->call(this->get_endpoint(), buf, this->get_timeout());
  auto std_reply = nprpc::impl::handle_standart_reply(buf);
  if (std_reply != 0) {
    std::cerr << "received an unusual reply for function with no output arguments\n";
  }
}

bool nprpc::Nameserver::Resolve(/*in*/const std::string& name, /*out*/Object*& obj) {
  ::nprpc::flat_buffer buf;
  {
    auto mb = buf.prepare(168);
    buf.commit(40);
    static_cast<::nprpc::impl::Header*>(mb.data())->msg_id = ::nprpc::impl::MessageId::FunctionCall;
    static_cast<::nprpc::impl::Header*>(mb.data())->msg_type = ::nprpc::impl::MessageType::Request;
  }
  ::nprpc::impl::flat::CallHeader_Direct __ch(buf, sizeof(::nprpc::impl::Header));
  __ch.object_id() = this->_data().object_id;
  __ch.poa_idx() = this->_data().poa_idx;
  __ch.interface_idx() = interface_idx_;
  __ch.function_idx() = 1;
  nprpc_nameserver_M2_Direct _(buf,32);
  _._1(name);
  static_cast<::nprpc::impl::Header*>(buf.data().data())->size = static_cast<uint32_t>(buf.size() - 4);
  ::nprpc::impl::g_orb->call(this->get_endpoint(), buf, this->get_timeout());
  auto std_reply = nprpc::impl::handle_standart_reply(buf);
  if (std_reply != -1) {
    std::cerr << "received an unusual reply for function with output arguments\n";
    throw nprpc::Exception("Unknown Error");
  }
  nprpc_nameserver_M3_Direct out(buf, sizeof(::nprpc::impl::Header));
    obj = nprpc::impl::create_object_from_flat(out._2(), this->get_endpoint());
    bool __ret_value;
    __ret_value = (bool)out._1();
  return __ret_value;
}

void nprpc::INameserver_Servant::dispatch(nprpc::Buffers& bufs, nprpc::EndPoint remote_endpoint, bool from_parent, nprpc::ReferenceList& ref_list) {
  nprpc::impl::flat::CallHeader_Direct __ch(bufs(), sizeof(::nprpc::impl::Header));
  switch(__ch.function_idx()) {
    case 0: {
      nprpc_nameserver_M1_Direct ia(bufs(), 32);
      Bind(nprpc::impl::create_object_from_flat(ia._1(), remote_endpoint), ia._2());
      nprpc::impl::make_simple_answer(bufs(), nprpc::impl::MessageId::Success);
      break;
    }
    case 1: {
      nprpc_nameserver_M2_Direct ia(bufs(), 32);
      auto& obuf = bufs.flip();
      obuf.consume(obuf.size());
      obuf.prepare(192);
      obuf.commit(64);
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
      nprpc::impl::make_simple_answer(bufs(), nprpc::impl::MessageId::Error_UnknownFunctionIdx);
  }
}

} // namespace nprpc


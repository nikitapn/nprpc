#include "nprpc_nameserver.hpp"
#include "nprpc_nameserver_m.hpp"
#include <nprpc/nprpc_impl.hpp>

void nprpc_nameserver_throw_exception(boost::beast::flat_buffer& buf);

namespace nprpc { 
void nprpc::Nameserver::Bind(/*in*/const ObjectId& obj, /*in*/const std::string& name) {
  boost::beast::flat_buffer buf;
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
  ::flat::nprpc_nameserver_M1_Direct _(buf,32);
  memcpy(_._1().__data(), &obj._data(), 24);
  _._1().class_id(obj._data().class_id);
  _._1().hostname(obj._data().hostname);
  _._2(name);
  static_cast<::nprpc::impl::Header*>(buf.data().data())->size = static_cast<uint32_t>(buf.size() - 4);
  ::nprpc::impl::g_orb->call(
    nprpc::EndPoint(this->_data().ip4, this->_data().port), buf, this->get_timeout()
  );
  auto std_reply = nprpc::impl::handle_standart_reply(buf);
  if (std_reply != 0) {
    std::cerr << "received an unusual reply for function with no output arguments\n";
  }
}

bool nprpc::Nameserver::Resolve(/*in*/const std::string& name, /*out*/Object*& obj) {
  boost::beast::flat_buffer buf;
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
  ::flat::nprpc_nameserver_M2_Direct _(buf,32);
  _._1(name);
  static_cast<::nprpc::impl::Header*>(buf.data().data())->size = static_cast<uint32_t>(buf.size() - 4);
  ::nprpc::impl::g_orb->call(
    nprpc::EndPoint(this->_data().ip4, this->_data().port), buf, this->get_timeout()
  );
  auto std_reply = nprpc::impl::handle_standart_reply(buf);
  if (std_reply != -1) {
    std::cerr << "received an unusual reply for function with output arguments\n";
    throw nprpc::Exception("Unknown Error");
  }
  ::flat::nprpc_nameserver_M3_Direct out(buf, sizeof(::nprpc::impl::Header));
  obj = this->create_from_object_id(out._2());
  bool __ret_value;
  __ret_value = out._1();
  return __ret_value;
}

void nprpc::INameserver_Servant::dispatch(nprpc::Buffers& bufs, nprpc::EndPoint remote_endpoint, bool from_parent, nprpc::ReferenceList& ref_list) {
  nprpc::impl::flat::CallHeader_Direct __ch(bufs(), sizeof(::nprpc::impl::Header));
  switch(__ch.function_idx()) {
    case 0: {
      ::flat::nprpc_nameserver_M1_Direct ia(bufs(), 32);
      Bind(nprpc::impl::g_orb->create_object_from_flat(ia._1(), remote_endpoint), ia._2());
      nprpc::impl::make_simple_answer(bufs(), nprpc::impl::MessageId::Success);
      break;
    }
    case 1: {
      ::flat::nprpc_nameserver_M2_Direct ia(bufs(), 32);
      auto& obuf = bufs.flip();
      obuf.consume(obuf.size());
      obuf.prepare(192);
      obuf.commit(64);
      ::flat::nprpc_nameserver_M3_Direct oa(obuf,16);
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


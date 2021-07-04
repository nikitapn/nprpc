// Copyright (c) 2021 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

#include "nprpc_nameserver.hpp"
#include "nprpc_nameserver_m.hpp"
#include <nprpc/nprpc_impl.hpp>

void throw_exception(boost::beast::flat_buffer& buf);

namespace nprpc { 
void nprpc::Nameserver::Bind(/*in*/const ObjectId& obj, /*in*/const std::string& name) {
  boost::beast::flat_buffer buf;
  {
    auto mb = buf.prepare(192);
    buf.commit(64);
    *((uint32_t*)mb.data() + 1) = ::nprpc::impl::MessageId::FunctionCall;
  }
  ::nprpc::detail::flat::CallHeader_Direct __ch(buf, 4 + 4);
  __ch.object_id() = this->_data().object_id;
  __ch.poa_idx() = this->_data().poa_idx;
  __ch.interface_idx() = interface_idx_;
  __ch.function_idx() = 0;
  ::flat::NPRPC_NAMESERVER__M1_Direct _(buf,24);
  memcpy(&_._1().ip4(), &obj._data().ip4, 20);
  _._1().class_id(obj._data().class_id);
  _._2(name);
  *(uint32_t*)buf.data().data() = static_cast<uint32_t>(buf.size() - 4);
  ::nprpc::impl::g_orb->call(
    nprpc::EndPoint(this->_data().ip4, this->_data().port), buf, this->get_timeout()
  );
  auto std_reply = nprpc::impl::handle_standart_reply(buf);
  if (std_reply != 0) {
    std::cerr << "received an unusual reply for function with no output arguments\n";
    assert(false);
  }
}

bool nprpc::Nameserver::Resolve(/*in*/const std::string& name, /*out*/Object*& obj) {
  boost::beast::flat_buffer buf;
  {
    auto mb = buf.prepare(160);
    buf.commit(32);
    *((uint32_t*)mb.data() + 1) = ::nprpc::impl::MessageId::FunctionCall;
  }
  ::nprpc::detail::flat::CallHeader_Direct __ch(buf, 4 + 4);
  __ch.object_id() = this->_data().object_id;
  __ch.poa_idx() = this->_data().poa_idx;
  __ch.interface_idx() = interface_idx_;
  __ch.function_idx() = 1;
  ::flat::NPRPC_NAMESERVER__M2_Direct _(buf,24);
  _._1(name);
  *(uint32_t*)buf.data().data() = static_cast<uint32_t>(buf.size() - 4);
  ::nprpc::impl::g_orb->call(
    nprpc::EndPoint(this->_data().ip4, this->_data().port), buf, this->get_timeout()
  );
  auto std_reply = nprpc::impl::handle_standart_reply(buf);
  if (std_reply != -1) {
    std::cerr << "received an unusual reply for function with output arguments\n";
    assert(false);
    throw nprpc::Exception("Unknown Error");
  }
  ::flat::NPRPC_NAMESERVER__M3_Direct out(buf, 8);
  obj = this->create_from_object_id(out._2());
  bool __ret_value;
  __ret_value = out._1();
  return __ret_value;
}

void nprpc::INameserver_Servant::dispatch(nprpc::Buffers& bufs, nprpc::EndPoint remote_endpoint, bool from_parent, nprpc::ReferenceList& ref_list) {
  nprpc::detail::flat::CallHeader_Direct __ch(bufs(), 4 + 4);
  switch(__ch.function_idx()) {
    case 0: {
      ::flat::NPRPC_NAMESERVER__M1_Direct ia(bufs(), 24);
      Bind(nprpc::impl::g_orb->create_object_from_flat(ia._1(), remote_endpoint), ia._2());
      nprpc::impl::make_simple_message(bufs(), nprpc::impl::MessageId::Success);
      break;
    }
    case 1: {
      ::flat::NPRPC_NAMESERVER__M2_Direct ia(bufs(), 24);
      auto& obuf = bufs.flip();
      obuf.consume(obuf.size());
      obuf.prepare(176);
      obuf.commit(48);
      ::flat::NPRPC_NAMESERVER__M3_Direct oa(obuf,8);
bool __ret_val;
      __ret_val = Resolve(ia._1(), oa._2());
  oa._1() = __ret_val;
      *((uint32_t*)obuf.data().data()) = static_cast<uint32_t>(obuf.size() - 4);
      *((uint32_t*)obuf.data().data() + 1) = ::nprpc::impl::MessageId::BlockResponse;
      break;
    }
    default:
      nprpc::impl::make_simple_message(bufs(), nprpc::impl::MessageId::Error_UnknownFunctionIdx);
  }
}

} // namespace nprpc


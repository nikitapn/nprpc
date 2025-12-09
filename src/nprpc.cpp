// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include <boost/uuid/uuid_io.hpp>
#include <iomanip>
#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/impl/uuid.hpp>
#include <sstream>

#include "logging.hpp"

using namespace nprpc;

namespace nprpc {

NPRPC_API RpcBuilder::RpcBuilder() : impl::RpcBuilderBase(cfg_)
{
  auto& uuid = impl::SharedUUID::instance().get();
  memcpy(cfg_.uuid.data(), &uuid, 16);

  if (1) {
    std::string buf(36, '0');
    bool ret = boost::uuids::to_chars(
        reinterpret_cast<const boost::uuids::uuid&>(uuid), buf.data(),
        buf.data() + buf.size());
    assert(ret);
    NPRPC_LOG_INFO("nprpc UUID: {}", buf);
  }
}

NPRPC_API uint32_t ObjectServant::release() noexcept
{
  if (static_cast<impl::PoaImpl*>(poa_.get())->get_lifespan() ==
      PoaPolicy::Lifespan::Persistent) {
    return 1;
  }

  // std::cout << "ObjectServant::release() called for object with ID: "<<
  // object_id_ <<
  // "\n ref_cnt: " << ref_cnt_.load() <<
  // "\n class_id: " << get_class() << std::endl;

  assert(is_unused() == false);

  auto cnt = ref_cnt_.fetch_sub(1, std::memory_order_acquire) - 1;
  if (cnt == 0) {
    static_cast<impl::PoaImpl*>(poa_.get())->deactivate_object(object_id_);
    impl::PoaImpl::delete_object(this);
  }

  return cnt;
}

NPRPC_API uint32_t Object::add_ref()
{
  auto const cnt = local_ref_cnt_.fetch_add(1, std::memory_order_release);
  if (policy_lifespan() == PoaPolicy::Lifespan::Persistent || cnt)
    return cnt + 1;

  flat_buffer buf;

  auto constexpr msg_size =
      sizeof(impl::Header) + sizeof(::nprpc::detail::flat::ObjectIdLocal);

  auto mb = buf.prepare(msg_size);
  buf.commit(msg_size);

  static_cast<impl::Header*>(mb.data())->size = msg_size - 4;
  static_cast<impl::Header*>(mb.data())->msg_id = impl::MessageId::AddReference;
  static_cast<impl::Header*>(mb.data())->msg_type = impl::MessageType::Request;

  ::nprpc::detail::flat::ObjectIdLocal_Direct msg(buf, sizeof(impl::Header));
  msg.object_id() = object_id();
  msg.poa_idx() = poa_idx();

  nprpc::impl::g_rpc->call_async(get_endpoint(), std::move(buf), std::nullopt);

  return cnt + 1;
}

NPRPC_API uint32_t Object::release()
{
  auto cnt = --local_ref_cnt_;
  if (cnt != 0)
    return cnt;

  if (::nprpc::impl::g_rpc == nullptr) {
    delete this;
    return 0;
  }

  if (policy_lifespan() == PoaPolicy::Lifespan::Transient) {
    const auto& endpoint = get_endpoint();

    if (endpoint.type() == EndPointType::TcpTethered &&
        ::nprpc::impl::g_rpc->has_session(endpoint) == false) {
      // session was closed and cannot be created.
    } else {
      flat_buffer buf;

      auto constexpr msg_size =
          sizeof(impl::Header) + sizeof(::nprpc::detail::flat::ObjectIdLocal);
      auto mb = buf.prepare(msg_size);
      buf.commit(msg_size);

      static_cast<impl::Header*>(mb.data())->size = msg_size - 4;
      static_cast<impl::Header*>(mb.data())->msg_id =
          impl::MessageId::ReleaseObject;
      static_cast<impl::Header*>(mb.data())->msg_type =
          impl::MessageType::Request;

      nprpc::detail::flat::ObjectIdLocal_Direct msg(buf, sizeof(impl::Header));
      msg.object_id() = object_id();
      msg.poa_idx() = poa_idx();

      try {
        ::nprpc::impl::g_rpc->call_async(
            endpoint, std::move(buf),
            [](const boost::system::error_code&, flat_buffer&) {
              // if (!ec) {
              // auto std_reply =
              // nprpc::impl::handle_standart_reply(buf); if
              // (std_reply == false) {
              //	std::cerr << "received an unusual reply for
              // function with no
              // output arguments" << std::endl;
              // }
              //}
            });
      } catch (Exception& ex) {
        NPRPC_LOG_ERROR("{}", ex.what());
      }
    }
  }

  delete this;

  return 0;
}

NPRPC_API bool
Object::select_endpoint(std::optional<EndPoint> remote_endpoint) noexcept
{
  try {
    std::string& urls = data_.urls;
    size_t start = [&urls, this, &remote_endpoint] {
      const auto same_machine = is_same_origin(impl::g_cfg.uuid);
      auto try_replace_ip = [&](size_t pos, std::string_view prefix) {
        if (same_machine || !remote_endpoint)
          return;

        auto start = pos + prefix.length();
        auto end = urls.find(':', start);
        auto ipv4_str = urls.substr(start, end - start);

        boost::system::error_code ec;
        auto ipv4_addr = nprpc::impl::net::ip::make_address_v4(ipv4_str, ec);
        if ((!ec && ipv4_addr.to_uint() == 0x7F000001) ||
            ipv4_str == "localhost") {
          // Change ip from localhost or 127.0.0.1 to ip of the remote
          // endpoint
          auto remote_ip = remote_endpoint->hostname();
          assert(remote_ip.size() > 0 &&
                 "Remote endpoint must have a valid hostname");
          urls =
              urls.substr(0, start) + std::string(remote_ip) + urls.substr(end);
        }
      };

      size_t pos = std::string::npos, pos2 = std::string::npos;
      if (same_machine &&
          ((pos = urls.find(mem_prefix)) != std::string::npos)) {
        // Prefer shared memory if possible
        return pos;
      }

      // Check for QUIC endpoint (preferred over UDP for unreliable when
      // available) Note: Don't replace IP for QUIC - TLS certificates
      // must match the hostname
      if ((pos = urls.find(quic_prefix)) != std::string::npos) {
        return pos;
      }

      // Check for UDP endpoint
      if ((pos = urls.find(udp_prefix)) != std::string::npos) {
        try_replace_ip(pos, udp_prefix);
        return pos;
      }

      if ((pos = urls.find(tcp_prefix)) != std::string::npos)
        try_replace_ip(pos, tcp_prefix);

      if ((pos2 = urls.find(ws_prefix)) != std::string::npos)
        try_replace_ip(pos2, ws_prefix);

      if (pos != std::string::npos)
        return pos;

      if (pos2 != std::string::npos)
        return pos2;

      if ((pos = urls.find(wss_prefix)) != std::string::npos)
        return pos;

      throw std::runtime_error("No valid endpoint found for object " +
                               class_id() + " with urls: " + urls);
    }();

    auto end = urls.find(';', start);
    auto size = (end != std::string::npos) ? end - start : std::string::npos;
    endpoint_ = EndPoint(urls.substr(start, size));
    return true;
  } catch (const std::exception& ex) {
    NPRPC_LOG_ERROR("Failed to select endpoint: {}", ex.what());
  }
  return false;
}

NPRPC_API uint32_t ObjectServant::add_ref() noexcept
{
  auto cnt = ref_cnt_.fetch_add(1, std::memory_order_release) + 1;

  //	if (cnt == 1 && static_cast<impl::PoaImpl*>(poa())->pl_lifespan ==
  // Policy_Lifespan::Transient) { 		std::lock_guard<std::mutex>
  // lk(impl::g_rpc->new_activated_objects_mut_); 		auto& list =
  // impl::g_rpc->new_activated_objects_; list.erase(std::find(begin(list),
  // end(list), this));
  //	}

  return cnt;
}

ReferenceList::ReferenceList() noexcept
{
  impl_ = new impl::ReferenceListImpl();
}

ReferenceList::~ReferenceList() { delete impl_; }

void ReferenceList::add_ref(ObjectServant* obj) { impl_->add_ref(obj); }

bool ReferenceList::remove_ref(poa_idx_t poa_idx, oid_t oid)
{
  return impl_->remove_ref(poa_idx, oid);
}

Poa* PoaBuilder::build()
{
  return static_cast<impl::RpcImpl*>(rpc_)->create_poa_impl(
      objects_max_, lifespan_policy_, object_id_policy_);
}

} // namespace nprpc

namespace nprpc::impl {

// Simple hex dump helper
static std::string to_hex(const std::vector<unsigned char>& data)
{
  std::ostringstream oss;
  oss << std::hex << std::setfill('0');
  for (size_t i = 0; i < data.size(); ++i) {
    if (i > 0 && i % 16 == 0)
      oss << '\n';
    oss << std::setw(2) << static_cast<int>(data[i]) << ' ';
  }
  return oss.str();
}

void dump_message(flat_buffer& buffer, bool rx)
{
  auto cb = buffer.cdata();
  auto size = cb.size();
  auto data = (unsigned char*)cb.data();

  // Create a vector for hex dump
  std::vector<unsigned char> vec(data, data + size);

  NPRPC_LOG_DEBUG("[nprpc] Message HEX16: {} size: {}\n{}",
                  (rx ? "rx." : "tx."), size, to_hex(vec).c_str());
}

} // namespace nprpc::impl

#include <nprpc/serialization/nvp.hpp>

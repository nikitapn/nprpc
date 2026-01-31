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

NPRPC_API RpcBuilder::RpcBuilder()
    : impl::RpcBuilderBase(cfg_)
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

// ============================================================================
// ObjectId/Object string serialization (NPRPC IOR format)
// Format: "NPRPC1:<base64_encoded_binary>"
// Binary format (little-endian):
//   - object_id: 8 bytes (uint64)
//   - poa_idx: 2 bytes (uint16)
//   - flags: 2 bytes (uint16)
//   - origin: 16 bytes (uuid)
//   - class_id_len: 4 bytes (uint32)
//   - class_id: variable
//   - urls_len: 4 bytes (uint32)
//   - urls: variable
// ============================================================================

namespace {

// Base64 encoding/decoding tables
constexpr char base64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64_encode(const std::vector<uint8_t>& data)
{
  std::string result;
  result.reserve(((data.size() + 2) / 3) * 4);

  size_t i = 0;
  while (i < data.size()) {
    uint32_t octet_a = i < data.size() ? data[i++] : 0;
    uint32_t octet_b = i < data.size() ? data[i++] : 0;
    uint32_t octet_c = i < data.size() ? data[i++] : 0;

    uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

    result += base64_chars[(triple >> 18) & 0x3F];
    result += base64_chars[(triple >> 12) & 0x3F];
    result += (i > data.size() + 1) ? '=' : base64_chars[(triple >> 6) & 0x3F];
    result += (i > data.size()) ? '=' : base64_chars[triple & 0x3F];
  }

  return result;
}

std::vector<uint8_t> base64_decode(std::string_view encoded)
{
  static constexpr uint8_t decode_table[256] = {
      64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
      64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
      64, 64, 64, 64, 64, 64, 64, 62, 64, 64, 64, 63, 52, 53, 54, 55, 56, 57,
      58, 59, 60, 61, 64, 64, 64, 64, 64, 64, 64, 0,  1,  2,  3,  4,  5,  6,
      7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
      25, 64, 64, 64, 64, 64, 64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36,
      37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 64, 64, 64,
      64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
      64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
      64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
      64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
      64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
      64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
      64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
      64, 64, 64, 64};

  std::vector<uint8_t> result;
  result.reserve((encoded.size() * 3) / 4);

  uint32_t bits = 0;
  int bit_count = 0;

  for (char c : encoded) {
    if (c == '=')
      break;
    uint8_t val = decode_table[static_cast<uint8_t>(c)];
    if (val == 64)
      return {};  // Invalid character

    bits = (bits << 6) | val;
    bit_count += 6;

    if (bit_count >= 8) {
      bit_count -= 8;
      result.push_back(static_cast<uint8_t>((bits >> bit_count) & 0xFF));
    }
  }

  return result;
}

template <typename T>
void write_le(std::vector<uint8_t>& buf, T value)
{
  for (size_t i = 0; i < sizeof(T); ++i) {
    buf.push_back(static_cast<uint8_t>(value & 0xFF));
    value >>= 8;
  }
}

template <typename T>
bool read_le(const uint8_t*& ptr, const uint8_t* end, T& value)
{
  if (ptr + sizeof(T) > end)
    return false;
  value = 0;
  for (size_t i = 0; i < sizeof(T); ++i) {
    value |= static_cast<T>(*ptr++) << (i * 8);
  }
  return true;
}

bool read_string(const uint8_t*& ptr, const uint8_t* end, std::string& str)
{
  uint32_t len;
  if (!read_le(ptr, end, len))
    return false;
  if (ptr + len > end)
    return false;
  str.assign(reinterpret_cast<const char*>(ptr), len);
  ptr += len;
  return true;
}

}  // anonymous namespace

namespace nprpc {

NPRPC_API std::string ObjectId::to_string() const
{
  std::vector<uint8_t> buf;
  buf.reserve(64 + data_.class_id.size() + data_.urls.size());

  // Write fixed fields (little-endian)
  write_le(buf, data_.object_id);
  write_le(buf, data_.poa_idx);
  write_le(buf, data_.flags);

  // Write origin UUID (16 bytes)
  for (auto b : data_.origin) {
    buf.push_back(b);
  }

  // Write class_id (length-prefixed)
  write_le(buf, static_cast<uint32_t>(data_.class_id.size()));
  for (char c : data_.class_id) {
    buf.push_back(static_cast<uint8_t>(c));
  }

  // Write urls (length-prefixed)
  write_le(buf, static_cast<uint32_t>(data_.urls.size()));
  for (char c : data_.urls) {
    buf.push_back(static_cast<uint8_t>(c));
  }

  return "NPRPC1:" + base64_encode(buf);
}

NPRPC_API bool ObjectId::from_string(std::string_view str)
{
  // Check prefix
  constexpr std::string_view prefix = "NPRPC1:";
  if (str.size() < prefix.size() || str.substr(0, prefix.size()) != prefix) {
    return false;
  }

  // Decode base64
  auto data = base64_decode(str.substr(prefix.size()));
  if (data.empty() && str.size() > prefix.size()) {
    return false;  // Decode error
  }

  const uint8_t* ptr = data.data();
  const uint8_t* end = ptr + data.size();

  // Read fixed fields
  if (!read_le(ptr, end, data_.object_id))
    return false;
  if (!read_le(ptr, end, data_.poa_idx))
    return false;
  if (!read_le(ptr, end, data_.flags))
    return false;

  // Read origin (16 bytes)
  if (ptr + 16 > end)
    return false;
  std::copy(ptr, ptr + 16, data_.origin.begin());
  ptr += 16;

  // Read strings
  if (!read_string(ptr, end, data_.class_id))
    return false;
  if (!read_string(ptr, end, data_.urls))
    return false;

  return true;
}

NPRPC_API Object* Object::from_string(std::string_view str)
{
  auto* obj = new Object();
  if (!obj->ObjectId::from_string(str)) {
    delete obj;
    return nullptr;
  }

  // Auto-select endpoint after parsing
  if (!obj->select_endpoint()) {
    delete obj;
    return nullptr;
  }

  return obj;
}

}  // namespace nprpc

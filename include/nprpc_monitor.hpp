#ifndef __NPRPC_NPRPC_MONITOR_HPP__
#define __NPRPC_NPRPC_MONITOR_HPP__

#include <cstring>
#include <variant>
#include <nprpc/flat.hpp>
#include <nprpc/nprpc.hpp>
#include <nprpc/bidi_stream.hpp>
#include <nprpc/stream_writer.hpp>
#include <nprpc/stream_reader.hpp>

// Module export macro
#ifdef NPRPC_EXPORTS
#  define NPRPC_MONITOR_API NPRPC_EXPORT_ATTR
#else
#  define NPRPC_MONITOR_API NPRPC_IMPORT_ATTR
#endif

namespace nprpc::monitor {

enum class Transport : uint32_t {
  Http3,
  WebSocket,
  WebTransport,
  Quic
};
struct TransportSample {
  Transport transport;
  uint64_t completed;
  uint64_t errors;
  uint64_t received_bytes;
  uint64_t sent_bytes;
  uint64_t dispatch_ns;
  uint64_t handshake_failures;
  uint64_t throttled;
  /// Inbound connections open now; not cumulative, unlike the fields above.
  uint64_t active_connections;
  /// Inbound stream directions open now; a bidi stream counts twice.
  uint64_t active_streams;
};

namespace flat {
struct TransportSample {
  Transport transport;
  uint64_t completed;
  uint64_t errors;
  uint64_t received_bytes;
  uint64_t sent_bytes;
  uint64_t dispatch_ns;
  uint64_t handshake_failures;
  uint64_t throttled;
  uint64_t active_connections;
  uint64_t active_streams;
};

class TransportSample_Direct {
  ::nprpc::flat_buffer& buffer_;
  const std::uint32_t offset_;

  auto& base() noexcept { return *reinterpret_cast<TransportSample*>(reinterpret_cast<std::byte*>(buffer_.data().data()) + offset_); }
  auto const& base() const noexcept { return *reinterpret_cast<const TransportSample*>(reinterpret_cast<const std::byte*>(buffer_.data().data()) + offset_); }
public:
  uint32_t offset() const noexcept { return offset_; }
  void* __data() noexcept { return (void*)&base(); }
  TransportSample_Direct(::nprpc::flat_buffer& buffer, std::uint32_t offset)
    : buffer_(buffer)
    , offset_(offset)
  {
  }
  const Transport& transport() const noexcept { return base().transport;}
  Transport& transport() noexcept { return base().transport;}
  const uint64_t& completed() const noexcept { return base().completed;}
  uint64_t& completed() noexcept { return base().completed;}
  const uint64_t& errors() const noexcept { return base().errors;}
  uint64_t& errors() noexcept { return base().errors;}
  const uint64_t& received_bytes() const noexcept { return base().received_bytes;}
  uint64_t& received_bytes() noexcept { return base().received_bytes;}
  const uint64_t& sent_bytes() const noexcept { return base().sent_bytes;}
  uint64_t& sent_bytes() noexcept { return base().sent_bytes;}
  const uint64_t& dispatch_ns() const noexcept { return base().dispatch_ns;}
  uint64_t& dispatch_ns() noexcept { return base().dispatch_ns;}
  const uint64_t& handshake_failures() const noexcept { return base().handshake_failures;}
  uint64_t& handshake_failures() noexcept { return base().handshake_failures;}
  const uint64_t& throttled() const noexcept { return base().throttled;}
  uint64_t& throttled() noexcept { return base().throttled;}
  const uint64_t& active_connections() const noexcept { return base().active_connections;}
  uint64_t& active_connections() noexcept { return base().active_connections;}
  const uint64_t& active_streams() const noexcept { return base().active_streams;}
  uint64_t& active_streams() noexcept { return base().active_streams;}
};
} // namespace flat

struct Snapshot {
  uint64_t uptime_ms;
  std::vector<TransportSample> transports;
  /// HTTP/1.1 has no transport row: TLS fails before a request can upgrade.
  uint64_t http1_handshake_failures;
  uint64_t http1_throttled;
};

namespace flat {
struct Snapshot {
  uint64_t uptime_ms;
  ::nprpc::flat::detail::Vector<flat::TransportSample> transports;
  uint64_t http1_handshake_failures;
  uint64_t http1_throttled;
};

class Snapshot_Direct {
  ::nprpc::flat_buffer& buffer_;
  const std::uint32_t offset_;

  auto& base() noexcept { return *reinterpret_cast<Snapshot*>(reinterpret_cast<std::byte*>(buffer_.data().data()) + offset_); }
  auto const& base() const noexcept { return *reinterpret_cast<const Snapshot*>(reinterpret_cast<const std::byte*>(buffer_.data().data()) + offset_); }
public:
  uint32_t offset() const noexcept { return offset_; }
  void* __data() noexcept { return (void*)&base(); }
  Snapshot_Direct(::nprpc::flat_buffer& buffer, std::uint32_t offset)
    : buffer_(buffer)
    , offset_(offset)
  {
  }
  const uint64_t& uptime_ms() const noexcept { return base().uptime_ms;}
  uint64_t& uptime_ms() noexcept { return base().uptime_ms;}
  void transports(std::uint32_t elements_size) { new (&base().transports) ::nprpc::flat::detail::Vector<flat::TransportSample>(buffer_, elements_size); }
  auto transports_d() noexcept { return ::nprpc::flat::Vector_Direct2<flat::TransportSample,flat::TransportSample_Direct>(buffer_, offset_ + offsetof(Snapshot, transports)); }
  auto transports() noexcept { return ::nprpc::flat::Span_ref<flat::TransportSample, flat::TransportSample_Direct>(buffer_, base().transports.range(buffer_.data().data())); }
  const uint64_t& http1_handshake_failures() const noexcept { return base().http1_handshake_failures;}
  uint64_t& http1_handshake_failures() noexcept { return base().http1_handshake_failures;}
  const uint64_t& http1_throttled() const noexcept { return base().http1_throttled;}
  uint64_t& http1_throttled() noexcept { return base().http1_throttled;}
};
} // namespace flat

class NPRPC_MONITOR_API IMonitor_Servant
  : public virtual ::nprpc::ObjectServant
{
public:
  static std::string_view _get_class() noexcept { return "nprpc_monitor/nprpc.monitor.Monitor"; }
  std::string_view get_class() const noexcept override { return IMonitor_Servant::_get_class(); }
  void dispatch(::nprpc::SessionContext& ctx, [[maybe_unused]] bool from_parent) override;
  virtual ::nprpc::StreamWriter<Snapshot> Watch (uint32_t interval_ms) = 0;
};

class NPRPC_MONITOR_API Monitor
  : public virtual ::nprpc::Object
{
  const uint8_t interface_idx_;
public:
  using servant_t = IMonitor_Servant;

  Monitor(uint8_t interface_idx) : interface_idx_(interface_idx) {}
  ::nprpc::StreamReader<Snapshot> Watch (uint32_t interval_ms);
};

} // module nprpc::monitor
namespace nprpc::detail::stream_codec {
template<>
inline ::nprpc::monitor::Snapshot deserialize<::nprpc::monitor::Snapshot>(::nprpc::flat_buffer& buf) {
  ::nprpc::impl::flat::StreamChunk_Direct __chunk(buf, sizeof(::nprpc::impl::Header));
  auto __span = __chunk.data();
  ::nprpc::flat_buffer __elem_buf;
  auto __mb = __elem_buf.prepare(__span.size());
  std::memcpy(__mb.data(), __span.data(), __span.size());
  __elem_buf.commit(__span.size());
  ::nprpc::monitor::Snapshot __result;
  ::nprpc::monitor::flat::Snapshot_Direct __d(__elem_buf, 0);
  __result.uptime_ms = __d.uptime_ms();
  {
    auto span = __d.transports();
    __result.transports.resize(span.size());
    memcpy(__result.transports.data(), span.data(), 80 * span.size());
  }
  __result.http1_handshake_failures = __d.http1_handshake_failures();
  __result.http1_throttled = __d.http1_throttled();
  return __result;
}
} // namespace nprpc::detail::stream_codec

namespace nprpc::detail::stream_codec {
template<>
inline ::nprpc::flat_buffer serialize<::nprpc::monitor::Snapshot>(const ::nprpc::monitor::Snapshot& value) {
  ::nprpc::flat_buffer __buf;
  __buf.prepare(32 + 128);
  __buf.commit(32);
  ::nprpc::monitor::flat::Snapshot_Direct __d(__buf, 0);
  __d.uptime_ms() = value.uptime_ms;
  __d.transports(static_cast<uint32_t>(value.transports.size()));
  memcpy(__d.transports().data(), value.transports.data(), value.transports.size() * 80);
  __d.http1_handshake_failures() = value.http1_handshake_failures;
  __d.http1_throttled() = value.http1_throttled;
  return __buf;
}
} // namespace nprpc::detail::stream_codec


#endif
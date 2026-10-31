#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <nprpc/export.hpp>
#include <nprpc_base.hpp>
#include <nprpc_monitor.hpp>
namespace nprpc::impl {
inline constexpr std::array kTelemetryTransports{
  monitor::Transport::Http3, monitor::Transport::WebSocket, monitor::Transport::WebTransport,
  monitor::Transport::Quic};

struct TelemetryCounters {
  uint64_t completed{}, errors{}, received_bytes{}, sent_bytes{}, dispatch_ns{};
  uint64_t handshake_failures{}, throttled{}, active_connections{}, active_streams{};
};
// Each field is exact, but a read can land between the fields of one request.
NPRPC_API TelemetryCounters read_telemetry(monitor::Transport transport);
NPRPC_API void record_telemetry(monitor::Transport transport, uint64_t received, uint64_t sent,
                                uint64_t nanoseconds, bool error) noexcept;

// These happen once per connection or rejection, and a server turning traffic
// away is the one that most needs them, so they are not gated on enablement.
NPRPC_API void record_handshake_failure(monitor::Transport transport) noexcept;
NPRPC_API void record_throttled(monitor::Transport transport) noexcept;
NPRPC_API void record_http1_handshake_failure() noexcept;
NPRPC_API void record_http1_throttled() noexcept;
NPRPC_API uint64_t read_http1_handshake_failures() noexcept;
NPRPC_API uint64_t read_http1_throttled() noexcept;

// For StreamManager::set_active_stream_counter().
NPRPC_API std::atomic<int64_t>* active_stream_counter(monitor::Transport transport) noexcept;

// One open inbound connection, for as long as this object lives.
class NPRPC_API ActiveConnection {
  monitor::Transport transport_;
public:
  explicit ActiveConnection(monitor::Transport transport) noexcept;
  ~ActiveConnection();
  ActiveConnection(const ActiveConnection&) = delete;
  ActiveConnection& operator=(const ActiveConnection&) = delete;
};

// Transports check this first, so a runtime without telemetry reads no clocks.
NPRPC_API extern std::atomic<bool> g_telemetry_enabled;
inline bool telemetry_enabled() noexcept { return g_telemetry_enabled.load(std::memory_order_relaxed); }

// Measures synchronous handler execution, not response delivery.  Only calls
// and stream openings count; rx_buffer is captured first because stream data
// dispatch moves it out.
template <typename Handle>
bool measured_dispatch(monitor::Transport transport, flat_buffer& rx_buffer, flat_buffer& tx_buffer, Handle&& handle)
{
  if (!telemetry_enabled()) return handle();
  const auto kind = rx_buffer.size() >= sizeof(flat::Header)
    ? flat::Header_Direct(rx_buffer, 0).msg_id() : MessageId::Error_BadInput;
  const auto received = rx_buffer.size();
  const auto start = std::chrono::steady_clock::now();
  const bool needs_reply = handle();
  if (kind == MessageId::FunctionCall || kind == MessageId::StreamInitialization) {
    const bool error = tx_buffer.size() >= sizeof(flat::Header) &&
      flat::Header_Direct(tx_buffer, 0).msg_id() >= MessageId::Exception;
    record_telemetry(transport, received, tx_buffer.size(),
      std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - start).count(), error);
  }
  return needs_reply;
}
}

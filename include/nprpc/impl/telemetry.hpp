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
  monitor::Transport::Http3, monitor::Transport::WebSocket, monitor::Transport::WebTransport};

struct TelemetryCounters {
  uint64_t completed{}, errors{}, received_bytes{}, sent_bytes{}, dispatch_ns{};
};
// Each field is exact, but a read can land between the fields of one request.
NPRPC_API TelemetryCounters read_telemetry(monitor::Transport transport);
NPRPC_API void record_telemetry(monitor::Transport transport, uint64_t received, uint64_t sent,
                                uint64_t nanoseconds, bool error) noexcept;

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

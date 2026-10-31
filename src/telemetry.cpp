#include <nprpc/impl/telemetry.hpp>
namespace {
struct alignas(64) Counters {
  std::atomic<uint64_t> completed{}, errors{}, received_bytes{}, sent_bytes{}, dispatch_ns{};
  std::atomic<uint64_t> handshake_failures{}, throttled{};
  // Signed, and clamped when read: a close can be counted before a racing open.
  std::atomic<int64_t> active_connections{}, active_streams{};
};
std::array<Counters, nprpc::impl::kTelemetryTransports.size()> counters;
std::atomic<uint64_t> http1_handshake_failures{}, http1_throttled{};

Counters* find(nprpc::monitor::Transport transport) noexcept {
  const auto index = static_cast<size_t>(transport);
  return index < counters.size() ? &counters[index] : nullptr;
}

uint64_t read_gauge(const std::atomic<int64_t>& gauge) noexcept {
  const auto value = gauge.load(std::memory_order_relaxed);
  return value > 0 ? static_cast<uint64_t>(value) : 0;
}
}
namespace nprpc::impl {
std::atomic<bool> g_telemetry_enabled{false};

void record_telemetry(monitor::Transport transport, uint64_t received, uint64_t sent,
                      uint64_t nanoseconds, bool error) noexcept {
  const auto index = static_cast<size_t>(transport);
  if (index >= counters.size()) return;
  auto& c = counters[index];
  c.completed.fetch_add(1, std::memory_order_relaxed);
  if (error) c.errors.fetch_add(1, std::memory_order_relaxed);
  c.received_bytes.fetch_add(received, std::memory_order_relaxed);
  c.sent_bytes.fetch_add(sent, std::memory_order_relaxed);
  c.dispatch_ns.fetch_add(nanoseconds, std::memory_order_relaxed);
}

TelemetryCounters read_telemetry(monitor::Transport transport) {
  const auto index = static_cast<size_t>(transport);
  if (index >= counters.size()) return {};
  auto& c = counters[index];
  return {c.completed.load(std::memory_order_relaxed), c.errors.load(std::memory_order_relaxed),
          c.received_bytes.load(std::memory_order_relaxed), c.sent_bytes.load(std::memory_order_relaxed),
          c.dispatch_ns.load(std::memory_order_relaxed),
          c.handshake_failures.load(std::memory_order_relaxed),
          c.throttled.load(std::memory_order_relaxed),
          read_gauge(c.active_connections), read_gauge(c.active_streams)};
}

void record_handshake_failure(monitor::Transport transport) noexcept {
  if (auto* c = find(transport)) c->handshake_failures.fetch_add(1, std::memory_order_relaxed);
}

void record_throttled(monitor::Transport transport) noexcept {
  if (auto* c = find(transport)) c->throttled.fetch_add(1, std::memory_order_relaxed);
}

void record_http1_handshake_failure() noexcept { http1_handshake_failures.fetch_add(1, std::memory_order_relaxed); }
void record_http1_throttled() noexcept { http1_throttled.fetch_add(1, std::memory_order_relaxed); }
uint64_t read_http1_handshake_failures() noexcept { return http1_handshake_failures.load(std::memory_order_relaxed); }
uint64_t read_http1_throttled() noexcept { return http1_throttled.load(std::memory_order_relaxed); }

std::atomic<int64_t>* active_stream_counter(monitor::Transport transport) noexcept {
  auto* c = find(transport);
  return c ? &c->active_streams : nullptr;
}

ActiveConnection::ActiveConnection(monitor::Transport transport) noexcept : transport_(transport) {
  if (auto* c = find(transport_)) c->active_connections.fetch_add(1, std::memory_order_relaxed);
}

ActiveConnection::~ActiveConnection() {
  if (auto* c = find(transport_)) c->active_connections.fetch_sub(1, std::memory_order_relaxed);
}
}

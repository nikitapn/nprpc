#include <nprpc/impl/telemetry.hpp>
namespace {
struct alignas(64) Counters {
  std::atomic<uint64_t> completed{}, errors{}, received_bytes{}, sent_bytes{}, dispatch_ns{};
};
std::array<Counters, nprpc::impl::kTelemetryTransports.size()> counters;
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
          c.dispatch_ns.load(std::memory_order_relaxed)};
}
}

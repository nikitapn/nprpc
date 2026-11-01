#include <nprpc/impl/telemetry.hpp>
#include <bit>
#include <filesystem>
#include <fstream>
#include <sstream>
namespace {
struct alignas(64) Counters {
  std::atomic<uint64_t> completed{}, errors{}, received_bytes{}, sent_bytes{}, dispatch_ns{};
  std::atomic<uint64_t> handshake_failures{}, throttled{};
  // Signed, and clamped when read: a close can be counted before a racing open.
  std::atomic<int64_t> active_connections{}, active_streams{};
  std::array<std::atomic<uint64_t>, nprpc::impl::kLatencyBuckets> latency{};
};
std::array<Counters, nprpc::impl::kTelemetryTransports.size()> counters;
std::atomic<uint64_t> http1_handshake_failures{}, http1_throttled{};

// 2xx to 5xx. Anything else is not a final response a client should see.
using StatusCounts = std::array<std::atomic<uint64_t>, 4>;
StatusCounts http1_status, http3_status;

void record_status(StatusCounts& counts, unsigned status) noexcept {
  if (status >= 200 && status < 600)
    counts[status / 100 - 2].fetch_add(1, std::memory_order_relaxed);
}

nprpc::monitor::HttpStatusCounts read_status(const StatusCounts& counts) noexcept {
  return {counts[0].load(std::memory_order_relaxed), counts[1].load(std::memory_order_relaxed),
          counts[2].load(std::memory_order_relaxed), counts[3].load(std::memory_order_relaxed)};
}

size_t latency_bucket(uint64_t nanoseconds) noexcept {
  const auto bucket = static_cast<size_t>(std::bit_width(nanoseconds / 1000));
  return bucket < nprpc::impl::kLatencyBuckets ? bucket : nprpc::impl::kLatencyBuckets - 1;
}

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
  c.latency[latency_bucket(nanoseconds)].fetch_add(1, std::memory_order_relaxed);
}

TelemetryCounters read_telemetry(monitor::Transport transport) {
  const auto index = static_cast<size_t>(transport);
  if (index >= counters.size()) return {};
  auto& c = counters[index];
  TelemetryCounters out{c.completed.load(std::memory_order_relaxed), c.errors.load(std::memory_order_relaxed),
          c.received_bytes.load(std::memory_order_relaxed), c.sent_bytes.load(std::memory_order_relaxed),
          c.dispatch_ns.load(std::memory_order_relaxed),
          c.handshake_failures.load(std::memory_order_relaxed),
          c.throttled.load(std::memory_order_relaxed),
          read_gauge(c.active_connections), read_gauge(c.active_streams)};
  for (size_t i = 0; i < kLatencyBuckets; ++i)
    out.latency[i] = c.latency[i].load(std::memory_order_relaxed);
  return out;
}

void record_handshake_failure(monitor::Transport transport) noexcept {
  if (auto* c = find(transport)) c->handshake_failures.fetch_add(1, std::memory_order_relaxed);
}

void record_throttled(monitor::Transport transport) noexcept {
  if (auto* c = find(transport)) c->throttled.fetch_add(1, std::memory_order_relaxed);
}

RouterStatsFile read_router_stats(const std::string& path) {
  // Far past what the router writes. A snapshot has to stay small.
  static constexpr uintmax_t kMaxBytes = 1024 * 1024;
  namespace fs = std::filesystem;
  std::error_code ec;
  const auto size = fs::file_size(path, ec);
  if (ec || size > kMaxBytes) return {};
  const auto modified = fs::last_write_time(path, ec);
  if (ec) return {};
  std::ifstream file(path, std::ios::binary);
  if (!file) return {};
  std::ostringstream content;
  content << file.rdbuf();
  const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(fs::file_time_type::clock::now() - modified).count();
  return {std::move(content).str(), age > 0 ? static_cast<uint64_t>(age) : 0};
}

void record_http1_status(unsigned status) noexcept { record_status(http1_status, status); }
void record_http3_status(unsigned status) noexcept { record_status(http3_status, status); }
monitor::HttpStatusCounts read_http1_status() noexcept { return read_status(http1_status); }
monitor::HttpStatusCounts read_http3_status() noexcept { return read_status(http3_status); }

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

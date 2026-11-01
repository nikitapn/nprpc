#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/impl/telemetry.hpp>
#include <nprpc_monitor.hpp>
#include <algorithm>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace nprpc::impl {
namespace {
using Clock = std::chrono::steady_clock;

constexpr uint32_t kMaxWatches = 32;
constexpr uint32_t kMaxWatchesPerSession = 4;

// Each Watch costs a writer, timers and periodic sampling for as long as the
// peer keeps it open.
class WatchLimits {
  std::mutex mutex_;
  uint32_t total_ = 0;
  std::unordered_map<const SessionContext*, uint32_t> per_session_;
public:
  bool acquire(const SessionContext* session)
  {
    std::lock_guard lock(mutex_);
    auto& count = per_session_[session];
    if (total_ >= kMaxWatches || count >= kMaxWatchesPerSession) {
      if (count == 0) per_session_.erase(session);
      return false;
    }
    ++count;
    ++total_;
    return true;
  }
  void release(const SessionContext* session)
  {
    std::lock_guard lock(mutex_);
    --total_;
    auto it = per_session_.find(session);
    if (--it->second == 0) per_session_.erase(it);
  }
};

// Held by the stream's frame, so it is released however the stream ends.
class WatchSlot {
  std::shared_ptr<WatchLimits> limits_;
  const SessionContext* session_;
public:
  WatchSlot(std::shared_ptr<WatchLimits> limits, const SessionContext* session)
    : limits_(std::move(limits)), session_(session) {}
  WatchSlot(WatchSlot&&) = default;
  ~WatchSlot() { if (limits_) limits_->release(session_); }
};

// The stream frame owns its start time rather than a pointer to the servant.
StreamWriter<monitor::Snapshot> samples(Clock::time_point started, std::string router_stats_file, WatchSlot)
{
  for (;;) {
    monitor::Snapshot snapshot;
    snapshot.uptime_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        Clock::now() - started).count();
    for (auto transport : kTelemetryTransports) {
      const auto c = read_telemetry(transport);
      snapshot.transports.push_back({transport, c.completed, c.errors,
          c.received_bytes, c.sent_bytes, c.dispatch_ns,
          c.handshake_failures, c.throttled, c.active_connections, c.active_streams,
          {c.latency.begin(), c.latency.end()}});
    }
    snapshot.http1_handshake_failures = read_http1_handshake_failures();
    snapshot.http1_throttled = read_http1_throttled();
    snapshot.http1_status = read_http1_status();
    snapshot.http3_status = read_http3_status();
    if (!router_stats_file.empty()) {
      auto router = read_router_stats(router_stats_file);
      snapshot.router_stats_json = std::move(router.json);
      snapshot.router_stats_age_ms = router.age_ms;
    }
    co_yield snapshot;
  }
}

class TelemetryService final : public monitor::IMonitor_Servant {
  const Clock::time_point started_ = Clock::now();
  const uint32_t min_interval_ms_;
  const std::string router_stats_file_;
  const std::shared_ptr<WatchLimits> limits_ = std::make_shared<WatchLimits>();
public:
  TelemetryService(uint32_t min_interval_ms, std::string router_stats_file)
    : min_interval_ms_(min_interval_ms), router_stats_file_(std::move(router_stats_file)) {}
  void dispatch(SessionContext& ctx, bool from_parent) override
  {
    // Activation flags advertise endpoints; enforce the management boundary
    // here too, including calls made over another listener or a callback session.
    if (ctx.remote_endpoint.type() != EndPointType::Quic ||
        (!g_cfg.quic_client_ca_file.empty() && !ctx.quic_client_authenticated)) {
      make_simple_answer(ctx, MessageId::Error_BadAccess);
      return;
    }
    monitor::IMonitor_Servant::dispatch(ctx, from_parent);
  }

  StreamWriter<monitor::Snapshot> Watch(uint32_t interval_ms) override
  {
    auto& ctx = get_context();
    if (!limits_->acquire(&ctx)) {
      make_simple_answer(ctx, MessageId::Error_BadAccess);
      throw Exception("Too many telemetry watches");
    }
    auto writer = samples(started_, router_stats_file_, WatchSlot(limits_, &ctx));
    writer.set_interval(std::chrono::milliseconds(
        std::clamp(interval_ms, min_interval_ms_, uint32_t{10000})));
    return writer;
  }
};
}

void RpcImpl::start_telemetry(uint32_t min_interval_ms, std::string router_stats_file)
{
  auto* poa = create_poa().with_max_objects(1)
      .with_lifespan(PoaPolicy::Lifespan::Persistent).build();
  auto service = std::make_unique<TelemetryService>(min_interval_ms, std::move(router_stats_file));
  const auto id = poa->activate_object(service.get(), ObjectActivationFlags::quic);
  service.release(); // the persistent POA owns the activated servant
  telemetry_reference_ = id.to_string();
  g_telemetry_enabled.store(true, std::memory_order_relaxed);
}
}

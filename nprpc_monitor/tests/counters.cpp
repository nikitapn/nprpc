#ifdef NDEBUG
#undef NDEBUG
#endif
#include <nprpc/impl/telemetry.hpp>
#include <cassert>
#include <thread>
#include <vector>
using nprpc::impl::kTelemetryTransports;
int main() {
  assert(!nprpc::impl::telemetry_enabled());
  std::vector<std::thread> workers;
  for (unsigned t = 0; t < 8; ++t) workers.emplace_back([] {
    for (unsigned i = 0; i < 10000; ++i)
      nprpc::impl::record_telemetry(kTelemetryTransports[i % kTelemetryTransports.size()], 10, 20, 30, i % 2 == 0);
  });
  for (auto& worker : workers) worker.join();
  uint64_t total = 0, errors = 0;
  for (auto t : kTelemetryTransports) {
    auto v = nprpc::impl::read_telemetry(t);
    total += v.completed;
    errors += v.errors;
    assert(v.received_bytes == v.completed * 10);
    assert(v.sent_bytes == v.completed * 20);
    assert(v.dispatch_ns == v.completed * 30);
  }
  assert(total == 80000 && errors == 40000);
  {
    constexpr auto t = nprpc::monitor::Transport::Quic;
    nprpc::impl::record_handshake_failure(t);
    nprpc::impl::record_throttled(t);
    nprpc::impl::record_throttled(t);
    nprpc::impl::record_http1_handshake_failure();
    nprpc::impl::record_http1_throttled();
    {
      nprpc::impl::ActiveConnection connection(t);
      nprpc::impl::active_stream_counter(t)->fetch_add(2);
      auto v = nprpc::impl::read_telemetry(t);
      assert(v.handshake_failures == 1 && v.throttled == 2);
      assert(v.active_connections == 1 && v.active_streams == 2);
      assert(nprpc::impl::read_telemetry(nprpc::monitor::Transport::Http3).throttled == 0);
    }
    nprpc::impl::active_stream_counter(t)->fetch_sub(3);
    auto v = nprpc::impl::read_telemetry(t);
    assert(v.active_connections == 0);
    assert(v.active_streams == 0); // clamped, though the counter is -1
    assert(nprpc::impl::read_http1_handshake_failures() == 1 && nprpc::impl::read_http1_throttled() == 1);
  }
  assert(nprpc::impl::read_telemetry(static_cast<nprpc::monitor::Transport>(99)).completed == 0);
}

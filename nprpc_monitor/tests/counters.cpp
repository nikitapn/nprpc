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
  assert(nprpc::impl::read_telemetry(static_cast<nprpc::monitor::Transport>(99)).completed == 0);
}

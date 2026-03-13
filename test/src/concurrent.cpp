// ============================================================================
// High-Concurrency Load Test
//
// Spawns N concurrent threads, each driving its own resolved proxy against a
// shared servant. Measures end-to-end call latency and reports min/avg/p50/
// p99/max as well as total throughput (calls/sec). Failures are counted and
// reported — any failure causes the test to fail after all threads finish so
// the full metrics picture is visible even in the presence of errors.
//
// Two workloads per transport:
//   Workload A – small round-trip:  ReturnU32()         (no payload)
//   Workload B – mixed payload:     In_(payload var.)   (1–64 kB random size)
// ============================================================================

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <sys/wait.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/stream_reader.hpp>
#include <nprpc/stream_writer.hpp>
#include <nprpc_nameserver.hpp>
#include <nprpc_test.hpp>

#include <boost/range/algorithm_ext/push_back.hpp>
#include <boost/range/irange.hpp>

#include "common/helper.inl"


namespace {

struct LatencyStats {
  std::vector<double> samples_us; // raw per-call latency in microseconds

  void record(std::chrono::nanoseconds elapsed) {
    samples_us.push_back(
        static_cast<double>(elapsed.count()) / 1000.0);
  }

  void merge(LatencyStats&& other) {
    samples_us.insert(samples_us.end(),
                      std::make_move_iterator(other.samples_us.begin()),
                      std::make_move_iterator(other.samples_us.end()));
  }

  void print(const std::string& label) const {
    if (samples_us.empty()) {
      std::cout << label << ": no samples\n";
      return;
    }
    auto sorted = samples_us;
    std::sort(sorted.begin(), sorted.end());
    double sum = std::accumulate(sorted.begin(), sorted.end(), 0.0);
    double avg = sum / static_cast<double>(sorted.size());
    double p50 = sorted[sorted.size() * 50 / 100];
    double p99 = sorted[sorted.size() * 99 / 100];
    std::cout << label
              << "  n=" << sorted.size()
              << "  min=" << sorted.front() << " µs"
              << "  avg=" << avg << " µs"
              << "  p50=" << p50 << " µs"
              << "  p99=" << p99 << " µs"
              << "  max=" << sorted.back() << " µs\n";
  }
};

} // anonymous namespace

namespace nprpctest {

// Parameters – keep defaults conservative enough for CI; tune via environment
// variables or rebuild constants for local load testing.
static constexpr int    kLoadConcurrency  = 64;   // parallel worker threads
static constexpr int    kCallsPerWorker   = 128;  // calls each worker makes
static constexpr size_t kSmallPayloadSize = 64;   // bytes for workload A
static constexpr size_t kMaxPayloadSize   = 64 * 1024; // max for workload B

TEST_F(NprpcTest, HighConcurrencyLoad)
{
  Test::RecordProperty("timeout", "300");

  // Suppress trace-level logging for the duration of this test.
  // The single ASIO-io_context thread writes to std::clog under a mutex on
  // every call; with 64 concurrent workers the pipe fills and blocks the ASIO
  // thread, deadlocking all waiting workers.
  auto* saved_rdbuf = std::clog.rdbuf(nullptr);
  auto restore_clog = [&]() noexcept { std::clog.rdbuf(saved_rdbuf); };

#include "common/tests/basic.inl"
  TestBasicImpl servant;

  auto run_transport = [&](nprpc::ObjectActivationFlags flags,
                           const std::string& transport_name)
  {
    // -----------------------------------------------------------------------
    // Workload A: small round-trip – ReturnU32()
    // -----------------------------------------------------------------------
    {
      auto obj = bind_and_resolve<nprpc::test::TestBasic>(
          servant, flags, "load_test_small");
      obj->set_timeout(10000);

      std::atomic<int> failures{0};
      std::vector<LatencyStats> per_thread_stats(kLoadConcurrency);
      std::vector<std::thread> workers;
      workers.reserve(kLoadConcurrency);

      auto wall_start = std::chrono::steady_clock::now();

      for (int t = 0; t < kLoadConcurrency; ++t) {
        workers.emplace_back([&, t]() {
          auto& stats = per_thread_stats[t];
          stats.samples_us.reserve(kCallsPerWorker);
          // Each thread resolves its own proxy to stress connection handling.
          nprpc::Object* raw_obj = nullptr;
          auto nameserver = rpc->get_nameserver("127.0.0.1");
          if (!nameserver->Resolve("load_test_small", raw_obj) || !raw_obj) {
            ++failures;
            return;
          }
          nprpc::ObjectPtr<nprpc::test::TestBasic> local_obj(
              nprpc::narrow<nprpc::test::TestBasic>(raw_obj));
          if (!local_obj) { ++failures; return; }
          local_obj->set_timeout(10000);

          for (int i = 0; i < kCallsPerWorker; ++i) {
            auto t0 = std::chrono::steady_clock::now();
            try {
              uint32_t v = local_obj->ReturnU32();
              if (v != 42u) ++failures;
            } catch (...) {
              ++failures;
            }
            stats.record(std::chrono::steady_clock::now() - t0);
          }
        });
      }

      for (auto& w : workers) w.join();

      auto wall_elapsed = std::chrono::steady_clock::now() - wall_start;
      double wall_sec = std::chrono::duration<double>(wall_elapsed).count();
      int total_calls = kLoadConcurrency * kCallsPerWorker;
      double throughput = total_calls / wall_sec;

      LatencyStats merged;
      merged.samples_us.reserve(total_calls);
      for (auto& s : per_thread_stats) merged.merge(std::move(s));

      std::cout << "\n[Load/" << transport_name << "/SmallRPC] "
                << "threads=" << kLoadConcurrency
                << " calls_per_thread=" << kCallsPerWorker
                << " total=" << total_calls
                << " failures=" << failures.load()
                << " throughput=" << throughput << " calls/s\n";
      merged.print("[Load/" + transport_name + "/SmallRPC/latency]");

      EXPECT_EQ(failures.load(), 0)
          << transport_name << " small-RPC workload had failures";
    }

    // -----------------------------------------------------------------------
    // Workload B: mixed payload sizes – In_()
    // -----------------------------------------------------------------------
    {
      auto obj = bind_and_resolve<nprpc::test::TestBasic>(
          servant, flags, "load_test_mixed");
      obj->set_timeout(10000);

      std::atomic<int> failures{0};
      std::vector<LatencyStats> per_thread_stats(kLoadConcurrency);
      std::vector<std::thread> workers;
      workers.reserve(kLoadConcurrency);

      auto wall_start = std::chrono::steady_clock::now();

      for (int t = 0; t < kLoadConcurrency; ++t) {
        workers.emplace_back([&, t]() {
          auto& stats = per_thread_stats[t];
          stats.samples_us.reserve(kCallsPerWorker);
          nprpc::Object* raw_obj = nullptr;
          auto nameserver = rpc->get_nameserver("127.0.0.1");
          if (!nameserver->Resolve("load_test_mixed", raw_obj) || !raw_obj) {
            ++failures;
            return;
          }
          nprpc::ObjectPtr<nprpc::test::TestBasic> local_obj(
              nprpc::narrow<nprpc::test::TestBasic>(raw_obj));
          if (!local_obj) { ++failures; return; }
          local_obj->set_timeout(10000);

          // Deterministic pseudo-random size ladder per thread
          size_t payload_size = kSmallPayloadSize + static_cast<size_t>(t) * 1024;
          if (payload_size > kMaxPayloadSize) payload_size = kSmallPayloadSize;

          std::vector<uint8_t> payload(payload_size);
          std::iota(payload.begin(), payload.end(), 0);

          for (int i = 0; i < kCallsPerWorker; ++i) {
            // Vary size each call: small → medium → large → small …
            size_t sz = kSmallPayloadSize
                << (static_cast<unsigned>(i) % 10); // 64 B … 32 kB
            if (sz > kMaxPayloadSize) sz = kSmallPayloadSize;
            if (sz > payload.size()) sz = payload.size();

            auto t0 = std::chrono::steady_clock::now();
            try {
              // In_ expects a=100, b=true, c=iota vector
              std::vector<uint8_t> buf(sz);
              std::iota(buf.begin(), buf.end(), 0);
              bool ok = local_obj->In_(100, true,
                            nprpc::flat::make_read_only_span(buf));
              if (!ok) ++failures;
            } catch (...) {
              ++failures;
            }
            stats.record(std::chrono::steady_clock::now() - t0);
          }
        });
      }

      for (auto& w : workers) w.join();

      auto wall_elapsed = std::chrono::steady_clock::now() - wall_start;
      double wall_sec = std::chrono::duration<double>(wall_elapsed).count();
      int total_calls = kLoadConcurrency * kCallsPerWorker;
      double throughput = total_calls / wall_sec;

      LatencyStats merged;
      merged.samples_us.reserve(total_calls);
      for (auto& s : per_thread_stats) merged.merge(std::move(s));

      std::cout << "\n[Load/" << transport_name << "/MixedPayload] "
                << "threads=" << kLoadConcurrency
                << " calls_per_thread=" << kCallsPerWorker
                << " total=" << total_calls
                << " failures=" << failures.load()
                << " throughput=" << throughput << " calls/s\n";
      merged.print("[Load/" + transport_name + "/MixedPayload/latency]");

      EXPECT_EQ(failures.load(), 0)
          << transport_name << " mixed-payload workload had failures";
    }
  };

  run_transport(nprpc::ObjectActivationFlags::tcp,       "TCP");
  run_transport(nprpc::ObjectActivationFlags::ws, "WS");

  restore_clog(); // restore before final metric output
}

} // namespace nprpctest
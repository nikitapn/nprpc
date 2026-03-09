// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// NPRPC vs gRPC Concurrent Throughput Benchmarks
//
// Measures throughput and latency under N simultaneous client threads all
// hammering the same servant/server.  Each iteration of the Google Benchmark
// loop launches N threads, fires K calls per thread, joins, and reports:
//   - total throughput (calls/sec)   via state.counters["calls/sec"]
//   - average per-call latency (µs)  via state.counters["avg_latency_us"]
//   - p99 per-call latency (µs)      via state.counters["p99_latency_us"]
//
// The range(0) argument selects the thread count [4, 8, 16, 32, 64].
// The range(1) argument for NPRPC selects the transport (same indices as
// LatencyFixture: 0=SharedMemory, 1=TCP, 2=WebSocket).

#include "common.hpp"

#include <benchmark/benchmark.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <numeric>
#include <thread>
#include <vector>

// ============================================================================
// Shared helpers
// ============================================================================

namespace {

// Per-thread latency samples collected during one benchmark iteration.
// We accumulate them then merge once threads are joined.
struct LatencySamples {
  std::vector<double> us; // microseconds per call
};

double percentile(std::vector<double>& sorted, double p)
{
  if (sorted.empty()) return 0.0;
  size_t idx = static_cast<size_t>(p / 100.0 * sorted.size());
  if (idx >= sorted.size()) idx = sorted.size() - 1;
  return sorted[idx];
}

} // anonymous namespace

// ============================================================================
// NPRPC concurrent fixture
// ============================================================================

#include <nprpc/nprpc.hpp>
#include <nprpc_benchmark.hpp>

namespace {

class NprpcConcurrentFixture : public ::benchmark::Fixture
{
  static constexpr int kCallsPerWorker = 64;

  enum class TransportType { SharedMemory = 0, TCP = 1, WebSocket = 2 };

  TransportType transport_;

  nprpc::ObjectPtr<nprpc::benchmark::Benchmark> master_proxy_;

  void select_transport(nprpc::ObjectPtr<nprpc::benchmark::Benchmark>& proxy)
  {
    std::string prefix;
    switch (transport_) {
    case TransportType::SharedMemory: prefix = "mem://"; break;
    case TransportType::TCP:          prefix = "tcp://"; break;
    case TransportType::WebSocket:    prefix = "ws://";  break;
    }
    auto& urls = proxy->get_data().urls;
    auto a = urls.find(prefix);
    auto b = urls.find(';', a);
    urls = urls.substr(a, b - a);
    proxy->select_endpoint();
  }

public:
  void SetUp(const ::benchmark::State& state) override
  {
    transport_ = static_cast<TransportType>(state.range(1));
    master_proxy_ =
        get_object<nprpc::benchmark::Benchmark>("nprpc_benchmark");
    select_transport(master_proxy_);
  }

  void TearDown(const ::benchmark::State& /* state */) override
  {
    master_proxy_.reset();
  }

  void RunConcurrentPing(benchmark::State& state)
  {
    const int n_threads = static_cast<int>(state.range(0));

    for (auto _ : state) {
      std::vector<LatencySamples> per_thread(n_threads);
      std::vector<std::thread> workers;
      workers.reserve(n_threads);
      std::atomic<int> failures{0};

      auto wall_start = std::chrono::steady_clock::now();

      for (int t = 0; t < n_threads; ++t) {
        workers.emplace_back([&, t]() {
          // Each worker resolves its own proxy (tests connection handling)
          auto proxy =
              get_object<nprpc::benchmark::Benchmark>("nprpc_benchmark");
          select_transport(proxy);
          proxy->set_timeout(10000);

          auto& samples = per_thread[t].us;
          samples.reserve(kCallsPerWorker);

          for (int i = 0; i < kCallsPerWorker; ++i) {
            auto t0 = std::chrono::steady_clock::now();
            try {
              proxy->Ping();
            } catch (...) {
              ++failures;
            }
            auto dt = std::chrono::steady_clock::now() - t0;
            samples.push_back(
                static_cast<double>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(dt)
                        .count()) /
                1000.0);
          }
        });
      }

      for (auto& w : workers) w.join();

      auto wall_elapsed = std::chrono::steady_clock::now() - wall_start;
      double wall_sec =
          std::chrono::duration<double>(wall_elapsed).count();

      // Merge all latency samples
      std::vector<double> all;
      all.reserve(n_threads * kCallsPerWorker);
      for (auto& s : per_thread)
        all.insert(all.end(), s.us.begin(), s.us.end());

      std::sort(all.begin(), all.end());
      double sum   = std::accumulate(all.begin(), all.end(), 0.0);
      double avg   = all.empty() ? 0.0 : sum / all.size();
      double p99   = percentile(all, 99.0);

      state.counters["threads"]         = n_threads;
      state.counters["calls/sec"]       =
          benchmark::Counter(static_cast<double>(all.size()) / wall_sec,
                             benchmark::Counter::kDefaults);
      state.counters["avg_latency_us"]  = avg;
      state.counters["p99_latency_us"]  = p99;
      state.counters["failures"]        =
          static_cast<double>(failures.load());
    }

    const char* transport_names[] = {"SharedMemory", "TCP", "WebSocket"};
    state.SetLabel(transport_names[static_cast<int>(transport_)]);
  }
};

} // anonymous namespace

BENCHMARK_DEFINE_F(NprpcConcurrentFixture, ConcurrentPing)
(benchmark::State& state)
{
  RunConcurrentPing(state);
}

// Transport × thread-count matrix
BENCHMARK_REGISTER_F(NprpcConcurrentFixture, ConcurrentPing)
    ->UseRealTime()
    ->Unit(benchmark::kMillisecond)
    ->Args({4,  0})->Args({8,  0})->Args({16, 0})->Args({32, 0})->Args({64, 0})  // SharedMemory
    ->Args({4,  1})->Args({8,  1})->Args({16, 1})->Args({32, 1})->Args({64, 1})  // TCP
    ->Args({4,  2})->Args({8,  2})->Args({16, 2})->Args({32, 2})->Args({64, 2}); // WebSocket

// ============================================================================
// gRPC concurrent fixture (compiled only when gRPC is available)
// ============================================================================

#ifdef NPRPC_GRPC_BENCHMARK_ENABLED

#include "benchmark.grpc.pb.h"
#include <grpcpp/grpcpp.h>

namespace {

class GrpcConcurrentFixture : public ::benchmark::Fixture
{
  static constexpr int kCallsPerWorker = 64;

public:
  void SetUp(const ::benchmark::State& /* state */) override {}
  void TearDown(const ::benchmark::State& /* state */) override {}

  void RunConcurrentPing(benchmark::State& state)
  {
    const int n_threads = static_cast<int>(state.range(0));

    for (auto _ : state) {
      std::vector<LatencySamples> per_thread(n_threads);
      std::vector<std::thread> workers;
      workers.reserve(n_threads);
      std::atomic<int> failures{0};

      auto wall_start = std::chrono::steady_clock::now();

      for (int t = 0; t < n_threads; ++t) {
        workers.emplace_back([&, t]() {
          // Each worker gets its own channel+stub to stress connection handling
          grpc::ChannelArguments args;
          auto channel = grpc::CreateCustomChannel(
              "localhost:50051",
              grpc::InsecureChannelCredentials(),
              args);
          auto stub = grpc::benchmark::Benchmark::NewStub(channel);

          auto& samples = per_thread[t].us;
          samples.reserve(kCallsPerWorker);

          grpc::benchmark::PingRequest  request;
          grpc::benchmark::PingResponse response;

          for (int i = 0; i < kCallsPerWorker; ++i) {
            auto t0 = std::chrono::steady_clock::now();
            grpc::ClientContext ctx;
            auto status = stub->Ping(&ctx, request, &response);
            if (!status.ok()) ++failures;
            auto dt = std::chrono::steady_clock::now() - t0;
            samples.push_back(
                static_cast<double>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(dt)
                        .count()) /
                1000.0);
          }
        });
      }

      for (auto& w : workers) w.join();

      auto wall_elapsed = std::chrono::steady_clock::now() - wall_start;
      double wall_sec =
          std::chrono::duration<double>(wall_elapsed).count();

      std::vector<double> all;
      all.reserve(n_threads * kCallsPerWorker);
      for (auto& s : per_thread)
        all.insert(all.end(), s.us.begin(), s.us.end());

      std::sort(all.begin(), all.end());
      double sum  = std::accumulate(all.begin(), all.end(), 0.0);
      double avg  = all.empty() ? 0.0 : sum / all.size();
      double p99  = percentile(all, 99.0);

      state.counters["threads"]         = n_threads;
      state.counters["calls/sec"]       =
          benchmark::Counter(static_cast<double>(all.size()) / wall_sec,
                             benchmark::Counter::kDefaults);
      state.counters["avg_latency_us"]  = avg;
      state.counters["p99_latency_us"]  = p99;
      state.counters["failures"]        =
          static_cast<double>(failures.load());
    }

    state.SetLabel("gRPC");
  }
};

} // anonymous namespace

BENCHMARK_DEFINE_F(GrpcConcurrentFixture, ConcurrentPing)
(benchmark::State& state)
{
  RunConcurrentPing(state);
}

BENCHMARK_REGISTER_F(GrpcConcurrentFixture, ConcurrentPing)
    ->UseRealTime()
    ->Unit(benchmark::kMillisecond)
    ->Arg(4)->Arg(8)->Arg(16)->Arg(32)->Arg(64);

#endif // NPRPC_GRPC_BENCHMARK_ENABLED

// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// NPRPC Latency Benchmarks - Round-trip time measurements

#include "common.hpp"

#include <benchmark/benchmark.h>
#include <thread>
#include <memory>

namespace {
// Shared benchmark fixture
class LatencyFixture : public ::benchmark::Fixture {
protected:
  enum class TransportType {
    SharedMemory = 0,
    TCP = 1,
    WebSocket = 2,
  };

  TransportType transport_;
  nprpc::ObjectPtr<nprpc::benchmark::Benchmark> proxy_;

  std::string GetEndpoint(TransportType type) {
    switch (type) {
      case TransportType::SharedMemory:
        return "mem://";
      case TransportType::TCP:
        return "tcp://";
      case TransportType::WebSocket:
        return "ws://";
      default:
        assert(false);
        return "";
    }
  }

  const char* GetTransportName(TransportType type) {
    switch (type) {
      case TransportType::SharedMemory: return "SharedMemory";
      case TransportType::TCP: return "TCP";
      case TransportType::WebSocket: return "WebSocket";
      default: assert(false); return "Unknown";
    }
  }
public:
  void SetUp(const ::benchmark::State& state) override {
    // Get transport type from state range
    transport_ = static_cast<TransportType>(state.range(0));
    proxy_ = get_object<nprpc::benchmark::Benchmark>("nprpc_benchmark");

    auto endpoint = GetEndpoint(transport_);

    auto& urls = proxy_->get_data().urls;

    auto a = urls.find(endpoint);
    auto b = urls.find(";", a);
    urls = urls.substr(a, b - a);

    proxy_->select_endpoint();
  }

  void TearDown(const ::benchmark::State& /* state */) override {}
};

} // anonymous namespace

// Benchmark: Simple function call latency (no payload)
BENCHMARK_DEFINE_F(LatencyFixture, EmptyCall)(benchmark::State& state) {
  for (auto _ : state) {
    proxy_->Ping();
  }

  state.SetLabel(GetTransportName(transport_));
  state.counters["calls/sec"] = benchmark::Counter(
    state.iterations(), 
    benchmark::Counter::kIsRate
  );
}

// Benchmark: Function call with return value
BENCHMARK_DEFINE_F(LatencyFixture, CallWithReturn)(benchmark::State& state) {
  for (auto _ : state) {
    uint32_t result = proxy_->Func1(42, 24);
    benchmark::DoNotOptimize(result);
  }

  state.SetLabel(GetTransportName(transport_));
  state.counters["calls/sec"] = benchmark::Counter(
    state.iterations(), 
    benchmark::Counter::kIsRate
  );
}

// Benchmark: Small string payload
BENCHMARK_DEFINE_F(LatencyFixture, SmallStringCall)(benchmark::State& state) {
  std::string str(100, 'x'); // 100 bytes

  for (auto _ : state) {
    proxy_->Func2(str);
  }

  state.SetLabel(GetTransportName(transport_));
  state.SetBytesProcessed(state.iterations() * str.size());
  state.counters["calls/sec"] = benchmark::Counter(
    state.iterations(), 
    benchmark::Counter::kIsRate
  );
}

// Benchmark: Complex nested data structure
BENCHMARK_DEFINE_F(LatencyFixture, NestedDataCall)(benchmark::State& state) {
  // Create a complex nested employee object
  nprpc::benchmark::Employee employee;
  employee.person.name = "John Doe";
  employee.person.age = 30;
  employee.address.street = "123 Main St";
  employee.address.city = "Springfield";
  employee.address.zipCode = 12345;

  employee.employeeId = 1001;
  employee.salary = 75000.50;
  
  auto& skills = employee.skills;
  skills.resize(5);
  skills[0] = "C++";
  skills[1] = "Python";
  skills[2] = "JavaScript";
  skills[3] = "Rust";
  skills[4] = "Go";
  
  for (auto _ : state) {
    auto result = proxy_->ProcessEmployee(employee);
    benchmark::DoNotOptimize(result);
  }
}

// Benchmark: Large data payload (1 MB)
BENCHMARK_DEFINE_F(LatencyFixture, LargeData1MB)(benchmark::State& state) {
  std::vector<uint8_t> data(1024 * 1024); // 1 MB
  std::fill(data.begin(), data.end(), 0x42);

  for (auto _ : state) {
    auto result = proxy_->ProcessLargeData({data.data(), data.data() + data.size()});
    benchmark::DoNotOptimize(result);
  }

  state.SetLabel(GetTransportName(transport_));
  state.SetBytesProcessed(state.iterations() * data.size());
  state.counters["calls/sec"] = benchmark::Counter(
    state.iterations(), 
    benchmark::Counter::kIsRate
  );
}

// Benchmark: Very large data payload (10 MB)
BENCHMARK_DEFINE_F(LatencyFixture, LargeData10MB)(benchmark::State& state) {
  std::vector<uint8_t> data(10 * 1024 * 1024); // 10 MB
  std::fill(data.begin(), data.end(), 0x42);

  for (auto _ : state) {
    auto result = proxy_->ProcessLargeData({data.data(), data.data() + data.size()});
    benchmark::DoNotOptimize(result);
  }

  state.SetLabel(GetTransportName(transport_));
  state.SetBytesProcessed(state.iterations() * data.size());
  state.counters["calls/sec"] = benchmark::Counter(
    state.iterations(), 
    benchmark::Counter::kIsRate
  );
}

// Register benchmarks for each transport
// Arg(0) = SharedMemory, Arg(1) = TCP, Arg(2) = WebSocket

BENCHMARK_REGISTER_F(LatencyFixture, EmptyCall)
  ->Arg(0)  // SharedMemory
  ->Arg(1)  // TCP
  ->Arg(2)  // WebSocket
  ->Unit(benchmark::kMicrosecond);

BENCHMARK_REGISTER_F(LatencyFixture, CallWithReturn)
  ->Arg(0)
  ->Arg(1)
  ->Arg(2)
  ->Unit(benchmark::kMicrosecond);

BENCHMARK_REGISTER_F(LatencyFixture, SmallStringCall)
  ->Arg(0)
  ->Arg(1)
  ->Arg(2)
  ->Unit(benchmark::kMicrosecond);

BENCHMARK_REGISTER_F(LatencyFixture, NestedDataCall)
  ->Arg(0)
  ->Arg(1)
  ->Arg(2)
  ->Unit(benchmark::kMicrosecond);

BENCHMARK_REGISTER_F(LatencyFixture, LargeData1MB)
  ->Arg(0)
  ->Arg(1)
  ->Arg(2)
  ->Unit(benchmark::kMillisecond);

BENCHMARK_REGISTER_F(LatencyFixture, LargeData10MB)
  ->Arg(0)
  ->Arg(1)
  ->Arg(2)
  ->Unit(benchmark::kMillisecond);


/* RESULTS:
--------------------------------------------------------------------------------------
Benchmark                                 Time             CPU   Iterations  calls/sec
--------------------------------------------------------------------------------------
LatencyFixture/EmptyCall/0              132 us         17.0 us        41528  58.871k/s SharedMemory
LatencyFixture/EmptyCall/1              119 us         18.9 us        36365 52.8213k/s TCP
LatencyFixture/EmptyCall/2              123 us         20.0 us        35030 50.0187k/s WebSocket
LatencyFixture/CallWithReturn/0         130 us         17.0 us        40097 58.8606k/s SharedMemory
LatencyFixture/CallWithReturn/1         112 us         18.0 us        37362 55.7027k/s TCP
LatencyFixture/CallWithReturn/2         116 us         18.9 us        37664 52.9795k/s WebSocket
-------------------------------------------------------------------------------------------------------
Benchmark                                 Time             CPU   Iterations bytes_per_second  calls/sec
-------------------------------------------------------------------------------------------------------
LatencyFixture/SmallStringCall/0        125 us         16.5 us        38706       5.7919Mi/s 60.7325k/s SharedMemory
LatencyFixture/SmallStringCall/1        112 us         18.0 us        38649      5.31255Mi/s 55.7061k/s TCP
LatencyFixture/SmallStringCall/2        116 us         19.1 us        37101      5.00113Mi/s 52.4407k/s WebSocket
*/
// Cap'n Proto latency benchmarks
#include <benchmark/benchmark.h>
#include "benchmark.capnp.h"
#include <capnp/ez-rpc.h>
#include <kj/debug.h>
#include <string>

static constexpr const char* CAPNP_SERVER_ADDR = "localhost:50052";

class CapnpLatencyFixture : public benchmark::Fixture {
public:
  CapnpLatencyFixture() = default;
  virtual ~CapnpLatencyFixture() noexcept = default;

  void SetUp(const ::benchmark::State& state) override {
    // Connect to Cap'n Proto server
    client = kj::heap<capnp::EzRpcClient>(CAPNP_SERVER_ADDR);
    
    // Get the capability
    capability = client->getMain<BenchmarkService>();
  }

  void TearDown(const ::benchmark::State& state) override {
    capability = BenchmarkService::Client(nullptr);
    client = nullptr;
  }

protected:
  kj::Own<capnp::EzRpcClient> client;
  BenchmarkService::Client capability{nullptr};
};

BENCHMARK_DEFINE_F(CapnpLatencyFixture, EmptyCall)(benchmark::State& state) {
  auto& waitScope = client->getWaitScope();
  
  for (auto _ : state) {
    auto request = capability.emptyCallRequest();
    auto promise = request.send();
    promise.wait(waitScope);
  }
  
  state.counters["calls/sec"] = benchmark::Counter(
    state.iterations(), benchmark::Counter::kIsRate);
  state.SetLabel("Cap'n Proto");
}

BENCHMARK_DEFINE_F(CapnpLatencyFixture, CallWithReturn)(benchmark::State& state) {
  auto& waitScope = client->getWaitScope();
  
  for (auto _ : state) {
    auto request = capability.callWithReturnRequest();
    auto promise = request.send();
    auto response = promise.wait(waitScope);
    benchmark::DoNotOptimize(response.getResult());
  }
  
  state.counters["calls/sec"] = benchmark::Counter(
    state.iterations(), benchmark::Counter::kIsRate);
  state.SetLabel("Cap'n Proto");
}

BENCHMARK_DEFINE_F(CapnpLatencyFixture, SmallStringCall)(benchmark::State& state) {
  auto& waitScope = client->getWaitScope();
  const std::string testData = "Hello, Cap'n Proto!";
  
  for (auto _ : state) {
    auto request = capability.smallStringCallRequest();
    request.setData(testData);
    auto promise = request.send();
    auto response = promise.wait(waitScope);
    benchmark::DoNotOptimize(response.getResult());
  }
  
  state.counters["bytes_per_second"] = benchmark::Counter(
    state.iterations() * testData.size(), 
    benchmark::Counter::kIsRate);
  state.counters["calls/sec"] = benchmark::Counter(
    state.iterations(), benchmark::Counter::kIsRate);
  state.SetLabel("Cap'n Proto");
}

BENCHMARK_REGISTER_F(CapnpLatencyFixture, EmptyCall)->Unit(benchmark::kMicrosecond);
BENCHMARK_REGISTER_F(CapnpLatencyFixture, CallWithReturn)->Unit(benchmark::kMicrosecond);
BENCHMARK_REGISTER_F(CapnpLatencyFixture, SmallStringCall)->Unit(benchmark::kMicrosecond);

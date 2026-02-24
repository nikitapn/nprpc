// Cap'n Proto latency benchmarks
#include "benchmark.capnp.h"
#include <benchmark/benchmark.h>
#include <capnp/ez-rpc.h>
#include <kj/debug.h>
#include <string>

static constexpr const char* CAPNP_SERVER_ADDR = "localhost:50052";

class CapnpLatencyFixture : public benchmark::Fixture
{
public:
  CapnpLatencyFixture() = default;
  virtual ~CapnpLatencyFixture() noexcept = default;

  void SetUp(const ::benchmark::State& state) override
  {
    // Connect to Cap'n Proto server
    client = kj::heap<capnp::EzRpcClient>(CAPNP_SERVER_ADDR);

    // Get the capability
    capability = client->getMain<BenchmarkService>();
  }

  void TearDown(const ::benchmark::State& state) override
  {
    capability = BenchmarkService::Client(nullptr);
    client = nullptr;
  }

protected:
  kj::Own<capnp::EzRpcClient> client;
  BenchmarkService::Client capability{nullptr};
};

BENCHMARK_DEFINE_F(CapnpLatencyFixture, EmptyCall)(benchmark::State& state)
{
  auto& waitScope = client->getWaitScope();

  for (auto _ : state) {
    auto request = capability.emptyCallRequest();
    auto promise = request.send();
    promise.wait(waitScope);
  }

  state.counters["calls/sec"] =
      benchmark::Counter(state.iterations(), benchmark::Counter::kIsRate);
  state.SetLabel("Cap'n Proto");
}

BENCHMARK_DEFINE_F(CapnpLatencyFixture, CallWithReturn)(benchmark::State& state)
{
  auto& waitScope = client->getWaitScope();

  for (auto _ : state) {
    auto request = capability.callWithReturnRequest();
    auto promise = request.send();
    auto response = promise.wait(waitScope);
    benchmark::DoNotOptimize(response.getResult());
  }

  state.counters["calls/sec"] =
      benchmark::Counter(state.iterations(), benchmark::Counter::kIsRate);
  state.SetLabel("Cap'n Proto");
}

BENCHMARK_DEFINE_F(CapnpLatencyFixture,
                   SmallStringCall)(benchmark::State& state)
{
  auto& waitScope = client->getWaitScope();
  const std::string testData = "Hello, Cap'n Proto!";

  for (auto _ : state) {
    auto request = capability.smallStringCallRequest();
    request.setData(testData);
    auto promise = request.send();
    auto response = promise.wait(waitScope);
    benchmark::DoNotOptimize(response.getResult());
  }

  state.counters["bytes_per_second"] =
      benchmark::Counter(state.iterations() * testData.size(), benchmark::Counter::kIsRate);
  state.counters["calls/sec"] =
      benchmark::Counter(state.iterations(), benchmark::Counter::kIsRate);
  state.SetLabel("Cap'n Proto");
}

BENCHMARK_DEFINE_F(CapnpLatencyFixture, NestedDataCall)(benchmark::State& state)
{
  auto& waitScope = client->getWaitScope();

  for (auto _ : state) {
    auto request = capability.processEmployeeRequest();
    auto employee = request.initEmployee();

    auto person = employee.initPerson();
    person.setName("John Doe");
    person.setAge(35);
    person.setEmail("john.doe@example.com");

    auto address = employee.initAddress();
    address.setStreet("123 Main St");
    address.setCity("New York");
    address.setCountry("USA");
    address.setZipCode(10001);

    employee.setEmployeeId(987654321);
    employee.setSalary(125000.50);

    auto skills = employee.initSkills(5);
    skills.set(0, "C++");
    skills.set(1, "Python");
    skills.set(2, "JavaScript");
    skills.set(3, "TypeScript");
    skills.set(4, "Rust");

    auto promise = request.send();
    auto response = promise.wait(waitScope);
    benchmark::DoNotOptimize(response.getResult());
  }

  state.counters["calls/sec"] =
      benchmark::Counter(state.iterations(), benchmark::Counter::kIsRate);
  state.SetLabel("Cap'n Proto");
}

BENCHMARK_DEFINE_F(CapnpLatencyFixture, LargeData1MB)(benchmark::State& state)
{
  auto& waitScope = client->getWaitScope();
  std::vector<uint8_t> data(1024 * 1024, 0x42); // 1 MB

  for (auto _ : state) {
    auto request = capability.processLargeDataRequest();
    request.setData(kj::arrayPtr(data.data(), data.size()));
    auto promise = request.send();
    auto response = promise.wait(waitScope);
    benchmark::DoNotOptimize(response.getResult());
  }

  state.SetBytesProcessed(state.iterations() * data.size());
  state.counters["calls/sec"] =
      benchmark::Counter(state.iterations(), benchmark::Counter::kIsRate);
  state.SetLabel("Cap'n Proto");
}

BENCHMARK_DEFINE_F(CapnpLatencyFixture, LargeData10MB)(benchmark::State& state)
{
  auto& waitScope = client->getWaitScope();
  std::vector<uint8_t> data(10 * 1024 * 1024, 0x42); // 10 MB

  for (auto _ : state) {
    auto request = capability.processLargeDataRequest();
    request.setData(kj::arrayPtr(data.data(), data.size()));
    auto promise = request.send();
    auto response = promise.wait(waitScope);
    benchmark::DoNotOptimize(response.getResult());
  }

  state.SetBytesProcessed(state.iterations() * data.size());
  state.counters["calls/sec"] =
      benchmark::Counter(state.iterations(), benchmark::Counter::kIsRate);
  state.SetLabel("Cap'n Proto");
}

BENCHMARK_REGISTER_F(CapnpLatencyFixture, EmptyCall)
    ->UseRealTime()->Unit(benchmark::kMicrosecond);
BENCHMARK_REGISTER_F(CapnpLatencyFixture, CallWithReturn)
    ->UseRealTime()->Unit(benchmark::kMicrosecond);
BENCHMARK_REGISTER_F(CapnpLatencyFixture, SmallStringCall)
    ->UseRealTime()->Unit(benchmark::kMicrosecond);
BENCHMARK_REGISTER_F(CapnpLatencyFixture, NestedDataCall)
    ->UseRealTime()->Unit(benchmark::kMicrosecond);
BENCHMARK_REGISTER_F(CapnpLatencyFixture, LargeData1MB)
    ->UseRealTime()->Unit(benchmark::kMillisecond);
BENCHMARK_REGISTER_F(CapnpLatencyFixture, LargeData10MB)
    ->UseRealTime()->Unit(benchmark::kMillisecond);

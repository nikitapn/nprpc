// gRPC Latency Benchmarks - for comparison with NPRPC

#include <grpcpp/grpcpp.h>
#include "benchmark.grpc.pb.h"

#include <benchmark/benchmark.h>
#include <memory>

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

namespace {

class GrpcLatencyFixture : public ::benchmark::Fixture {
protected:
  std::shared_ptr<Channel> channel_;
  std::unique_ptr<grpc::benchmark::Benchmark::Stub> stub_;

public:
  void SetUp(const ::benchmark::State& state) override {
    // Connect to gRPC server
    channel_ = grpc::CreateChannel("localhost:50051", grpc::InsecureChannelCredentials());
    stub_ = grpc::benchmark::Benchmark::NewStub(channel_);
  }

  void TearDown(const ::benchmark::State& /* state */) override {}
};

} // anonymous namespace

// Benchmark: Simple function call latency (no payload)
BENCHMARK_DEFINE_F(GrpcLatencyFixture, EmptyCall)(benchmark::State& state) {
  grpc::benchmark::PingRequest request;
  grpc::benchmark::PingResponse response;
  
  for (auto _ : state) {
    ClientContext context;
    Status status = stub_->Ping(&context, request, &response);
    if (!status.ok()) {
      state.SkipWithError("RPC failed");
      break;
    }
  }

  state.SetLabel("gRPC");
  state.counters["calls/sec"] = benchmark::Counter(
    state.iterations(), 
    benchmark::Counter::kIsRate
  );
}

// Benchmark: Function call with return value
BENCHMARK_DEFINE_F(GrpcLatencyFixture, CallWithReturn)(benchmark::State& state) {
  grpc::benchmark::Func1Request request;
  request.set_a(42);
  request.set_b(24);
  grpc::benchmark::Func1Response response;
  
  for (auto _ : state) {
    ClientContext context;
    Status status = stub_->Func1(&context, request, &response);
    if (!status.ok()) {
      state.SkipWithError("RPC failed");
      break;
    }
    benchmark::DoNotOptimize(response.result());
  }

  state.SetLabel("gRPC");
  state.counters["calls/sec"] = benchmark::Counter(
    state.iterations(), 
    benchmark::Counter::kIsRate
  );
}

// Benchmark: Small string payload
BENCHMARK_DEFINE_F(GrpcLatencyFixture, SmallStringCall)(benchmark::State& state) {
  grpc::benchmark::Func2Request request;
  request.set_data(std::string(100, 'x'));
  grpc::benchmark::Func2Response response;
  
  for (auto _ : state) {
    ClientContext context;
    Status status = stub_->Func2(&context, request, &response);
    if (!status.ok()) {
      state.SkipWithError("RPC failed");
      break;
    }
  }

  state.SetLabel("gRPC");
  state.SetBytesProcessed(state.iterations() * 100);
  state.counters["calls/sec"] = benchmark::Counter(
    state.iterations(), 
    benchmark::Counter::kIsRate
  );
}

// Benchmark: Complex nested data structure
BENCHMARK_DEFINE_F(GrpcLatencyFixture, NestedDataCall)(benchmark::State& state) {
  grpc::benchmark::ProcessEmployeeRequest request;
  auto* employee = request.mutable_employee();
  employee->mutable_person()->set_name("John Doe");
  employee->mutable_person()->set_age(35);
  employee->mutable_person()->set_email("john.doe@example.com");
  employee->mutable_address()->set_street("123 Main St");
  employee->mutable_address()->set_city("New York");
  employee->mutable_address()->set_country("USA");
  employee->mutable_address()->set_zip_code(10001);
  employee->set_employee_id(987654321);
  employee->set_salary(125000.50);
  employee->add_skills("C++");
  employee->add_skills("Python");
  employee->add_skills("JavaScript");
  employee->add_skills("TypeScript");
  employee->add_skills("Rust");
  
  grpc::benchmark::ProcessEmployeeResponse response;
  
  for (auto _ : state) {
    ClientContext context;
    Status status = stub_->ProcessEmployee(&context, request, &response);
    if (!status.ok()) {
      state.SkipWithError("RPC failed");
      break;
    }
    benchmark::DoNotOptimize(response.result());
  }

  state.SetLabel("gRPC");
  state.counters["calls/sec"] = benchmark::Counter(
    state.iterations(), 
    benchmark::Counter::kIsRate
  );
}

// Benchmark: Large data payload (1 MB)
BENCHMARK_DEFINE_F(GrpcLatencyFixture, LargeData1MB)(benchmark::State& state) {
  grpc::benchmark::ProcessLargeDataRequest request;
  std::string data(1024 * 1024, 0x42); // 1 MB
  request.set_data(data);
  
  grpc::benchmark::ProcessLargeDataResponse response;
  
  for (auto _ : state) {
    ClientContext context;
    Status status = stub_->ProcessLargeData(&context, request, &response);
    if (!status.ok()) {
      state.SkipWithError("RPC failed");
      break;
    }
    benchmark::DoNotOptimize(response.result());
  }

  state.SetLabel("gRPC");
  state.SetBytesProcessed(state.iterations() * data.size());
  state.counters["calls/sec"] = benchmark::Counter(
    state.iterations(), 
    benchmark::Counter::kIsRate
  );
}

// Benchmark: Very large data payload (10 MB)
BENCHMARK_DEFINE_F(GrpcLatencyFixture, LargeData10MB)(benchmark::State& state) {
  grpc::benchmark::ProcessLargeDataRequest request;
  std::string data(10 * 1024 * 1024, 0x42); // 10 MB
  request.set_data(data);
  
  grpc::benchmark::ProcessLargeDataResponse response;
  
  for (auto _ : state) {
    ClientContext context;
    Status status = stub_->ProcessLargeData(&context, request, &response);
    if (!status.ok()) {
      state.SkipWithError("RPC failed");
      break;
    }
    benchmark::DoNotOptimize(response.result());
  }

  state.SetLabel("gRPC");
  state.SetBytesProcessed(state.iterations() * data.size());
  state.counters["calls/sec"] = benchmark::Counter(
    state.iterations(), 
    benchmark::Counter::kIsRate
  );
}

// Register benchmarks
BENCHMARK_REGISTER_F(GrpcLatencyFixture, EmptyCall)
  ->Unit(benchmark::kMicrosecond);

BENCHMARK_REGISTER_F(GrpcLatencyFixture, CallWithReturn)
  ->Unit(benchmark::kMicrosecond);

BENCHMARK_REGISTER_F(GrpcLatencyFixture, SmallStringCall)
  ->Unit(benchmark::kMicrosecond);

BENCHMARK_REGISTER_F(GrpcLatencyFixture, NestedDataCall)
  ->Unit(benchmark::kMicrosecond);

BENCHMARK_REGISTER_F(GrpcLatencyFixture, LargeData1MB)
  ->Unit(benchmark::kMillisecond);

BENCHMARK_REGISTER_F(GrpcLatencyFixture, LargeData10MB)
  ->Unit(benchmark::kMillisecond);

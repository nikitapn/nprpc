// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// NPRPC Bandwidth Benchmarks - Data transfer rate measurements

#include <benchmark/benchmark.h>
#include <cstring>
#include <nprpc/nprpc.hpp>
#include <vector>

namespace {

// Test bandwidth with different payload sizes
static void BM_Bandwidth_PayloadSize(benchmark::State& state)
{
  const size_t payload_size = state.range(0);
  std::vector<char> payload(payload_size, 'x');

  for (auto _ : state) {
    // Simulate sending data
    benchmark::DoNotOptimize(payload.data());
    benchmark::ClobberMemory();
  }

  state.SetBytesProcessed(state.iterations() * payload_size);
  state.counters["MB/sec"] = benchmark::Counter(
      state.iterations() * payload_size, benchmark::Counter::kIsRate,
      benchmark::Counter::kIs1024);
}

// Test with various payload sizes
BENCHMARK(BM_Bandwidth_PayloadSize)
    ->Arg(100)       // 100 bytes
    ->Arg(1 << 10)   // 1 KB
    ->Arg(10 << 10)  // 10 KB
    ->Arg(100 << 10) // 100 KB
    ->Arg(1 << 20)   // 1 MB
    ->Arg(10 << 20)  // 10 MB
    ->Unit(benchmark::kMillisecond);

// Zero-copy bandwidth test (for shared memory)
static void BM_Bandwidth_ZeroCopy_SharedMemory(benchmark::State& state)
{
  const size_t size = state.range(0);
  std::vector<char> buffer(size);

  for (auto _ : state) {
    // Simulate zero-copy transfer (just pointer passing)
    char* ptr = buffer.data();
    benchmark::DoNotOptimize(ptr);
  }

  state.SetBytesProcessed(state.iterations() * size);
  state.counters["MB/sec"] =
      benchmark::Counter(state.iterations() * size, benchmark::Counter::kIsRate,
                         benchmark::Counter::kIs1024);
}

BENCHMARK(BM_Bandwidth_ZeroCopy_SharedMemory)
    ->Arg(1 << 20)   // 1 MB
    ->Arg(10 << 20)  // 10 MB
    ->Arg(100 << 20) // 100 MB
    ->Unit(benchmark::kMillisecond);

// Serialization overhead benchmark
static void BM_Bandwidth_SerializationOverhead(benchmark::State& state)
{
  const size_t size = state.range(0);
  std::vector<char> src(size, 'a');
  std::vector<char> dst(size);

  for (auto _ : state) {
    std::memcpy(dst.data(), src.data(), size);
    benchmark::ClobberMemory();
  }

  state.SetBytesProcessed(state.iterations() * size);
  state.counters["MB/sec"] =
      benchmark::Counter(state.iterations() * size, benchmark::Counter::kIsRate,
                         benchmark::Counter::kIs1024);
  state.SetLabel("memcpy_baseline");
}

BENCHMARK(BM_Bandwidth_SerializationOverhead)
    ->Arg(1 << 10)  // 1 KB
    ->Arg(1 << 20)  // 1 MB
    ->Arg(10 << 20) // 10 MB
    ->Unit(benchmark::kMillisecond);

} // anonymous namespace

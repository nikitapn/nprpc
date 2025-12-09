// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// NPRPC Throughput Benchmarks - Messages per second

#include "common.hpp"

#include <benchmark/benchmark.h>

#include <atomic>
#include <thread>
#include <vector>

namespace {

// Simple throughput test - how many calls can we make per second?
static void BM_Throughput_SingleThread_SharedMemory(benchmark::State& state)
{
  // TODO: Setup shared memory connection
  // For now, just measure the overhead

  uint32_t counter = 0;

  for (auto _ : state) {
    // Simulate minimal work
    counter++;
    benchmark::DoNotOptimize(counter);
  }

  state.counters["ops/sec"] =
      benchmark::Counter(state.iterations(), benchmark::Counter::kIsRate);
}
BENCHMARK(BM_Throughput_SingleThread_SharedMemory);

// Multi-threaded throughput test
static void BM_Throughput_MultiThread_SharedMemory(benchmark::State& state)
{
  uint32_t counter = 0;

  for (auto _ : state) {
    counter++;
    benchmark::DoNotOptimize(counter);
  }

  state.counters["ops/sec"] =
      benchmark::Counter(state.iterations(), benchmark::Counter::kIsRate);

  if (state.thread_index() == 0) {
    state.counters["total_ops/sec"] = benchmark::Counter(
        state.iterations() * state.threads(), benchmark::Counter::kIsRate);
  }
}
BENCHMARK(BM_Throughput_MultiThread_SharedMemory)
    ->Threads(1)
    ->Threads(2)
    ->Threads(4)
    ->Threads(8)
    ->Threads(16);

// Async call throughput (fire-and-forget pattern)
static void BM_Throughput_AsyncCalls_SharedMemory(benchmark::State& state)
{
  std::atomic<int> pending_calls{0};

  for (auto _ : state) {
    pending_calls++;

    // Simulate async callback
    state.PauseTiming();
    pending_calls--;
    state.ResumeTiming();
  }

  state.counters["async_ops/sec"] =
      benchmark::Counter(state.iterations(), benchmark::Counter::kIsRate);
}
BENCHMARK(BM_Throughput_AsyncCalls_SharedMemory);

} // anonymous namespace

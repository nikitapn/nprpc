# NPRPC Performance Benchmarks

This directory contains performance benchmarks for the NPRPC framework using Google Benchmark.

## What's Measured

### 1. Latency Benchmarks (`benchmark_latency.cpp`)
- **Empty Call**: Minimal RPC overhead with no payload
- **Call with Return Value**: Round-trip time including return value serialization
- **Small String Call**: Latency with 100-byte string payload

Each benchmark runs across all transport types:
- Shared Memory (zero-copy IPC)
- TCP Sockets
- WebSockets
- HTTP

### 2. Throughput Benchmarks (`benchmark_throughput.cpp`)
- **Single Thread**: Maximum calls per second from one thread
- **Multi Thread**: Scalability with 1, 2, 4, 8, 16 concurrent clients
- **Async Calls**: Fire-and-forget pattern throughput

### 3. Bandwidth Benchmarks (`benchmark_bandwidth.cpp`)
- **Payload Size**: Data transfer rates with various payload sizes (100B to 10MB)
- **Zero-Copy**: Shared memory zero-copy performance baseline
- **Serialization Overhead**: Comparison with memcpy baseline

## Prerequisites

Install Google Benchmark:

```bash
# Ubuntu/Debian
sudo apt install libbenchmark-dev

# macOS
brew install google-benchmark

# From source
git clone https://github.com/google/benchmark.git
cd benchmark
cmake -E make_directory "build"
cmake -E chdir "build" cmake -DBENCHMARK_DOWNLOAD_DEPENDENCIES=on -DCMAKE_BUILD_TYPE=Release ../
cmake --build "build" --config Release
sudo cmake --build "build" --config Release --target install
```

## Building

Benchmarks are built automatically when tests are enabled:

```bash
mkdir build && cd build
cmake -DNPRPC_BUILD_TESTS=ON ..
cmake --build .
```

If Google Benchmark is not found, benchmarks will be skipped with a warning.

## Running Benchmarks

### Run all benchmarks:
```bash
cmake --build . --target run_benchmarks
```

### Run specific benchmark:
```bash
./benchmark/nprpc_benchmarks --benchmark_filter=Latency
```

### Run with specific iterations:
```bash
./benchmark/nprpc_benchmarks --benchmark_min_time=5.0
```

### Save results to JSON:
```bash
cmake --build . --target run_benchmarks_save
# Results saved to build/benchmark/benchmark_results.json
```

### Common options:
```bash
# List all benchmarks without running
./benchmark/nprpc_benchmarks --benchmark_list_tests

# Run only latency tests
./benchmark/nprpc_benchmarks --benchmark_filter=Latency

# Run with specific number of iterations
./benchmark/nprpc_benchmarks --benchmark_repetitions=10

# Output in different formats
./benchmark/nprpc_benchmarks --benchmark_format=<console|json|csv>

# Show counters in tabular format
./benchmark/nprpc_benchmarks --benchmark_counters_tabular=true
```

## Example Output

```
-------------------------------------------------------------------------
Benchmark                              Time             CPU   Iterations
-------------------------------------------------------------------------
Latency/EmptyCall/0              1245 ns         1243 ns       562847   calls/sec=804.373k/s   [SharedMemory]
Latency/EmptyCall/1             52134 ns        52089 ns        13429   calls/sec=19.1979k/s   [TCP]
Latency/EmptyCall/2             55328 ns        55274 ns        12658   calls/sec=18.0921k/s   [WebSocket]
Latency/EmptyCall/3            145678 ns       145534 ns         4808   calls/sec=6.87145k/s   [HTTP]

Bandwidth/PayloadSize/1048576    2.45 ms         2.44 ms          286   MB/sec=410.542M/s
Bandwidth/ZeroCopy_SharedMemory  0.05 ms         0.05 ms        14235   MB/sec=18952.3M/s
```

## Interpreting Results

### Latency (lower is better)
- **Shared Memory**: Should be < 5µs for empty calls
- **TCP/WebSocket**: Typically 50-100µs on localhost
- **HTTP**: Higher overhead due to protocol complexity

### Throughput (higher is better)
- **Shared Memory**: Can achieve 500k-1M ops/sec
- **TCP/WebSocket**: Typically 10k-50k ops/sec
- **HTTP**: 5k-15k ops/sec

### Bandwidth (higher is better)
- **Shared Memory**: Limited only by memory bandwidth (10-20 GB/s)
- **Network Transports**: Limited by network interface (typically 1 Gbps = 125 MB/s)

## Adding New Benchmarks

1. Create a new file in `src/` (e.g., `benchmark_mytest.cpp`)
2. Include the benchmark framework:
```cpp
#include <benchmark/benchmark.h>
#include <nprpc/nprpc.hpp>

static void BM_MyTest(benchmark::State& state) {
  for (auto _ : state) {
    // Code to benchmark
  }
}
BENCHMARK(BM_MyTest);
```
3. Add to `CMakeLists.txt` sources
4. Rebuild and run

## Comparing Results

To compare performance changes:

```bash
# Run baseline
./nprpc_benchmarks --benchmark_out=baseline.json --benchmark_out_format=json

# Make changes to code...

# Run new version
./nprpc_benchmarks --benchmark_out=new.json --benchmark_out_format=json

# Compare (requires compare.py from Google Benchmark tools)
python3 tools/compare.py benchmarks baseline.json new.json
```

## Performance Tips

Based on benchmark results:

1. **Use Shared Memory** for same-machine communication (8-10x faster than TCP)
2. **Batch small requests** to amortize per-call overhead
3. **Use async calls** for non-blocking operations
4. **Choose appropriate transport**:
   - Shared Memory: Same machine, highest performance
   - TCP: Remote machines, good balance
   - WebSocket: Browser clients, bi-directional
   - HTTP: Stateless, web-compatible, lowest performance

## CI Integration

To fail CI builds on performance regression:

```bash
./nprpc_benchmarks --benchmark_filter=Latency/EmptyCall/0 \
  --benchmark_min_time=3 \
  --benchmark_format=json > results.json

# Check if p50 latency is within acceptable range
python3 check_regression.py results.json --max-latency-us=2000
```

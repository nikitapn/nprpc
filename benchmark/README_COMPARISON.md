# NPRPC vs gRPC Performance Comparison

This benchmark suite compares NPRPC against gRPC on identical test scenarios.

## Installation

### Install gRPC (Ubuntu/Debian)

```bash
sudo apt install -y \
    libgrpc++-dev \
    libprotobuf-dev \
    protobuf-compiler-grpc
```

### Or build gRPC from source (for latest version)

```bash
# Install dependencies
sudo apt install -y build-essential autoconf libtool pkg-config

# Clone gRPC
git clone --recurse-submodules -b v1.60.0 --depth 1 --shallow-submodules https://github.com/grpc/grpc
cd grpc

# Build and install
mkdir -p cmake/build
cd cmake/build
cmake -DgRPC_INSTALL=ON \
      -DgRPC_BUILD_TESTS=OFF \
      -DCMAKE_INSTALL_PREFIX=/usr/local \
      ../..
make -j$(nproc)
sudo make install
```

## Building Benchmarks

```bash
cd /path/to/nprpc
mkdir -p build && cd build
cmake ..
cmake --build . --target nprpc_benchmarks
```

The build will automatically detect if gRPC is available and include gRPC benchmarks.

## Running Benchmarks

### Run all benchmarks (NPRPC + gRPC)

```bash
cd build/benchmark
./nprpc_benchmarks --benchmark_counters_tabular=true
```

### Run only NPRPC benchmarks

```bash
./nprpc_benchmarks --benchmark_filter="LatencyFixture.*" --benchmark_counters_tabular=true
```

### Run only gRPC benchmarks

```bash
./nprpc_benchmarks --benchmark_filter="GrpcLatencyFixture.*" --benchmark_counters_tabular=true
```

### Save results to JSON

```bash
./nprpc_benchmarks \
  --benchmark_counters_tabular=true \
  --benchmark_out=comparison_results.json \
  --benchmark_out_format=json
```

## Benchmark Tests

Both frameworks are tested on identical scenarios:

1. **EmptyCall** - Minimal RPC call with no payload (measures pure framework overhead)
2. **CallWithReturn** - Simple function with integer parameters and return value
3. **SmallStringCall** - Call with 100-byte string payload

### NPRPC Tests
- Shared Memory transport
- TCP transport  
- WebSocket transport

### gRPC Tests
- HTTP/2 transport (standard gRPC)

## Expected Output

```
--------------------------------------------------------------------------------------
Benchmark                                 Time             CPU   Iterations  calls/sec
--------------------------------------------------------------------------------------
LatencyFixture/EmptyCall/0              17.0 us         17.0 us        41528  58.871k/s SharedMemory
LatencyFixture/EmptyCall/1              18.9 us         18.9 us        36365 52.8213k/s TCP
LatencyFixture/EmptyCall/2              20.0 us         20.0 us        35030 50.0187k/s WebSocket
GrpcLatencyFixture/EmptyCall            XX.X us         XX.X us        XXXXX  XX.XXXk/s gRPC
...
```

## Analysis

Key metrics to compare:
- **Latency (CPU time)**: Lower is better
- **Throughput (calls/sec)**: Higher is better
- **Iterations**: More iterations = more stable measurement

NPRPC advantages:
- Multiple transport options (shared memory, TCP, WebSocket)
- Shared memory for same-host ultra-low latency

gRPC advantages:
- Industry standard
- Wide language support
- Built-in features (auth, compression, streaming)

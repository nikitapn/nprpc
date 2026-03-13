#!/bin/bash

set -e

# Build with frame pointers for better stacks
cmake -S . -B .build_perf -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_FLAGS="-fno-omit-frame-pointer" \
  -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
  -DNPRPC_BUILD_TESTS=ON \
  -DNPRPC_BUILD_TOOLS=ON \
  -DNPRPC_ENABLE_QUIC=ON \
  -DNPRPC_ENABLE_HTTP3=ON \
  -DNPRPC_ENABLE_SSR=ON \
  -DNPRPC_BUILD_EXAMPLES=ON \
  -DNPRPC_BUILD_NODE_ADDON=ON

cmake --build .build_perf --target nprpc_benchmarks -j$(nproc)

killall benchmark_server npnameserver grpc_benchmark_server capnp_benchmark_aserver 2>/dev/null || true

# Start the server manually (same way run_benchmark.sh does)
# NPRPC_URING=1 .build_perf/benchmark/benchmark_server &
# sleep 1

# Record just the client process, filtered to the TCP large-data iterations
NPRPC_URING=1 perf record -g -F 999 \
  .build_perf/benchmark/nprpc_benchmarks \
  --benchmark_filter="LargeData10MB/1" \
  --benchmark_min_time=5s

perf report --no-children -g graph,0.5,caller
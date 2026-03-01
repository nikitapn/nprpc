#!/bin/env bash

set -e

cmake --build .build_release --target=nprpc_benchmarks -j$(nproc) 2>&1 > /dev/null
killall benchmark_server npnameserver grpc_benchmark_server capnp_benchmark_aserver 2>/dev/null || true
./.build_release/benchmark/nprpc_benchmarks $@ 2>&1 | awk '/----------/{f=1} /Shutdown requested/{f=0} f'

# Example usage:
# ./run_benchmark.sh --benchmark_filter="LatencyFixture/LargeData1MB/0"
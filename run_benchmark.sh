#!/bin/env bash

set -e

cmake --build .build_release --target=nprpc_benchmarks -j$(nproc) > /dev/null
killall benchmark_server npnameserver grpc_benchmark_server capnp_benchmark_server 2>/dev/null || true
./.build_release/benchmark/nprpc_benchmarks $@

# Example usage:
# ./run_benchmark.sh --benchmark_filter="LatencyFixture/LargeData1MB/0"
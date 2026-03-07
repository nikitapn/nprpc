#!/bin/env bash

set -e

cmake --build .build_relwith_debinfo --target=nprpc_benchmarks -j$(nproc) 2>&1 > /dev/null
killall benchmark_server npnameserver grpc_benchmark_server capnp_benchmark_aserver 2>/dev/null || true

# gdb -batch -ex 'set pagination off' -ex 'run' -ex 'bt' -ex 'thread apply all bt' --args \
  ./.build_relwith_debinfo/benchmark/nprpc_benchmarks $@ 2>&1 | awk '/----------/{f=1} /NPRPC Benchmark Environment Teardown/{f=0} f'

# Example usage:
# ./run_benchmark.sh --benchmark_filter="LatencyFixture/LargeData1MB/0"
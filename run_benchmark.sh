#!/bin/env bash

set -e

. .env

require_cmd() {
  if [[ -x "$1" ]]; then
    return 0
  fi
  command -v "$1" >/dev/null 2>&1 || {
    echo "Required command not found: $1" >&2
    exit 1
  }
}

ensure_nprpc_bpf_capabilities() {
  local binary="$BUILD_DIR/benchmark/benchmark_server"
  local current_caps=""

  require_cmd getcap
  require_cmd setcap

  if [[ ! -x "$binary" ]]; then
    echo "benchmark_server not found or not executable: $binary" >&2
    exit 1
  fi

  current_caps="$(getcap "$binary" 2>/dev/null || true)"
  if [[ "$current_caps" == *"cap_net_admin"* && "$current_caps" == *"cap_bpf"* ]]; then
    return 0
  fi

  if [[ ${EUID:-$(id -u)} -eq 0 ]]; then
    setcap cap_net_admin,cap_bpf+ep "$binary"
  else
    # sudo -v || {
    #   log "Failed to obtain sudo permissions; cannot grant BPF capabilities to benchmark_server"
    #   exit 1
    # }
    echo "Granting cap_net_admin,cap_bpf to benchmark_server for HTTP/3 reuseport BPF" >&2
    sudo setcap cap_net_admin,cap_bpf+ep "$binary"
  fi
}

cmake --build $BUILD_DIR --target=nprpc_benchmarks -j$(nproc)
killall benchmark_server npnameserver grpc_benchmark_server capnp_benchmark_aserver 2>/dev/null || true

ensure_nprpc_bpf_capabilities

# gdb -batch -ex 'set pagination off' -ex 'run' -ex 'bt' -ex 'thread apply all bt' --args \
  ./${BUILD_DIR}/benchmark/nprpc_benchmarks $@ 2>&1 | awk '/----------/{f=1} /NPRPC Benchmark Environment Teardown/{f=0} f'

# Example usage:
# ./run_benchmark.sh --benchmark_filter="LatencyFixture/LargeData1MB/0"
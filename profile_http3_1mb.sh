#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/.build_perf_http3}"
BUILD_TYPE="${BUILD_TYPE:-RelWithDebInfo}"
SKIP_CONFIGURE="${SKIP_CONFIGURE:-0}"
WWW_DIR="${WWW_DIR:-$ROOT_DIR/benchmark/http_shootout/.work/www}"
H2LOAD_BIN="${H2LOAD_BIN:-$ROOT_DIR/third_party/nghttp2/build-h2load/src/h2load}"
PERF_DATA="${PERF_DATA:-$ROOT_DIR/perf-http3-1mb.data}"
PERF_FREQ="${PERF_FREQ:-999}"
PERF_SECONDS="${PERF_SECONDS:-20}"
PERF_CALL_GRAPH="${PERF_CALL_GRAPH:-fp}"
PERF_EVENT="${PERF_EVENT:-cpu-clock}"
H2LOAD_DURATION="${H2LOAD_DURATION:-15}"
H2LOAD_WARMUP="${H2LOAD_WARMUP:-3}"
H2LOAD_CONNECTIONS="${H2LOAD_CONNECTIONS:-32}"
H2LOAD_STREAMS="${H2LOAD_STREAMS:-10}"
BENCH_HOST="${BENCH_HOST:-localhost}"
NPRPC_HTTP_PORT="${NPRPC_HTTP_PORT:-22223}"
TARGET_PATH="${TARGET_PATH:-/1mb.bin}"
SERVER_LOG="${SERVER_LOG:-$ROOT_DIR/benchmark/http_shootout/.work/tmp/profile-http3-server.log}"
OUTPUT_PATH="${OUTPUT_PATH:-$ROOT_DIR/perf.txt}"

SERVER_PID=""
PERF_PID=""

log() {
  printf '[profile_http3_1mb] %s\n' "$*"
}

require_cmd() {
  command -v "$1" >/dev/null 2>&1 || {
    printf 'Required command not found: %s\n' "$1" >&2
    exit 1
  }
}

cleanup() {
  set +e
  if [[ -n "$PERF_PID" ]] && kill -0 "$PERF_PID" 2>/dev/null; then
    wait "$PERF_PID" 2>/dev/null || true
  fi
  if [[ -n "$SERVER_PID" ]] && kill -0 "$SERVER_PID" 2>/dev/null; then
    kill -INT "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
  fi
}

trap cleanup EXIT

usage() {
  cat <<EOF
Usage: $(basename "$0") [profile|build]

Defaults:
  BUILD_DIR=$BUILD_DIR
  PERF_DATA=$PERF_DATA
  TARGET_URL=https://$BENCH_HOST:$NPRPC_HTTP_PORT$TARGET_PATH

Environment overrides:
  BUILD_DIR            Perf build directory
  BUILD_TYPE           CMake build type (default: RelWithDebInfo)
  PERF_DATA            Output perf.data path
  PERF_FREQ            perf sample frequency (default: 999)
  PERF_SECONDS         perf record duration (default: 20)
  PERF_CALL_GRAPH      perf call graph mode (default: fp)
  PERF_EVENT           perf event (default: cpu-clock; captures user+kernel time)
  H2LOAD_DURATION      h2load duration in seconds (default: 15)
  H2LOAD_WARMUP        h2load warm-up time in seconds (default: 3)
  H2LOAD_CONNECTIONS   h2load -c (default: 32)
  H2LOAD_STREAMS       h2load -m (default: 10)
  BENCH_HOST           HTTPS host (default: localhost)
  NPRPC_HTTP_PORT      Benchmark server HTTPS port (default: 22223)
  TARGET_PATH          Target path (default: /1mb.bin)
EOF
}

configure_and_build() {
  require_cmd cmake
  if [ "$SKIP_CONFIGURE" -eq 0 ]; then
    log "Configuring perf build in $BUILD_DIR"
    cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
      -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
      -DCMAKE_C_FLAGS="-fno-omit-frame-pointer" \
      -DCMAKE_CXX_FLAGS="-fno-omit-frame-pointer" \
      -DNPRPC_BUILD_TESTS=ON \
      -DNPRPC_BUILD_TOOLS=ON \
      -DNPRPC_ENABLE_QUIC=ON \
      -DNPRPC_ENABLE_HTTP3=ON \
      -DNPRPC_ENABLE_SSR=ON \
      -DNPRPC_BUILD_EXAMPLES=ON \
      -DNPRPC_BUILD_NODE_ADDON=ON
  fi
  log "Building benchmark_server"
  cmake --build "$BUILD_DIR" --target benchmark_server -j"$(nproc)"
}

prepare_assets() {
  log "Preparing HTTP shootout assets"
  "$ROOT_DIR/benchmark/http_shootout/run_server_shootout.sh" prepare-assets
}

ensure_h2load() {
  if [[ -x "$H2LOAD_BIN" ]]; then
    return
  fi

  log "Building local h2load"
  "$ROOT_DIR/benchmark/http_shootout/build_h2load.sh"
}

wait_for_server() {
  require_cmd curl

  local url="https://$BENCH_HOST:$NPRPC_HTTP_PORT$TARGET_PATH"
  for _ in $(seq 1 50); do
    if curl -ksS --http1.1 "$url" -o /dev/null >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.2
  done

  log "Server did not become ready; see $SERVER_LOG"
  return 1
}

start_server() {
  mkdir -p "$(dirname "$SERVER_LOG")"

  # Cleanup any existing server processes that might be using the port
  killall -9 benchmark_server npnameserver 2>/dev/null || true

  log "Starting benchmark_server"
  (
    cd "$ROOT_DIR"
    NPRPC_HTTP_ROOT_DIR="$WWW_DIR" \
    NPRPC_BENCH_ENABLE_HTTP3=1 \
    exec "$BUILD_DIR/benchmark/benchmark_server"
  ) >"$SERVER_LOG" 2>&1 &
  SERVER_PID=$!

  wait_for_server
  log "benchmark_server PID=$SERVER_PID"
}

record_server() {
  require_cmd perf

  log "Recording perf to $PERF_DATA (event=$PERF_EVENT, call-graph=$PERF_CALL_GRAPH)"
  perf record \
    -e "$PERF_EVENT" \
    -o "$PERF_DATA" \
    -g \
    --call-graph "$PERF_CALL_GRAPH" \
    -F "$PERF_FREQ" \
    -p "$SERVER_PID" \
    -- sleep "$PERF_SECONDS" &
  PERF_PID=$!
}

run_h2load() {
  local url="https://$BENCH_HOST:$NPRPC_HTTP_PORT$TARGET_PATH"

  log "Running h2load against $url"
  "$H2LOAD_BIN" \
    --alpn-list=h3 \
    --warm-up-time="$H2LOAD_WARMUP" \
    -D "$H2LOAD_DURATION" \
    -c "$H2LOAD_CONNECTIONS" \
    -m "$H2LOAD_STREAMS" \
    "$url" 2>&1 | tee "/tmp/h2load_output.txt"
}

profile_http3() {
  require_cmd bash
  configure_and_build
  prepare_assets
  ensure_h2load
  start_server
  record_server
  run_h2load
  wait "$PERF_PID"
  PERF_PID=""

  log "Perf data written to $PERF_DATA"
  log "Inspect with: perf report -i $PERF_DATA --no-children -g graph,0.5,caller"
  log "Or see output in $OUTPUT_PATH"

  cat "/tmp/h2load_output.txt" > "$OUTPUT_PATH"
  perf report --stdio -i /home/nikita/projects/nprpc/perf-http3-1mb.data --no-children -g graph,0.5,caller >> "$OUTPUT_PATH"
}

case "${1:-profile}" in
  build)
    configure_and_build
    ;;
  profile)
    profile_http3
    ;;
  -h|--help|help)
    usage
    ;;
  *)
    usage
    exit 1
    ;;
esac
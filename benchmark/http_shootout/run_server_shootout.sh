#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source $SCRIPT_DIR/.settings
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/.build_release}"
RESULTS_DIR="${RESULTS_DIR:-$ROOT_DIR/benchmark/http_shootout/results}"
WORK_DIR="${WORK_DIR:-$ROOT_DIR/benchmark/http_shootout/.work}"
WWW_DIR="$WORK_DIR/www"
TMP_DIR="$WORK_DIR/tmp"
LOCAL_H2LOAD_BIN="$ROOT_DIR/third_party/nghttp2/build-h2load/src/h2load"
SKIP_HTTP1="${SKIP_HTTP1:-0}"
SKIP_CADDY="${SKIP_CADDY:-0}"

# Payload sizes to benchmark.  Add or remove entries as needed.
# Supported suffixes: kb (kibibytes), mb (mebibytes).
if [ -z "${PAYLOAD_SIZES+x}" ]; then
  PAYLOAD_SIZES=("1kb" "64kb" "256kb" "1mb")
fi

NPRPC_DO_NOT_START_SERVER="${NPRPC_DO_NOT_START_SERVER:-0}"
NPRPC_HTTP_PORT="${NPRPC_HTTP_PORT:-22223}"
NPRPC_QUIC_PORT="${NPRPC_QUIC_PORT:-22225}"
NGINX_HTTP_PORT="${NGINX_HTTP_PORT:-28080}"
NGINX_HTTPS_PORT="${NGINX_HTTPS_PORT:-28443}"
CADDY_HTTP_PORT="${CADDY_HTTP_PORT:-29080}"
CADDY_HTTPS_PORT="${CADDY_HTTPS_PORT:-29443}"
BENCH_HOST="${BENCH_HOST:-localhost}"
NGINX_MODE="${NGINX_MODE:-auto}"
CADDY_MODE="${CADDY_MODE:-auto}"

OHA_BIN="${OHA_BIN:-oha}"
H2LOAD_BIN="${H2LOAD_BIN:-$LOCAL_H2LOAD_BIN}"

NPRPC_PID=""
NGINX_PID=""
CADDY_PID=""
NGINX_CID=""
CADDY_CID=""
NGINX_HTTP3_ENABLED=0

mkdir -p "$RESULTS_DIR" "$WWW_DIR" "$TMP_DIR"

log() {
  printf '[http_shootout] %s\n' "$*"
}

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
    log "benchmark_server not found or not executable: $binary"
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
    log "Granting cap_net_admin,cap_bpf to benchmark_server for HTTP/3 reuseport BPF"
    sudo setcap cap_net_admin,cap_bpf+ep "$binary"
  fi
}

cleanup() {
  set +e
  if [[ -n "$NPRPC_PID" ]] && kill -0 "$NPRPC_PID" 2>/dev/null; then
    kill -INT "$NPRPC_PID" 2>/dev/null || true
    wait "$NPRPC_PID" 2>/dev/null || true
  fi
  if [[ -n "$NGINX_PID" ]] && kill -0 "$NGINX_PID" 2>/dev/null; then
    kill -TERM "$NGINX_PID" 2>/dev/null || true
    wait "$NGINX_PID" 2>/dev/null || true
  fi
  if [[ -n "$CADDY_PID" ]] && kill -0 "$CADDY_PID" 2>/dev/null; then
    kill -TERM "$CADDY_PID" 2>/dev/null || true
    wait "$CADDY_PID" 2>/dev/null || true
  fi
  if [[ -n "$NGINX_CID" ]]; then
    docker rm -f "$NGINX_CID" >/dev/null 2>&1 || true
  fi
  if [[ -n "$CADDY_CID" ]]; then
    docker rm -f "$CADDY_CID" >/dev/null 2>&1 || true
  fi
}

trap cleanup EXIT

wait_for_nprpc() {
  require_cmd curl

  local url="https://$BENCH_HOST:$NPRPC_HTTP_PORT/1kb.bin"
  for _ in $(seq 1 50); do
    if curl -ksS --http1.1 "$url" -o /dev/null >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.2
  done

  log "NPRPC benchmark server did not become ready; see $TMP_DIR/nprpc_server.log"
  return 1
}

prepare_assets() {
  log "Preparing shared static assets"
  mkdir -p "$WWW_DIR"
  cat > "$WWW_DIR/index.html" <<'EOF'
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <title>NPRPC HTTP Shootout</title>
</head>
<body>
  <h1>NPRPC HTTP Shootout</h1>
  <p>Shared content for NPRPC, nginx, and Caddy.</p>
</body>
</html>
EOF

  python3 - <<'PY' "$WWW_DIR" "${PAYLOAD_SIZES[@]}"
from pathlib import Path
import random
import sys

root = Path(sys.argv[1])
sizes_raw = sys.argv[2:]
rng = random.Random(0xC0FFEE)

def parse_bytes(label: str) -> int:
    label = label.lower()
    if label.endswith("mb"):
        return int(label[:-2]) * 1024 * 1024
    if label.endswith("kb"):
        return int(label[:-2]) * 1024
    return int(label)

for size_label in sizes_raw:
    nbytes = parse_bytes(size_label)
    (root / f"{size_label}.bin").write_bytes(rng.randbytes(nbytes))
PY
}

build_benchmark_server() {
  log "Building benchmark server"
  cmake --build "$BUILD_DIR" --target benchmark_server -j"$(nproc)"
}

nginx_supports_http3() {
  if ! command -v nginx >/dev/null 2>&1; then
    return 1
  fi

  nginx -V 2>&1 | grep -q -- '--with-http_v3_module'
}

start_nprpc() {
  if [[ "$NPRPC_DO_NOT_START_SERVER" == "1" ]]; then
    SERVER_PID="$(pgrep -n -f benchmark_server || true)"
    if [ -n "$SERVER_PID" ]; then
      log "NPRPC benchmark server is already running with PID $SERVER_PID; skipping startup"
      return
    else
      log "NPRPC_DO_NOT_START_SERVER=1, but no running server found; cannot proceed"
      exit 1
    fi
  fi

  ensure_nprpc_bpf_capabilities

  log "Starting NPRPC benchmark server"
  local -a server_cmd

  server_cmd=(
    env
    "NPRPC_HTTP_ROOT_DIR=$WWW_DIR"
    "NPRPC_BENCH_ENABLE_HTTP3=1"
    "$BUILD_DIR/benchmark/benchmark_server"
  )

  killall -9 npnameserver benchmark_server 2>/dev/null || true
  sleep 0.1

  (
    cd "$ROOT_DIR"
    exec "${server_cmd[@]}"
  ) >"$TMP_DIR/nprpc_server.log" 2>&1 &
  NPRPC_PID=$!

  wait_for_nprpc

  log "NPRPC benchmark_server PID=$NPRPC_PID"
}

generate_nginx_conf() {
  mkdir -p "$TMP_DIR/nginx-client-body" "$TMP_DIR/nginx-proxy" \
           "$TMP_DIR/nginx-fastcgi" "$TMP_DIR/nginx-uwsgi" \
           "$TMP_DIR/nginx-scgi"
  local nginx_quic_listen=""
  local nginx_alt_svc=""
  if [[ "$NGINX_HTTP3_ENABLED" == "1" ]]; then
    nginx_quic_listen="    listen 127.0.0.1:${NGINX_HTTPS_PORT} quic reuseport;"
    nginx_alt_svc="    add_header Alt-Svc 'h3=\":${NGINX_HTTPS_PORT}\"; ma=86400' always;"
  fi
  cat > "$TMP_DIR/nginx.conf" <<EOF
worker_processes auto;
worker_cpu_affinity auto;
worker_rlimit_nofile 65535;
pid $TMP_DIR/nginx.pid;

events {
  worker_connections 8192;
  multi_accept on;
}

http {
  access_log off;
  error_log $TMP_DIR/nginx-error.log warn;
  sendfile on;
  quic_gso on;
  tcp_nopush on;
  tcp_nodelay on;
  keepalive_timeout 65;
  keepalive_requests 100000;
  open_file_cache max=2000 inactive=60s;
  open_file_cache_valid 120s;
  open_file_cache_min_uses 2;
  open_file_cache_errors on;
  ssl_session_cache shared:SSL:10m;
  ssl_session_timeout 10m;
  ssl_session_tickets off;
  client_body_temp_path $TMP_DIR/nginx-client-body;
  proxy_temp_path $TMP_DIR/nginx-proxy;
  fastcgi_temp_path $TMP_DIR/nginx-fastcgi;
  uwsgi_temp_path $TMP_DIR/nginx-uwsgi;
  scgi_temp_path $TMP_DIR/nginx-scgi;

  server {
    listen 127.0.0.1:${NGINX_HTTP_PORT} reuseport;
    listen 127.0.0.1:${NGINX_HTTPS_PORT} ssl reuseport;
${nginx_quic_listen}
    server_name localhost;

    ssl_certificate $ROOT_DIR/certs/out/localhost.crt;
    ssl_certificate_key $ROOT_DIR/certs/out/localhost.key;
${nginx_alt_svc}

    root $WWW_DIR;
    location / {
      try_files \$uri \$uri/ =404;
    }
  }
}
EOF
}

choose_mode() {
  local requested="$1"
  local binary="$2"
  if [[ "$requested" == "system" || "$requested" == "docker" ]]; then
    printf '%s\n' "$requested"
    return
  fi
  if command -v "$binary" >/dev/null 2>&1; then
    printf '%s\n' "system"
  else
    printf '%s\n' "docker"
  fi
}

start_nginx_system() {
  require_cmd nginx
  mkdir -p "$TMP_DIR/nginx-prefix"
  log "Starting nginx from system binary"
  nginx -c "$TMP_DIR/nginx.conf" -p "$TMP_DIR/nginx-prefix" -g 'daemon off;' \
    >"$TMP_DIR/nginx-stdout.log" 2>&1 &
  NGINX_PID=$!
  sleep 2
}

start_nginx() {
  local mode
  mode="$(choose_mode "$NGINX_MODE" nginx)"
  if [[ "$mode" == "system" ]] && nginx_supports_http3; then
    NGINX_HTTP3_ENABLED=1
  else
    NGINX_HTTP3_ENABLED=0
  fi

  generate_nginx_conf

  if [[ "$mode" == "system" ]]; then
    start_nginx_system
    return
  fi

  require_cmd docker
  log "Starting nginx container"
  NGINX_CID=$(docker run -d \
    -p "$NGINX_HTTP_PORT:$NGINX_HTTP_PORT" \
    -p "$NGINX_HTTPS_PORT:$NGINX_HTTPS_PORT" \
    -p "$NGINX_HTTPS_PORT:$NGINX_HTTPS_PORT/udp" \
    -v "$WWW_DIR:$WWW_DIR:ro" \
    -v "$TMP_DIR/nginx.conf:$TMP_DIR/nginx.conf:ro" \
    -v "$ROOT_DIR:$ROOT_DIR:ro" \
    nginx:alpine \
    nginx -c "$TMP_DIR/nginx.conf" -g 'daemon off;')
  sleep 2
}

generate_caddyfile() {
  cat > "$TMP_DIR/Caddyfile" <<EOF
{
  auto_https off
}

http://${BENCH_HOST}:${CADDY_HTTP_PORT} {
  root * $WWW_DIR
  file_server
}

https://${BENCH_HOST}:${CADDY_HTTPS_PORT} {
  tls $ROOT_DIR/certs/out/localhost.crt $ROOT_DIR/certs/out/localhost.key
  root * $WWW_DIR
  file_server
}
EOF
}

start_caddy_system() {
  require_cmd caddy
  log "Starting Caddy from system binary"
  caddy run --config "$TMP_DIR/Caddyfile" --adapter caddyfile \
    >"$TMP_DIR/caddy-stdout.log" 2>&1 &
  CADDY_PID=$!
  sleep 2
}

start_caddy() {
  generate_caddyfile
  local mode
  mode="$(choose_mode "$CADDY_MODE" caddy)"
  if [[ "$mode" == "system" ]]; then
    start_caddy_system
    return
  fi

  require_cmd docker
  log "Starting Caddy container"
  CADDY_CID=$(docker run -d \
    -p "$CADDY_HTTP_PORT:$CADDY_HTTP_PORT" \
    -p "$CADDY_HTTPS_PORT:$CADDY_HTTPS_PORT/udp" \
    -p "$CADDY_HTTPS_PORT:$CADDY_HTTPS_PORT/tcp" \
    -v "$WWW_DIR:$WWW_DIR:ro" \
    -v "$TMP_DIR/Caddyfile:$TMP_DIR/Caddyfile:ro" \
    -v "$ROOT_DIR:$ROOT_DIR:ro" \
    caddy:2-alpine \
    caddy run --config "$TMP_DIR/Caddyfile" --adapter caddyfile)
  sleep 2
}

run_oha() {
  local name="$1"
  local url="$2"
  require_cmd "$OHA_BIN"
  log "Running oha for $name"
  "$OHA_BIN" --no-tui --insecure -z 15s -c 64 --output-format json "$url" \
    > "$RESULTS_DIR/${name}.oha.json"
}

run_h2load() {
  local name="$1"
  local host="$2"
  local port="$3"
  local path="$4"
  require_cmd "$H2LOAD_BIN"
  log "Running h2load for $name"
  "$H2LOAD_BIN" --alpn-list=h3 --warm-up-time=3 -D 15 -c 32 -m 10 \
    "https://$host:$port$path" \
    > "$RESULTS_DIR/${name}.h2load.txt"
}

write_summary() {
  cat > "$RESULTS_DIR/README.md" <<'EOF'
# HTTP Shootout Results

Generated by 

	benchmark/http_shootout/run_server_shootout.sh

Files:

- `nprpc-http1-1kb.oha.json`
- `nginx-http1-1kb.oha.json`
- `caddy-http1-1kb.oha.json`
- `nprpc-http3-1kb.h2load.txt`
- `nginx-http3-1kb.h2load.txt` (when nginx exposes `http_v3_module`)
- `caddy-http3-1kb.h2load.txt`

Use `jq` for oha JSON and inspect the h2load text summaries for requests/sec,
latency distribution, and success rate.
EOF
}

run_suite() {
  if [ "$RESULTS_DIR" == "$ROOT_DIR/benchmark/http_shootout/results" ]; then
    echo "Clearing default results directory: $RESULTS_DIR"
    rm -rf "$RESULTS_DIR"/*
  fi

  prepare_assets
  build_benchmark_server
  start_nprpc
  start_nginx
  [ "$SKIP_CADDY" -ne 1 ] &&
    start_caddy

  if [[ "$SKIP_HTTP1" != "1" ]]; then
    for size in "${PAYLOAD_SIZES[@]}"; do
      run_oha "nprpc-http1-${size}" "https://$BENCH_HOST:$NPRPC_HTTP_PORT/${size}.bin"
      run_oha "nginx-http1-${size}" "https://$BENCH_HOST:$NGINX_HTTPS_PORT/${size}.bin"
      if [[ "$SKIP_CADDY" -ne 1 ]]; then
        run_oha "caddy-http1-${size}" "https://$BENCH_HOST:$CADDY_HTTPS_PORT/${size}.bin"
      fi
    done
  else
    log "Skipping HTTP/1.1 benchmarks as SKIP_HTTP1=1"
  fi

  for size in "${PAYLOAD_SIZES[@]}"; do
    run_h2load "nprpc-http3-${size}" "$BENCH_HOST" "$NPRPC_HTTP_PORT" "/${size}.bin"
    if [[ "$NGINX_HTTP3_ENABLED" == "1" ]]; then
      run_h2load "nginx-http3-${size}" "$BENCH_HOST" "$NGINX_HTTPS_PORT" "/${size}.bin"
    fi
    if [[ "$SKIP_CADDY" -ne 1 ]]; then
      run_h2load "caddy-http3-${size}" "$BENCH_HOST" "$CADDY_HTTPS_PORT" "/${size}.bin"
    fi
  done
  if [[ "$NGINX_HTTP3_ENABLED" != "1" ]]; then
    log "Skipping nginx HTTP/3 benchmarks (http_v3_module not available in current mode)"
  fi

  write_summary
  log "Results written to $RESULTS_DIR"
}

usage() {
  cat <<EOF
Usage: $0 [prepare-assets|build|build-h2load|start-nprpc|run|view]

Commands:
  prepare-assets  Create shared static files for all servers.
  build           Build the NPRPC benchmark server.
  build-h2load    Build a vendored h2load under third_party/nghttp2.
  start-nprpc     Start only the NPRPC benchmark server with HTTP/3 enabled.
  run             Run the full shootout suite against NPRPC, nginx, and Caddy.
  view            Summarize result files in benchmark/http_shootout/results.

Requirements:
  - nginx and/or docker
  - caddy and/or docker
  - oha (HTTP/1.1/HTTPS load)
  - h2load (HTTP/3 load), or run ./benchmark/http_shootout/build_h2load.sh
Environment:
  - NGINX_MODE=auto|system|docker (default: auto)
  - CADDY_MODE=auto|system|docker (default: auto)
  - BENCH_HOST=localhost|127.0.0.1 (default: localhost)
EOF
}

case "${1:-run}" in
  prepare-assets)
    prepare_assets
    ;;
  build)
    build_benchmark_server
    ;;
  build-h2load)
    "$ROOT_DIR/benchmark/http_shootout/build_h2load.sh"
    ;;
  start-nprpc)
    prepare_assets
    build_benchmark_server
    start_nprpc
    wait "$NPRPC_PID"
    ;;
  run)
    run_suite
    ;;
  view)
    python3 "$ROOT_DIR/benchmark/http_shootout/view_results.py" "$RESULTS_DIR"
    ;;
  run-view)
    run_suite
    python3 "$ROOT_DIR/benchmark/http_shootout/view_results.py" "$RESULTS_DIR"
    ;;
  *)
    usage
    exit 1
    ;;
esac
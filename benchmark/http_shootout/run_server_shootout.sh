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
LOCAL_H2LOAD_BIN="$ROOT_DIR/third_party/nghttp2/build/src/h2load"
SKIP_HTTP1="${SKIP_HTTP1:-0}"
SKIP_CADDY="${SKIP_CADDY:-0}"
SKIP_NGINX="${SKIP_NGINX:-0}"
SKIP_NPRPC="${SKIP_NPRPC:-0}"

# When enabled, attach strace to each server (nprpc/nginx/caddy, system mode
# only) and report network syscall counts alongside the final results.
# Disabled by default since it adds tracing overhead.
TRACE_SYSCALLS="${TRACE_SYSCALLS:-0}"

# Syscalls to count when TRACE_SYSCALLS=1. Default covers both the
# recvmsg/sendmsg style (TCP, boost::asio's buffer-sequence path) and the
# recvfrom/sendto style (nprpc's HTTP/3 UDP path uses async_receive_from /
# async_send_to, which resolve to recvfrom/sendto on Linux -- not recvmsg).
# Override to narrow/widen what gets counted.
TRACE_SYSCALLS_FILTER="${TRACE_SYSCALLS_FILTER:-sendmsg,sendmmsg,recvmsg,recvmmsg,sendto,recvfrom}"

# When enabled, capture each server's (system-mode nginx/caddy, plus nprpc)
# QUIC/UDP traffic with tcpdump during the run, one pcap per server, for
# later visualization with `run_server_shootout.sh plot`. Disabled by
# default since it needs sudo and adds capture overhead.
CAPTURE_PACKETS="${CAPTURE_PACKETS:-0}"

# Script that turns a pcap into a packet-length-over-time chart.
PACKET_PLOT_SCRIPT="${PACKET_PLOT_SCRIPT:-$SCRIPT_DIR/plot_packets.py}"

# Payload sizes to benchmark.  Add or remove entries as needed.
# Supported suffixes: kb (kibibytes), mb (mebibytes).
if [ -z "${PAYLOAD_SIZES+x}" ]; then
  PAYLOAD_SIZES=("1kb" "64kb" "256kb" "1mb")
fi

NPRPC_DO_NOT_START_SERVER="${NPRPC_DO_NOT_START_SERVER:-0}"
NPRPC_HTTP_PORT="${NPRPC_HTTP_PORT:-22223}"

# Router variant: npquicrouter listens on these public ports, forwards to
# the backend on NPRPC_ROUTER_BACKEND_* ports via the SHM egress channel.
NPRPC_ROUTER_HTTP_PORT="${NPRPC_ROUTER_HTTP_PORT:-22233}"
NPRPC_ROUTER_BACKEND_HTTP_PORT="${NPRPC_ROUTER_BACKEND_HTTP_PORT:-22243}"
NPRPC_ROUTER_NUM_WORKERS="${NPRPC_ROUTER_NUM_WORKERS:-4}"
NPRPC_ROUTER_BIN="${NPRPC_ROUTER_BIN:-$BUILD_DIR/npquicrouter/npquicrouter}"
NPRPC_ROUTER_SHM_CHANNEL="bench_quic_edge" # Use same name for ingress and egress for simplicity in this benchmark setup
NGINX_HTTP_PORT="${NGINX_HTTP_PORT:-28080}"
NGINX_HTTPS_PORT="${NGINX_HTTPS_PORT:-28443}"
CADDY_HTTP_PORT="${CADDY_HTTP_PORT:-29080}"
CADDY_HTTPS_PORT="${CADDY_HTTPS_PORT:-29443}"
BENCH_HOST="${BENCH_HOST:-localhost}"
NGINX_MODE="${NGINX_MODE:-auto}"
CADDY_MODE="${CADDY_MODE:-auto}"

OHA_BIN="${OHA_BIN:-oha}"
H2LOAD_BIN="${H2LOAD_BIN:-$LOCAL_H2LOAD_BIN}"
H2LOAD_ARGS="--warm-up-time=3 -D 15 -c 32 -m 10"
if [ "$CAPTURE_PACKETS" == "1" ]; then
  H2LOAD_ARGS="--warm-up-time=0 -D 1 -c 2 -m 10"
fi
H2LOAD_ARGS="--alpn-list=h3 ${H2LOAD_ARGS}"


NPRPC_PID=""
NPRPC_ROUTER_PID=""
NPRPC_ROUTER_BACKEND_PID=""
NGINX_PID=""
CADDY_PID=""
NGINX_CID=""
CADDY_CID=""
NGINX_HTTP3_ENABLED=0
declare -A STRACE_PIDS=()
declare -A TCPDUMP_PIDS=()

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

start_strace() {
  local name="$1"
  shift
  local -a pids=("$@")
  [[ "$TRACE_SYSCALLS" == "1" ]] || return 0
  if [[ ${#pids[@]} -eq 0 || -z "${pids[0]}" ]]; then
    log "TRACE_SYSCALLS=1 but no PID available for $name; skipping strace"
    return 0
  fi
  require_cmd strace
  local out="$RESULTS_DIR/${name}.strace.txt"
  local -a strace_args=(-f -c -e "trace=${TRACE_SYSCALLS_FILTER}")
  local pid
  for pid in "${pids[@]}"; do
    strace_args+=(-p "$pid")
  done
  log "Attaching strace to $name (PIDs: ${pids[*]}) for syscall counts"
  # -f only follows forks that happen *after* attach, so callers must pass
  # any pre-existing worker PIDs explicitly (e.g. nginx forks its workers
  # before we get a chance to attach).
  # -n + </dev/null: fail fast instead of blocking on a sudo password
  # prompt from a backgrounded job (assumes passwordless sudo is configured
  # for strace; see NOPASSWD sudoers entry).
  sudo -n strace "${strace_args[@]}" -o "$out" </dev/null >/dev/null 2>&1 &
  STRACE_PIDS["$name"]=$!
}

collect_child_pids() {
  local parent_pid="$1"
  pgrep -P "$parent_pid" 2>/dev/null || true
}

start_packet_capture() {
  local name="$1"
  local port="$2"
  [[ "$CAPTURE_PACKETS" == "1" ]] || return 0
  require_cmd tcpdump
  # Port is baked into the filename (not just tracked in-memory) so a later,
  # separate `plot` invocation can recover it without needing this run's
  # shell state.
  local out="$RESULTS_DIR/${name}-${port}.pcap"
  rm -f "$out"
  log "Capturing UDP traffic for $name on port $port -> $(basename "$out")"
  # -U: flush to the output file as packets arrive, so a SIGTERM below
  # doesn't lose buffered-but-uncommitted packets.
  sudo -n tcpdump -i any -n -U -w "$out" "udp port ${port}" \
    </dev/null >/dev/null 2>&1 &
  TCPDUMP_PIDS["$name"]=$!
}

stop_packet_capture_all() {
  [[ "$CAPTURE_PACKETS" == "1" ]] || return 0
  local name pid
  for name in "${!TCPDUMP_PIDS[@]}"; do
    pid="${TCPDUMP_PIDS[$name]}"
    if kill -0 "$pid" 2>/dev/null; then
      kill -TERM "$pid" 2>/dev/null || true
      wait "$pid" 2>/dev/null || true
    fi
    unset 'TCPDUMP_PIDS[$name]'
  done
}

plot_packet_captures() {
  require_cmd python3
  if [[ ! -f "$PACKET_PLOT_SCRIPT" ]]; then
    log "PACKET_PLOT_SCRIPT not found: $PACKET_PLOT_SCRIPT"
    exit 1
  fi

  local file base name port title found=0
  local -a plot_pids=()
  for file in "$RESULTS_DIR"/*.pcap; do
    [[ -e "$file" ]] || continue
    found=1
    base="$(basename "$file" .pcap)"
    port="${base##*-}"
    name="${base%-*}"
    title="$(printf '%s' "$name" | tr '[:lower:]' '[:upper:]')"
    log "Plotting $(basename "$file") (port $port)"
    python3 "$PACKET_PLOT_SCRIPT" --pcap "$file" --port "$port" --title "$title" &
    plot_pids+=($!)
  done

  if [[ "$found" -eq 0 ]]; then
    log "No .pcap files found in $RESULTS_DIR (run with CAPTURE_PACKETS=1 first)"
    return 1
  fi

  # Each plot opens its own window; wait for all of them to be closed.
  wait "${plot_pids[@]}" 2>/dev/null || true
}

stop_strace_all() {
  [[ "$TRACE_SYSCALLS" == "1" ]] || return 0
  local name pid
  for name in "${!STRACE_PIDS[@]}"; do
    pid="${STRACE_PIDS[$name]}"
    if kill -0 "$pid" 2>/dev/null; then
      kill -INT "$pid" 2>/dev/null || true
      wait "$pid" 2>/dev/null || true
    fi
    unset 'STRACE_PIDS[$name]'
  done
}

print_syscall_report() {
  [[ "$TRACE_SYSCALLS" == "1" ]] || return 0
  local name file
  echo
  log "Syscall counts (${TRACE_SYSCALLS_FILTER}):"
  for name in nprpc nginx caddy; do
    file="$RESULTS_DIR/${name}.strace.txt"
    if [[ -f "$file" ]]; then
      echo "--- $name ---"
      cat "$file"
      echo
    fi
  done
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
  stop_strace_all
  stop_packet_capture_all
  if [[ -n "$NPRPC_PID" ]] && kill -0 "$NPRPC_PID" 2>/dev/null; then
    kill -INT "$NPRPC_PID" 2>/dev/null || true
    wait "$NPRPC_PID" 2>/dev/null || true
  fi
  if [[ -n "$NPRPC_ROUTER_PID" ]] && kill -0 "$NPRPC_ROUTER_PID" 2>/dev/null; then
    kill -INT "$NPRPC_ROUTER_PID" 2>/dev/null || true
    wait "$NPRPC_ROUTER_PID" 2>/dev/null || true
  fi
  if [[ -n "$NPRPC_ROUTER_BACKEND_PID" ]] && kill -0 "$NPRPC_ROUTER_BACKEND_PID" 2>/dev/null; then
    kill -INT "$NPRPC_ROUTER_BACKEND_PID" 2>/dev/null || true
    wait "$NPRPC_ROUTER_BACKEND_PID" 2>/dev/null || true
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

build_router() {
  log "Building npquicrouter"
  cmake --build "$BUILD_DIR" --target npquicrouter -j"$(nproc)"
}

ensure_router_bpf_capabilities() {
  local binary="$NPRPC_ROUTER_BIN"
  require_cmd getcap
  require_cmd setcap
  if [[ ! -x "$binary" ]]; then
    log "npquicrouter not found or not executable: $binary"
    exit 1
  fi
  local current_caps
  current_caps="$(getcap "$binary" 2>/dev/null || true)"
  if [[ "$current_caps" == *"cap_net_admin"* && "$current_caps" == *"cap_bpf"* ]]; then
    return 0
  fi
  log "Granting cap_net_admin,cap_bpf to npquicrouter for SO_REUSEPORT eBPF routing"
  if [[ ${EUID:-$(id -u)} -eq 0 ]]; then
    setcap cap_net_admin,cap_bpf+ep "$binary"
  else
    sudo setcap cap_net_admin,cap_bpf+ep "$binary"
  fi
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
      start_strace nprpc "$SERVER_PID"
      start_packet_capture nprpc "$NPRPC_HTTP_PORT"
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
  start_strace nprpc "$NPRPC_PID"
  start_packet_capture nprpc "$NPRPC_HTTP_PORT"
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
  # nginx's master process mostly just supervises; the actual client I/O
  # (sendmsg/recvmsg) happens in worker processes, which have already been
  # forked by the time we get here. Attach to the master *and* its current
  # workers so strace's -f can also follow any future respawns.
  local -a nginx_worker_pids=()
  while IFS= read -r pid; do
    [[ -n "$pid" ]] && nginx_worker_pids+=("$pid")
  done < <(collect_child_pids "$NGINX_PID")
  start_strace nginx "$NGINX_PID" "${nginx_worker_pids[@]}"
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
  start_packet_capture nginx "$NGINX_HTTPS_PORT"

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
  start_strace caddy "$CADDY_PID"
}

start_caddy() {
  generate_caddyfile
  start_packet_capture caddy "$CADDY_HTTPS_PORT"
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
  "$H2LOAD_BIN" $H2LOAD_ARGS \
    "https://$host:$port$path" \
    > "$RESULTS_DIR/${name}.h2load.txt"
}

# ---------------------------------------------------------------------------
# Router-variant helpers
# ---------------------------------------------------------------------------

# Write a temporary npquicrouter config that puts the router on
# NPRPC_ROUTER_HTTP_PORT and proxies to the backend
# on NPRPC_ROUTER_BACKEND_* via SHM.
generate_router_config() {
  cat > "$TMP_DIR/router.config.json" <<EOF
{
  "listen_address": "127.0.0.1",
  "listen_tcp_port": ${NPRPC_ROUTER_HTTP_PORT},
  "listen_udp_port": ${NPRPC_ROUTER_HTTP_PORT},
  "shm_egress_channel": "${NPRPC_ROUTER_SHM_CHANNEL}",
  "num_workers": ${NPRPC_ROUTER_NUM_WORKERS},
  "routes": [
    {
      "sni": "localhost",
      "tcp_backend": "127.0.0.1:${NPRPC_ROUTER_BACKEND_HTTP_PORT}",
      "udp_backend": "127.0.0.1:${NPRPC_ROUTER_BACKEND_HTTP_PORT}",
      "shm_ingress_channel": "${NPRPC_ROUTER_SHM_CHANNEL}"
    }
  ]
}
EOF
}

wait_for_router() {
  require_cmd curl
  local url="https://localhost:${NPRPC_ROUTER_HTTP_PORT}/1kb.bin"
  for _ in $(seq 1 50); do
    if curl -ksS --http1.1 "$url" -o /dev/null >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.2
  done
  log "npquicrouter did not become ready; see $TMP_DIR/router.log"
  return 1
}

start_nprpc_via_router() {
  ensure_router_bpf_capabilities
  ensure_nprpc_bpf_capabilities
  generate_router_config

  killall -9 npquicrouter 2>/dev/null || true
  sleep 0.1

  log "Starting npquicrouter (workers=${NPRPC_ROUTER_NUM_WORKERS})"
  (
    cd "$ROOT_DIR"
    exec "$NPRPC_ROUTER_BIN" "$TMP_DIR/router.config.json"
  ) >"$TMP_DIR/router.log" 2>&1 &
  NPRPC_ROUTER_PID=$!

  # Give router a moment to create SHM rings before backend opens them
  sleep 1

  log "Starting NPRPC backend (router mode) on port (tcp/udp) ${NPRPC_ROUTER_BACKEND_HTTP_PORT}"
  (
    cd "$ROOT_DIR"
    exec env \
      "NPRPC_HTTP_ROOT_DIR=$WWW_DIR" \
      "NPRPC_BENCH_ENABLE_HTTP3=1" \
      "NPRPC_BENCH_HTTP_PORT=${NPRPC_ROUTER_BACKEND_HTTP_PORT}" \
      "NPRPC_BENCH_SHM_EGRESS=${NPRPC_ROUTER_SHM_CHANNEL}" \
      "NPRPC_BENCH_SHM_INGRESS=${NPRPC_ROUTER_SHM_CHANNEL}" \
      "$BUILD_DIR/benchmark/benchmark_server"
  ) >"$TMP_DIR/nprpc_backend_router.log" 2>&1 &
  NPRPC_ROUTER_BACKEND_PID=$!

  sleep 1

  wait_for_router
  log "npquicrouter PID=$NPRPC_ROUTER_PID backend PID=$NPRPC_ROUTER_BACKEND_PID"
  start_strace nprpc "$NPRPC_ROUTER_BACKEND_PID"
  start_packet_capture nprpc "$NPRPC_ROUTER_HTTP_PORT"
}

run_suite_with_router() {
  local results_suffix="${1:-router}"
  local orig_results_dir="$RESULTS_DIR"
  RESULTS_DIR="$orig_results_dir/$results_suffix"
  mkdir -p "$RESULTS_DIR"

  prepare_assets
  build_benchmark_server
  build_router
  start_nprpc_via_router

  capture_environment_report

  for size in "${PAYLOAD_SIZES[@]}"; do
    run_h2load "nprpc-router-http3-${size}" "$BENCH_HOST" "$NPRPC_ROUTER_HTTP_PORT" "/${size}.bin"
  done

  stop_strace_all
  stop_packet_capture_all
  print_syscall_report

  log "Router-variant results written to $RESULTS_DIR"
  RESULTS_DIR="$orig_results_dir"
}

capture_environment_report() {
  local report="$RESULTS_DIR/ENVIRONMENT.md"
  local resolved_nginx_mode="auto"
  local resolved_caddy_mode="auto"
  local nginx_http3_capable="0"
  local hostname_value="unknown"
  local kernel_value="unknown"
  local architecture="unknown"
  local cpu_model="unknown"
  local cpu_count="unknown"
  local sockets="unknown"
  local cores_per_socket="unknown"
  local threads_per_core="unknown"
  local numa_nodes="unknown"
  local max_mhz="unknown"
  local min_mhz="unknown"
  local virtualization="unknown"
  local mem_total="unknown"
  local current_clocksource="unknown"
  local available_clocksources="unknown"
  local scaling_governor="unknown"
  local cpu_driver="unknown"
  local h2load_version="unknown"
  local oha_version="unknown"
  local nginx_version="not installed"
  local caddy_version="not installed"

  hostname_value="$(hostname 2>/dev/null || true)"
  kernel_value="$(uname -srmo 2>/dev/null || uname -a 2>/dev/null || true)"
  resolved_nginx_mode="$(choose_mode "$NGINX_MODE" nginx)"
  resolved_caddy_mode="$(choose_mode "$CADDY_MODE" caddy)"
  if [[ "$resolved_nginx_mode" == "system" ]] && nginx_supports_http3; then
    nginx_http3_capable="1"
  fi

  if command -v lscpu >/dev/null 2>&1; then
    architecture="$(lscpu | awk -F: '/^Architecture:/ {sub(/^[ 	]+/, "", $2); print $2; exit}')"
    cpu_model="$(lscpu | awk -F: '/^Model name:/ {sub(/^[ 	]+/, "", $2); print $2; exit}')"
    cpu_count="$(lscpu | awk -F: '/^CPU\(s\):/ {sub(/^[ 	]+/, "", $2); print $2; exit}')"
    sockets="$(lscpu | awk -F: '/^Socket\(s\):/ {sub(/^[ 	]+/, "", $2); print $2; exit}')"
    cores_per_socket="$(lscpu | awk -F: '/^Core\(s\) per socket:/ {sub(/^[ 	]+/, "", $2); print $2; exit}')"
    threads_per_core="$(lscpu | awk -F: '/^Thread\(s\) per core:/ {sub(/^[ 	]+/, "", $2); print $2; exit}')"
    numa_nodes="$(lscpu | awk -F: '/^NUMA node\(s\):/ {sub(/^[ 	]+/, "", $2); print $2; exit}')"
    max_mhz="$(lscpu | awk -F: '/^CPU max MHz:/ {sub(/^[ 	]+/, "", $2); print $2; exit}')"
    min_mhz="$(lscpu | awk -F: '/^CPU min MHz:/ {sub(/^[ 	]+/, "", $2); print $2; exit}')"
    virtualization="$(lscpu | awk -F: '/^Virtualization:/ {sub(/^[ 	]+/, "", $2); print $2; exit}')"
  fi

  if [[ -r /proc/meminfo ]]; then
    mem_total="$(awk '/^MemTotal:/ {printf "%.2f GiB", $2 / 1024 / 1024; exit}' /proc/meminfo)"
  fi

  if [[ -r /sys/devices/system/clocksource/clocksource0/current_clocksource ]]; then
    current_clocksource="$(< /sys/devices/system/clocksource/clocksource0/current_clocksource)"
  fi
  if [[ -r /sys/devices/system/clocksource/clocksource0/available_clocksource ]]; then
    available_clocksources="$(< /sys/devices/system/clocksource/clocksource0/available_clocksource)"
  fi
  if [[ -r /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor ]]; then
    scaling_governor="$(< /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor)"
  fi
  if [[ -r /sys/devices/system/cpu/cpu0/cpufreq/scaling_driver ]]; then
    cpu_driver="$(< /sys/devices/system/cpu/cpu0/cpufreq/scaling_driver)"
  fi

  if [[ -x "$H2LOAD_BIN" ]]; then
    h2load_version="$($H2LOAD_BIN --version 2>/dev/null | head -n1 || true)"
  elif command -v "$H2LOAD_BIN" >/dev/null 2>&1; then
    h2load_version="$("$H2LOAD_BIN" --version 2>/dev/null | head -n1 || true)"
  fi

  if command -v "$OHA_BIN" >/dev/null 2>&1; then
    oha_version="$("$OHA_BIN" --version 2>/dev/null | head -n1 || true)"
  fi
  if command -v nginx >/dev/null 2>&1; then
    nginx_version="$(nginx -V 2>&1 | head -n1 || true)"
  fi
  if command -v caddy >/dev/null 2>&1; then
    caddy_version="$(caddy version 2>/dev/null | head -n1 || true)"
  fi

  result=$(cat << EOF
# Benchmark Environment

- Generated at: $(date -Is)
- Hostname: ${hostname_value:-unknown}
- Kernel: ${kernel_value:-unknown}
- Architecture: ${architecture:-unknown}
- CPU model: ${cpu_model:-unknown}
- Logical CPUs: ${cpu_count:-unknown}
- Sockets: ${sockets:-unknown}
- Cores per socket: ${cores_per_socket:-unknown}
- Threads per core: ${threads_per_core:-unknown}
- NUMA nodes: ${numa_nodes:-unknown}
- CPU max MHz: ${max_mhz:-unknown}
- CPU min MHz: ${min_mhz:-unknown}
- Virtualization: ${virtualization:-unknown}
- Memory: ${mem_total:-unknown}
- Current clocksource: ${current_clocksource:-unknown}
- Available clocksources: ${available_clocksources:-unknown}
- CPU scaling governor: ${scaling_governor:-unknown}
- CPU scaling driver: ${cpu_driver:-unknown}

## Harness

- Build dir: ${BUILD_DIR}
- Benchmark host: ${BENCH_HOST}
- Payload sizes: ${PAYLOAD_SIZES[*]}
- H2LOAD_BIN: ${H2LOAD_BIN}
- h2load version: ${h2load_version:-unknown}
- OHA_BIN: ${OHA_BIN}
- oha version: ${oha_version:-unknown}
- nginx mode: ${NGINX_MODE}
- nginx resolved mode: ${resolved_nginx_mode}
- nginx version: ${nginx_version:-not installed}
- nginx HTTP/3 capable: ${nginx_http3_capable}
- caddy mode: ${CADDY_MODE}
- caddy resolved mode: ${resolved_caddy_mode}
- caddy version: ${caddy_version:-not installed}
- SKIP_HTTP1: ${SKIP_HTTP1}
- SKIP_CADDY: ${SKIP_CADDY}
- SKIP_NGINX: ${SKIP_NGINX}
- SKIP_NPRPC: ${SKIP_NPRPC}
EOF
)
  echo "$result" > "$report"
  echo "$result"
}

write_summary() {
  cat > "$RESULTS_DIR/README.md" <<'EOF'
# HTTP Shootout Results

Generated by 

	benchmark/http_shootout/run_server_shootout.sh

Environment:

- `ENVIRONMENT.md` contains the host hardware, kernel, clocksource, and tool configuration for this run.

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
  [ "$SKIP_NPRPC" -ne 1 ] &&
    start_nprpc

  [ "$SKIP_NGINX" -ne 1 ] &&
    start_nginx

  [ "$SKIP_CADDY" -ne 1 ] &&
    start_caddy

  capture_environment_report

  if [ "$SKIP_HTTP1" -ne 1 ]; then
    for size in "${PAYLOAD_SIZES[@]}"; do
      [ "$SKIP_NPRPC" -ne 1 ] &&
        run_oha "nprpc-http1-${size}" "https://$BENCH_HOST:$NPRPC_HTTP_PORT/${size}.bin"
      [ "$SKIP_NGINX" -ne 1 ] &&
        run_oha "nginx-http1-${size}" "https://$BENCH_HOST:$NGINX_HTTPS_PORT/${size}.bin"
      [ "$SKIP_CADDY" -ne 1 ] &&
        run_oha "caddy-http1-${size}" "https://$BENCH_HOST:$CADDY_HTTPS_PORT/${size}.bin"
    done
  else
    log "Skipping HTTP/1.1 benchmarks as SKIP_HTTP1=1"
  fi

  for size in "${PAYLOAD_SIZES[@]}"; do
    [ "$SKIP_NPRPC" -ne 1 ] &&
      run_h2load "nprpc-http3-${size}" "$BENCH_HOST" "$NPRPC_HTTP_PORT" "/${size}.bin"
    if [[ "$NGINX_HTTP3_ENABLED" == "1" && "$SKIP_NGINX" -ne 1 ]]; then
      run_h2load "nginx-http3-${size}" "$BENCH_HOST" "$NGINX_HTTPS_PORT" "/${size}.bin"
    fi
    [ "$SKIP_CADDY" -ne 1 ] &&
      run_h2load "caddy-http3-${size}" "$BENCH_HOST" "$CADDY_HTTPS_PORT" "/${size}.bin"
  done
  if [[ "$NGINX_HTTP3_ENABLED" != "1" ]]; then
    log "Skipping nginx HTTP/3 benchmarks (http_v3_module not available in current mode)"
  fi

  stop_strace_all
  stop_packet_capture_all
  print_syscall_report

  write_summary
  log "Results written to $RESULTS_DIR"
}

usage() {
  cat <<EOF
Usage: $0 [prepare-assets|build|build-h2load|capture-environment|start-nprpc|run|run-router|compare|view|plot]

Commands:
  prepare-assets  Create shared static files for all servers.
  build           Build the NPRPC benchmark server.
  build-h2load    Build a vendored h2load under third_party/nghttp2.
  capture-environment  Write ENVIRONMENT.md and refresh README.md in the results directory.
  start-nprpc     Start only the NPRPC benchmark server with HTTP/3 enabled.
  run             Run the full shootout suite against NPRPC, nginx, and Caddy.
  run-router      Run HTTP/3 benchmark with nprpc behind npquicrouter (SHM + multi-worker eBPF).
  compare         Run both 'run' and 'run-router' back to back then view results.
  view            Summarize result files in benchmark/http_shootout/results.
  plot            Plot packet-length-over-time charts from .pcap files captured with
                  CAPTURE_PACKETS=1 (one window per server involved in the run).

Requirements:
  - nginx and/or docker
  - caddy and/or docker
  - oha (HTTP/1.1/HTTPS load)
  - h2load (HTTP/3 load), or run ./benchmark/http_shootout/build_h2load.sh
  - tcpdump + python3 with matplotlib/scapy (only for CAPTURE_PACKETS=1 / plot)
Environment:
  - NGINX_MODE=auto|system|docker (default: auto)
  - CADDY_MODE=auto|system|docker (default: auto)
  - BENCH_HOST=localhost|127.0.0.1 (default: localhost)
  - NPRPC_ROUTER_NUM_WORKERS=N (default: 4) router UDP worker threads
  - TRACE_SYSCALLS=1 (default: 0) attach strace to each server (system-mode
    nginx/caddy, plus nprpc) and report network syscall counts alongside the
    final results.
  - TRACE_SYSCALLS_FILTER=<syscalls> (default:
    sendmsg,sendmmsg,recvmsg,recvmmsg,sendto,recvfrom) comma list passed to
    strace -e trace=. nprpc's HTTP/3 path uses async_receive_from/
    async_send_to (recvfrom/sendto on Linux), not recvmsg, so the default
    covers both that and the TCP recvmsg/sendmsg path.
  - CAPTURE_PACKETS=1 (default: 0) capture each server's UDP/QUIC traffic
    with tcpdump during 'run'/'run-router' (one .pcap per server, named
    <server>-<port>.pcap in the results dir). Needs passwordless sudo for
    tcpdump (see NOPASSWD sudoers entry). View with '$0 plot' afterwards.
  - PACKET_PLOT_SCRIPT=path (default: benchmark/http_shootout/plot_packets.py)
  - NPRPC_ROUTER_BIN=path (default: BUILD_DIR/npquicrouter/npquicrouter)
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
  capture-environment)
    capture_environment_report
    write_summary
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
  run-router)
    run_suite_with_router
    ;;
  compare)
    run_suite
    run_suite_with_router
    python3 "$ROOT_DIR/benchmark/http_shootout/view_results.py" "$RESULTS_DIR"
    ;;
  view)
    python3 "$ROOT_DIR/benchmark/http_shootout/view_results.py" "$RESULTS_DIR"
    ;;
  plot)
    plot_packet_captures
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
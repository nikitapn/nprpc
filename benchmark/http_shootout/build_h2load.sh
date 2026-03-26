#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
NGHTTP2_DIR="$ROOT_DIR/third_party/nghttp2"
BUILD_DIR="$NGHTTP2_DIR/build"
REPO_URL="${NGHTTP2_REPO_URL:-https://github.com/nghttp2/nghttp2.git}"

log() {
  printf '[build_h2load] %s\n' "$*"
}

require_cmd() {
  command -v "$1" >/dev/null 2>&1 || {
    echo "Required command not found: $1" >&2
    exit 1
  }
}

require_cmd git
require_cmd cmake

if ! git -C "$NGHTTP2_DIR" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  log "Cloning nghttp2 into third_party/nghttp2"
  git clone --depth 1 "$REPO_URL" "$NGHTTP2_DIR"
else
  log "Using existing nghttp2 checkout at $NGHTTP2_DIR"
fi

cd "$NGHTTP2_DIR"

log "Initializing shallow submodules"
git submodule update --init --depth 1

log "Configuring h2load build"
cmake -S . -B "$BUILD_DIR" \
  -DENABLE_APP=ON \
  -DENABLE_HTTP3=ON \
  -DENABLE_ASIO_LIB=OFF \
  -DENABLE_EXAMPLES=OFF \
  -DENABLE_PYTHON_BINDINGS=OFF \
  -DENABLE_FAILMALLOC=OFF

log "Building h2load"
cmake --build "$BUILD_DIR" -j"$(nproc)" --target h2load

log "Built: $BUILD_DIR/src/h2load"
#!/bin/bash
# Run the HTTP3 server example in Docker

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SWIFT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
PROJECT_ROOT="$(cd "$SWIFT_ROOT/.." && pwd)"

docker run --rm -it \
  -v "$PROJECT_ROOT:/workspace" \
  -w /workspace/nprpc_swift \
  -p 3000:3000 \
  nprpc-swift-ubuntu \
  bash -c 'LD_LIBRARY_PATH=/workspace/.build_ubuntu_swift ./.build/x86_64-unknown-linux-gnu/debug/http3-server'

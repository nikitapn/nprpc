#!/bin/bash
# Run Swift tests in Docker container

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NPRPC_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

docker run --rm -v         \
  "$NPRPC_ROOT:/workspace" \
   nprpc-swift-ubuntu bash -c '
    set -e
    cd /workspace/nprpc_swift
    LD_LIBRARY_PATH=/workspace/.build_ubuntu_swift swift test
   '

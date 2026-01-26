#!/bin/bash
# Build script for nprpc_swift that sets up proper environment for linking

set -e

# # Get absolute paths
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NPRPC_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$NPRPC_ROOT/.build_ubuntu_swift"

# Check if libnprpc.so exists
if [ ! -f "$BUILD_DIR/libnprpc.so" ]; then
  echo "Error: libnprpc.so not found at $BUILD_DIR"
  echo "Please build nprpc with Swift's Clang 17 first:"
  # echo "  cd $NPRPC_ROOT && ./configure_swift_clang17.sh && cmake --build .build_swift_clang17 -j\$(nproc)"
  exit 1
fi

docker run --rm -v         \
  "$NPRPC_ROOT:/workspace" \
   nprpc-swift-ubuntu bash -c '
    set -e
    cd /workspace/nprpc_swift
    LD_LIBRARY_PATH=/workspace/.build_ubuntu_swift
    swift build
   '
echo "✅ Swift package built successfully."
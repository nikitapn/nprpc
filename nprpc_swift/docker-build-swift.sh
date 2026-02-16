#!/bin/bash
# Convenience build script to build Swift package linking against nprpc built with Swift's Clang 17 

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

# Check for --test flag
RUN_TESTS=false
if [ "$1" = "--test" ]; then
  RUN_TESTS=true
fi

if [ "$RUN_TESTS" = true ]; then
  docker run --rm -v         \
    "$NPRPC_ROOT:/workspace" \
     nprpc-swift-ubuntu bash -c '
      set -e
      cd /workspace/nprpc_swift
      export LD_LIBRARY_PATH=/workspace/.build_ubuntu_swift
      swift build
      set +e
      # Use stdbuf to disable output buffering so we see print() before timeout kills process
      stdbuf -oL -eL timeout 15 swift test --filter testStreams 2>&1
      code=$?
      test $code -eq 0 && echo "✅ Stream tests passed" || echo "❌ Stream tests failed with code $code"
     '
else
  docker run --rm -v         \
    "$NPRPC_ROOT:/workspace" \
     nprpc-swift-ubuntu bash -c '
      set -e
      cd /workspace/nprpc_swift
      export LD_LIBRARY_PATH=/workspace/.build_ubuntu_swift
      swift build
     '
  echo "✅ Swift package built successfully."
fi
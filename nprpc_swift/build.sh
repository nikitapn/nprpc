#!/bin/bash
# Build script for nprpc_swift that sets up proper environment for linking

set -e

# Get absolute paths
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NPRPC_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$NPRPC_ROOT/.build_swift_clang17"

# Check if libnprpc.so exists
if [ ! -f "$BUILD_DIR/libnprpc.so" ]; then
    echo "Error: libnprpc.so not found at $BUILD_DIR"
    echo "Please build nprpc with Swift's Clang 17 first:"
    echo "  cd $NPRPC_ROOT && ./configure_swift_clang17.sh && cmake --build .build_swift_clang17 -j\$(nproc)"
    exit 1
fi

# Set up environment for Swift build
export CPATH="$NPRPC_ROOT/include:$BUILD_DIR/include"
export LIBRARY_PATH="$BUILD_DIR"
export LD_LIBRARY_PATH="$BUILD_DIR:$LD_LIBRARY_PATH"

echo "Building nprpc_swift with environment:"
echo "  CPATH=$CPATH"
echo "  LIBRARY_PATH=$LIBRARY_PATH"
echo ""

cd "$SCRIPT_DIR"

# Note: Swift 6.2.3 uses Clang 17, which is compatible with system libstdc++
# Don't use -stdlib=libc++ as it tries to use system libc++ headers (for Clang 21+)
# Swift's embedded Clang 17 works fine with GCC's libstdc++
swift build "$@"

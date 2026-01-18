#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/.build_swift_clang17"
BOOST_INSTALL_DIR="$SCRIPT_DIR/.build_swift_clang17/boost_install"

# Swift's embedded Clang 17
export CC=/usr/lib/swift/bin/clang-17
export CXX=/usr/lib/swift/bin/clang-17

echo "Configuring NPRPC with Swift's Clang 17..."
echo "CC:  $CC"
echo "CXX: $CXX"

# Check if local Boost exists
CMAKE_ARGS=(
    -S "$SCRIPT_DIR"
    -B "$BUILD_DIR"
    -DCMAKE_BUILD_TYPE=RelWithDebInfo
    -DCMAKE_C_COMPILER=/usr/lib/swift/bin/clang-17
    -DCMAKE_CXX_COMPILER=/usr/lib/swift/bin/clang-17
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    -DNPRPC_BUILD_TESTS=ON
    -DNPRPC_BUILD_TOOLS=ON
    -DNPRPC_BUILD_JS=OFF
    -DNPRPC_ENABLE_QUIC=OFF
    -DNPRPC_ENABLE_HTTP3=OFF
    -DNPRPC_ENABLE_SSR=OFF
    -DBUILD_SHARED_LIBS=ON
)

if [ -d "$BOOST_INSTALL_DIR" ]; then
    echo "Using local Boost installation: $BOOST_INSTALL_DIR"
    CMAKE_ARGS+=(
        -DBOOST_ROOT="$BOOST_INSTALL_DIR"
        -DBoost_NO_SYSTEM_PATHS=ON
    )
else
    echo "No local Boost found. Will use system Boost (may cause ABI issues)."
    echo "To build Boost with Clang 17, run: ./build_boost_clang17.sh"
fi

cmake "${CMAKE_ARGS[@]}"

echo ""
echo "Configuration complete!"
echo "Build directory: $BUILD_DIR"
echo ""
echo "To build, run:"
echo "  cmake --build $BUILD_DIR -j\$(nproc)"

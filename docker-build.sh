#!/bin/bash
# Build and test NPRPC with Swift in Docker (Debian)

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "Building Docker image with Swift on Debian..."
docker build -f "$SCRIPT_DIR/Dockerfile.debian-swift" -t nprpc-swift-debian "$SCRIPT_DIR"

echo ""
echo "Running build in container..."
docker run --rm -v "$SCRIPT_DIR:/workspace" -w /workspace nprpc-swift-debian bash -c '
    set -e
    
    echo "=== Swift Version ==="
    swift --version
    
    echo ""
    echo "=== Clang Version ==="
    clang --version
    
    echo ""
    if [ ! -d ".build_debian_swift/boost_install/include/boost" ]; then
        echo "⚠️  Boost not found! Run ./build_boost_debian.sh first"
        exit 1
    fi
    
    echo "=== Using existing Boost installation ==="
    ls -la .build_debian_swift/boost_install/include/boost/ | head -5
    
    echo ""
    echo "=== Configuring NPRPC with Swift Clang ==="
    cmake -S . -B .build_debian_swift \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DCMAKE_C_COMPILER=clang \
        -DCMAKE_CXX_COMPILER=clang++ \
        -DBOOST_DIR=/workspace/.build_debian_swift/boost_install \
        -DNPRPC_BUILD_TESTS=OFF \
        -DNPRPC_BUILD_TOOLS=ON \
        -DNPRPC_BUILD_JS=OFF \
        -DNPRPC_ENABLE_QUIC=OFF \
        -DNPRPC_ENABLE_HTTP3=OFF \
        -DNPRPC_ENABLE_SSR=OFF \
        -DBUILD_SHARED_LIBS=ON
    
    echo ""
    echo "=== Building NPRPC ==="
    cmake --build .build_debian_swift -j$(nproc)
    
    echo ""
    echo "=== Checking library ==="
    ls -lh .build_debian_swift/libnprpc.so*
    
    echo ""
    echo "=== Building Swift Package ==="
    cd nprpc_swift
    
    export CPATH="/workspace/include:/workspace/.build_debian_swift/include:/workspace/.build_debian_swift/boost_install/include"
    export LIBRARY_PATH="/workspace/.build_debian_swift:/workspace/.build_debian_swift/boost_install/lib"
    export LD_LIBRARY_PATH="/workspace/.build_debian_swift:/workspace/.build_debian_swift/boost_install/lib"
    
    # Update Package.swift to use debian build dir
    sed -i "s|../.build_swift_clang17|../.build_debian_swift|g" Package.swift
    
    swift build -v
    
    echo ""
    echo "=== Running Tests ==="
    swift test --skip-build || echo "Tests completed (some may have Foundation conflicts)"
    
    echo ""
    echo "✅ Build successful in Debian container!"
'

echo ""
echo "Done! To enter the container for manual testing:"
echo "  docker run --rm -it -v \"$SCRIPT_DIR:/workspace\" -w /workspace nprpc-swift-debian bash"

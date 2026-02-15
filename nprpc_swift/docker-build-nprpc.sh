#!/bin/bash
# Build NPRPC library for Swift usage in Docker (Ubuntu + Swift's Clang)
# Run from nprpc_swift/ directory

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

REBUILD_DOCKER=false
if [ "$1" == "--rebuild" ]; then
    REBUILD_DOCKER=true
fi

DOCKER_IMAGE_NAME="nprpc-swift-ubuntu"
IS_DOCKER_BUILT=$(docker images -q "$DOCKER_IMAGE_NAME")

if [ -z "$IS_DOCKER_BUILT" ] || [ "$REBUILD_DOCKER" = true ]; then
    echo "Docker image '$DOCKER_IMAGE_NAME' not found or rebuild requested. Building..."
    docker build -f "$SCRIPT_DIR/Dockerfile" \
        --build-arg USER_ID=$(id -u) \
        --build-arg GROUP_ID=$(id -g) \
        --build-arg USERNAME=$(id -un) \
        -t nprpc-swift-ubuntu "$PROJECT_ROOT"
else
    echo "Docker image '$DOCKER_IMAGE_NAME' already exists. Skipping build."
fi

echo ""
echo "Running build in container..."
docker run --rm -v "$PROJECT_ROOT:/workspace" -w /workspace nprpc-swift-ubuntu bash -c '
    set -e
    
    echo "=== Swift Version ==="
    swift --version
    
    echo ""
    echo "=== Clang Version ==="
    clang --version
    
    echo ""
    if [ ! -d ".build_ubuntu_swift/boost_install/include/boost" ]; then
        echo "⚠️  Boost not found! Run ./docker-build-boost.sh first"
        exit 1
    fi
    
    echo "=== Using existing Boost installation ==="
    ls -la .build_ubuntu_swift/boost_install/include/boost/ | head -5
    
    echo ""
    echo "=== Configuring NPRPC with Swift Clang ==="
    cmake -S . -B .build_ubuntu_swift \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DCMAKE_C_COMPILER=clang \
        -DCMAKE_CXX_COMPILER=clang++ \
        -DBOOST_DIR=/workspace/.build_ubuntu_swift/boost_install \
        -DNPRPC_BUILD_TESTS=OFF \
        -DNPRPC_BUILD_TOOLS=ON \
        -DNPRPC_BUILD_JS=OFF \
        -DNPRPC_ENABLE_QUIC=OFF \
        -DNPRPC_ENABLE_HTTP3=OFF \
        -DNPRPC_ENABLE_SSR=OFF \
        -DBUILD_SHARED_LIBS=ON
    
    echo ""
    echo "=== Building NPRPC ==="
    cmake --build .build_ubuntu_swift -j$(nproc)
    
    echo ""
    echo "=== Checking library ==="
    ls -lh .build_ubuntu_swift/libnprpc.so*
    
    echo ""
    echo "=== Building Swift Package ==="
    cd nprpc_swift
    
    export CPATH="/workspace/include:/workspace/.build_ubuntu_swift/include:/workspace/.build_ubuntu_swift/boost_install/include"
    export LIBRARY_PATH="/workspace/.build_ubuntu_swift:/workspace/.build_ubuntu_swift/boost_install/lib"
    export LD_LIBRARY_PATH="/workspace/.build_ubuntu_swift:/workspace/.build_ubuntu_swift/boost_install/lib"
    
    swift build -v
    
    # echo ""
    # echo "=== Running Tests ==="
    # swift test || echo "Tests completed"
    
    echo ""
    echo "✅ Build successful in Ubuntu container!"
'

echo ""
echo "Done! To enter the container for manual testing:"
echo "  docker run --rm -it -v \"$SCRIPT_DIR:/workspace\" -w /workspace nprpc-swift-ubuntu bash"

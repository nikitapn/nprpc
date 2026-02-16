#!/bin/bash
# Build Boost 1.89.0 with Swift's Clang in Docker
# This only needs to be run once before docker-build-nprpc.sh
# Run from nprpc_swift/ directory

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

echo "Building Boost in Docker container..."
docker run --rm -v "$PROJECT_ROOT:/workspace" -w /workspace nprpc-swift-ubuntu bash -c '
    set -e
    
    echo "=== Downloading Boost 1.89.0 ==="
    if [ ! -d "third_party/boost_1_89_0" ]; then
        mkdir -p third_party
        cd third_party
        wget https://archives.boost.io/release/1.89.0/source/boost_1_89_0.tar.gz
        tar xzf boost_1_89_0.tar.gz
        rm boost_1_89_0.tar.gz
        cd ..
    fi
    
    cd third_party/boost_1_89_0
    
    echo "=== Cleaning previous build ==="
    rm -rf .build_ubuntu_swift/boost_install
    rm -f ./b2 ./bjam
    
    echo "=== Bootstrapping Boost ==="
    ./bootstrap.sh --with-toolset=clang
    
    cat > user-config.jam <<EOF
using clang : : clang++ ;
EOF
    
    echo "=== Building and installing Boost ==="
    ./b2 --user-config=user-config.jam \
        toolset=clang \
        cxxstd=23 \
        variant=release \
        link=shared \
        threading=multi \
        --prefix=/workspace/.build_ubuntu_swift/boost_install \
        --with-system \
        --with-thread \
        --with-filesystem \
        --with-program_options \
        --with-process \
        -j$(nproc) \
        install
    
    echo ""
    echo "=== Verifying Boost installation ==="
    ls -la /workspace/.build_ubuntu_swift/boost_install/include/boost/ | head -20
    ls -la /workspace/.build_ubuntu_swift/boost_install/lib/
    
    echo ""
    echo "✅ Boost build complete!"
'

echo ""
echo "Boost installed to: .build_ubuntu_swift/boost_install"

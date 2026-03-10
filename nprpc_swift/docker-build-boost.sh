#!/bin/bash
# Build Boost 1.89.0 and OpenSSL 3.6.1 (QUIC-capable) with Swift's Clang in Docker
# This only needs to be run once before docker-build-nprpc.sh
# Run from nprpc_swift/ directory

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

DOCKER_IMAGE_NAME="nprpc-swift-ubuntu"

echo "Building Boost and OpenSSL in Docker container..."
docker run --rm -v "$PROJECT_ROOT:/workspace" -w /workspace ${DOCKER_IMAGE_NAME} bash -c '
    set -e

    echo "=== Building OpenSSL 3.6.1 with QUIC support ==="
    if [ ! -f ".build_ubuntu_swift/openssl_install/lib/libssl.so" ]; then
        if [ ! -d "third_party/openssl-3.6.1" ]; then
            mkdir -p third_party
            cd third_party
            wget https://www.openssl.org/source/openssl-3.6.1.tar.gz
            tar xzf openssl-3.6.1.tar.gz
            rm openssl-3.6.1.tar.gz
            cd ..
        fi
        mkdir -p .build_ubuntu_swift/openssl_install
        cd third_party/openssl-3.6.1
        ./config \
            --prefix=/workspace/.build_ubuntu_swift/openssl_install \
            --openssldir=/workspace/.build_ubuntu_swift/openssl_install/ssl \
            --libdir=lib \
            CC=clang \
            shared \
            no-tests
        make -j$(nproc)
        make install_sw
        cd ../..
        echo "✅ OpenSSL 3.6.1 build complete!"
    else
        echo "=== Using existing OpenSSL installation ==="
    fi

    echo ""
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
echo "OpenSSL installed to: .build_ubuntu_swift/openssl_install"

#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BOOST_VERSION="1.89.0"
BOOST_VERSION_UNDERSCORE="${BOOST_VERSION//./_}"
BOOST_DIR="$SCRIPT_DIR/third_party/boost_${BOOST_VERSION_UNDERSCORE}"
BOOST_INSTALL_DIR="$SCRIPT_DIR/.build_swift_clang17/boost_install"

# Swift's Clang 17
CLANG_BIN="/usr/lib/swift/bin/clang-17"
CLANGXX_BIN="/usr/lib/swift/bin/clang-17"

echo "============================================"
echo "Building Boost ${BOOST_VERSION} with Swift's Clang 17"
echo "============================================"
echo ""
echo "Compiler: $CLANGXX_BIN"
echo "Install dir: $BOOST_INSTALL_DIR"
echo ""

# Download Boost if not present
if [ ! -d "$BOOST_DIR" ]; then
    echo "Downloading Boost ${BOOST_VERSION}..."
    mkdir -p "$SCRIPT_DIR/third_party"
    cd "$SCRIPT_DIR/third_party"
    
    BOOST_TAR="boost_${BOOST_VERSION_UNDERSCORE}.tar.gz"
    wget "https://archives.boost.io/release/${BOOST_VERSION}/source/${BOOST_TAR}" -O "$BOOST_TAR"
    tar xzf "$BOOST_TAR"
    rm "$BOOST_TAR"
    echo "Boost downloaded and extracted to $BOOST_DIR"
else
    echo "Boost already downloaded at $BOOST_DIR"
fi

cd "$BOOST_DIR"

# Bootstrap Boost build system
if [ ! -f "./b2" ]; then
    echo ""
    echo "Bootstrapping Boost..."
    ./bootstrap.sh \
        --with-toolset=clang \
        --prefix="$BOOST_INSTALL_DIR"
fi

# Create user-config.jam for Clang 17
# Need to specify link flags to use libstdc++
cat > user-config.jam <<EOF
using clang : 17 : $CLANGXX_BIN : <linkflags>"-lstdc++" ;
EOF

echo ""
echo "Building Boost libraries..."
echo "This will take several minutes..."
echo ""

# Build Boost with Clang 17
# Only build the libraries NPRPC needs: system, thread, filesystem (for SSR), process (for SSR)
./b2 \
    --user-config=user-config.jam \
    toolset=clang-17 \
    cxxstd=23 \
    variant=release \
    link=shared \
    threading=multi \
    --prefix="$BOOST_INSTALL_DIR" \
    --with-system \
    --with-thread \
    --with-filesystem \
    --with-process \
    -j$(nproc) \
    install

echo ""
echo "============================================"
echo "Boost built successfully!"
echo "============================================"
echo ""
echo "Installation directory: $BOOST_INSTALL_DIR"
echo ""
echo "To use this Boost build with NPRPC, reconfigure with:"
echo "  ./configure_swift_clang17.sh"
echo ""
echo "The configure script will automatically detect the local Boost installation."

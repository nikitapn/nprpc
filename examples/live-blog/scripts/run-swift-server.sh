#!/bin/bash
# Run the Swift server inside the nprpc-dev Docker container.
#
# Usage:
#   ./run_swift_server.sh              # release build
#   ./run_swift_server.sh --debug      # debug build
#   ./run_swift_server.sh debug        # gdb session (same as --debug)

set -e

ROOT_DIR="$(dirname $(readlink -e ${BASH_SOURCE[0]}))/.." # live-blog/ folder
SERVER_EXE_NAME="LiveBlogServer"
DOCKER_IMAGE="nprpc-dev:latest"
BUILD_CONFIG="release"
HOSTNAME_ARG="localhost"
PORT_ARG="8443"

for arg in "$@"; do
    case $arg in
        --debug|debug) BUILD_CONFIG="debug" ;;
    esac
done

BINARY="/app/swift/.build/$BUILD_CONFIG/$SERVER_EXE_NAME"

if [ ! -f "$ROOT_DIR/swift/.build/$BUILD_CONFIG/$SERVER_EXE_NAME" ]; then
    echo "Binary not found: swift/.build/$BUILD_CONFIG/$SERVER_EXE_NAME"
    echo "Run ./build_swift_server.sh first."
    exit 1
fi

echo "Starting $SERVER_EXE_NAME Swift server ($BUILD_CONFIG) inside Docker..."
echo "  Image   : $DOCKER_IMAGE"
echo "  Hostname: $HOSTNAME_ARG  Port: $PORT_ARG"
echo ""

DOCKER_CMD=(
    docker run --rm -it
    --user "$(id -u):$(id -g)"
    --name live-blog-swift

    # Mount project sub-trees the server needs at runtime
    -v "$ROOT_DIR/swift":/app/swift:ro
    -v "$ROOT_DIR/../../certs":/app/certs:ro
    -v "$ROOT_DIR/client/build":/app/runtime-www:rw # We need rw for writing host.json

    # Expose the RPC/HTTP port
    -p "${PORT_ARG}:${PORT_ARG}/tcp"
    -p "${PORT_ARG}:${PORT_ARG}/udp"    # HTTP/3 (QUIC) uses UDP

    -w /app
    "$DOCKER_IMAGE"
    "$BINARY"
)

if [ "$BUILD_CONFIG" = "debug" ]; then
    DOCKER_CMD+=(gdb --args)
fi

exec "${DOCKER_CMD[@]}"

# To test node start up issues
# docker run --rm -v /home/nikita/projects/nprpc/examples/live-blog/client/build:/app/runtime-www:ro -w /app nprpc-dev:latest env -i NPRPC_CHANNEL_ID=test-channel node /app/runtime-www/index.js
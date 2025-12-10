#!/bin/bash

# BT (Build Target) helper script

source .env

if [ -z "$1" ]; then
    echo "Usage: $0 <build-target>"
    echo "Example build targets: nprpc_server_test, npnameserver"
    exit 1
fi

cmake --build $BUILD_DIR --target "$1" -j$(nproc)
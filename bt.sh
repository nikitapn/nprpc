#!/bin/bash

# BT (Build Target) helper script

if [ -z "$1" ]; then
    echo "Usage: $0 <build-target>"
    echo "Example build targets: nprpc_server_test, npnameserver"
    exit 1
fi

cmake --build .build_release --target "$1" -j$(nproc)
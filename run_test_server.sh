#!/bin/bash

set -e

cd /home/nikita/projects/nprpc

killall nprpc_server_test npnameserver 2>/dev/null || true
cmake --build .build_release --target nprpc_server_test -j$(nproc)
.build_release/test/nprpc_server_test
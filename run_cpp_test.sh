#!/bin/env bash

set -e

cmake --build .build_relwith_debinfo --target=nprpc_test -j$(nproc)
# pkill -9 npnameserver 2>/dev/null || true
./.build_relwith_debinfo/test/nprpc_test $@
# gdb --args ./.build_relwith_debinfo/test/nprpc_test --gtest_filter="*TestBasic*"
#!/bin/env bash

set -e
cmake --build .build_relwith_debinfo --target=nprpc_test -j$(nproc)
set +e
pkill -9 npnameserver 2>/dev/null

timeout 10 ./.build_relwith_debinfo/test/nprpc_test $@
# gdb --args ./.build_relwith_debinfo/test/nprpc_test --gtest_filter="*TestBasic*"
echo "Tests exited with code $?"
#!/bin/env bash

set -e
cmake --build .build_relwith_debinfo --target=nprpc_test -j$(nproc)
set +e
pkill -9 npnameserver 2>/dev/null

timeout 10 ./.build_relwith_debinfo/test/nprpc_test $@
# gdb --args ./.build_relwith_debinfo/test/nprpc_test --gtest_filter="*TestBasic*"
echo "Tests exited with code $?"


# Debugging stucked thread during cleanup after test completion
# pkill -9 npnameserver 2>/dev/null; gdb -batch -ex run -ex "thread apply all bt" -ex quit --args ./.build_relwith_debinfo/test/nprpc_test --gtest_filter="*TestObjectStream*" > /tmp/gdb_obj_stream.log 2>&1; echo "done"
# then killall -11 nprpc_test to trigger the SIGSEGV and get the backtrace in /tmp/gdb_obj_stream.log for Claude to analyze.
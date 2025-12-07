#!/bin/bash

cmake --build .build_release --target=npidl -j$(nproc) && \
  ./.build_release/npidl/npidl --cpp --output-dir /tmp test/idl/nprpc_test.npidl && \
  cp /tmp/nprpc_test.cpp .build_release/nprpc_test_stub/src/gen && \
  cp /tmp/nprpc_test.hpp .build_release/nprpc_test_stub/src/gen/include
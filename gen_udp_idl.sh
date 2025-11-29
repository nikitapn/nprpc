#!/bin/bash

cmake --build .build_release --target=npidl -j$(nproc) && \
  ./.build_release/npidl/npidl --cpp --output-dir /tmp test/idl/test_udp.npidl && \
  cp /tmp/test_udp.cpp .build_release/nprpc_test_stub/src/gen && \
  cp /tmp/test_udp.hpp .build_release/nprpc_test_stub/src/gen/include
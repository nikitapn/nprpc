#!/bin/bash

BUILD_DIR=.build_relwith_debinfo

cmake --build $BUILD_DIR --target=npidl -j$(nproc) && \
  "./${BUILD_DIR}/npidl/npidl" --cpp --output-dir /tmp test/idl/nprpc_test.npidl && \
  cp /tmp/nprpc_test.cpp "${BUILD_DIR}/nprpc_test_stub/src/gen" && \
  cp /tmp/nprpc_test.hpp "${BUILD_DIR}/nprpc_test_stub/src/gen/include"
#!/bin/bash

BUILD_TYPE=$1

if [ "$BUILD_TYPE" == "Debug" ]; then
  BUILD_DIR=".build_debug"
elif [ "$BUILD_TYPE" == "RelWithDebInfo" ]; then
  BUILD_DIR=".build_relwith_debinfo"
else
  BUILD_TYPE="Release"
  BUILD_DIR=".build_release"
fi

cmake -S . -B $BUILD_DIR \
  -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
  -DNPRPC_BUILD_TESTS=ON \
  -DNPRPC_BUILD_TOOLS=ON \
  -DNPRPC_ENABLE_QUIC=ON \
  -DNPRPC_ENABLE_HTTP3=ON \
  -DNPRPC_HTTP3_BACKEND=nghttp3 \
  -DNPRPC_ENABLE_SSR=ON \
  -DNPRPC_BUILD_EXAMPLES=ON

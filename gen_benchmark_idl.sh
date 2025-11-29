#!/bin/bash

cmake --build .build_release --target=npidl -j$(nproc) && \
  ./.build_release/npidl/npidl --cpp --output-dir /tmp benchmark/idl/nprpc_benchmark.npidl && \
  cp /tmp/nprpc_benchmark.cpp .build_release/nprpc_benchmark_stub/src/gen && \
  cp /tmp/nprpc_benchmark.hpp .build_release/nprpc_benchmark_stub/src/gen/include
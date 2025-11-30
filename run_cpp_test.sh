#!/bin/env bash

cmake --build .build_release --target=nprpc_test -j$(nproc) 2>&1 | tail -3

./.build_release/test/nprpc_test $@
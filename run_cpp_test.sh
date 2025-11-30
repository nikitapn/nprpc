#!/bin/env bash

cmake --build .build_release --target=nprpc_test -j$(nproc)

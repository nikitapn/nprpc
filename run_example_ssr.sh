#!/bin/bash

set -e

cd /home/nikita/projects/nprpc

cmake --build .build_release --target example_ssr_server -j$(nproc)
.build_release/examples/ssr-svelte-app/server/example_ssr_server
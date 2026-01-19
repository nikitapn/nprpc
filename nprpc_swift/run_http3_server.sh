#!/bin/bash
# Run the HTTP3 server example in Docker

docker run --rm -it \
  -v "$(pwd):/workspace" \
  -w /workspace/nprpc_swift \
  -p 3000:3000 \
  nprpc-swift-ubuntu \
  bash -c "LD_LIBRARY_PATH=/workspace/.build_ubuntu_swift ./.build/x86_64-unknown-linux-gnu/debug/http3-server"

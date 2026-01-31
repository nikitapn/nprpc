#!/bin/bash

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

cd "$SCRIPT_DIR/.."

NPIDL=.build_relwith_debinfo/npidl/npidl

# Build npidl if needed
./bt.sh npidl > /dev/null

$NPIDL --swift \
  idl/nprpc_base.npidl \
  idl/nprpc_nameserver.npidl \
  --output-dir nprpc_swift/Sources/NPRPC/Generated

$NPIDL --swift \
  nprpc_swift/Tests/IDL/basic_test.npidl \
  --output-dir nprpc_swift/Tests/NPRPCTests/Generated
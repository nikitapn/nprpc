#!/bin/bash

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

cd "$SCRIPT_DIR/.."

NPIDL=${NPIDL:-.build_relwith_debinfo/npidl/npidl}

if [ "$NPIDL" == ".build_relwith_debinfo/npidl/npidl" ]; then
  # Build npidl if needed
  ./bt.sh npidl > /dev/null
fi

$NPIDL --swift \
  idl/nprpc_base.npidl \
  idl/nprpc_nameserver.npidl \
  --output-dir nprpc_swift/Sources/NPRPC/Generated

$NPIDL --swift \
  nprpc_swift/Tests/IDL/basic_test.npidl \
  --output-dir nprpc_swift/Tests/NPRPCTests/Generated

$NPIDL --swift \
  nprpc_swift/Tests/IDL/nprpc_test.npidl \
  --output-dir nprpc_swift/Tests/NPRPCTests/Generated

# gdb --batch \
#   --ex "set confirm off" \
#   --ex "catch throw" \
#   --ex "run" \
#   --ex "thread apply all bt" \
#   --ex "quit" \
#   --args $NPIDL --swift \
#     nprpc_swift/Tests/IDL/nprpc_test.npidl \
#     --output-dir nprpc_swift/Tests/NPRPCTests/Generated | tee gen_stubs.log

echo "Generated Swift stubs for NPRPC"
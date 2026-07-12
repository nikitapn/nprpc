#!/usr/bin/env bash
# Generate Swift stubs from IDL.
# Prefer: `just gen-swift-stubs` from the repo root.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT_DIR"

BUILD_DIR="${BUILD_DIR:-.build_relwith_debinfo}"
NPIDL="${NPIDL:-$BUILD_DIR/npidl/npidl}"

if [[ ! -x "$NPIDL" ]]; then
  cmake --build "$BUILD_DIR" --target npidl -j"$(nproc 2>/dev/null || echo 4)"
  NPIDL="$BUILD_DIR/npidl/npidl"
fi

"$NPIDL" --swift \
  idl/nprpc_base.npidl \
  idl/nprpc_nameserver.npidl \
  --output-dir nprpc_swift/Sources/NPRPC/Generated

"$NPIDL" --swift \
  nprpc_swift/Tests/IDL/basic_test.npidl \
  --output-dir nprpc_swift/Tests/NPRPCTests/Generated

"$NPIDL" --swift \
  nprpc_swift/Tests/IDL/nprpc_test.npidl \
  --output-dir nprpc_swift/Tests/NPRPCTests/Generated

echo "Generated Swift stubs for NPRPC"

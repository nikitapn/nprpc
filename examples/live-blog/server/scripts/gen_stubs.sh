#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SERVER_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ROOT_DIR="$(cd "$SERVER_DIR/../../.." && pwd)"

NPIDL_BIN="${NPIDL:-$ROOT_DIR/.build_relwith_debinfo/npidl/npidl}"
IDL_FILE="$SERVER_DIR/idl/live_blog.npidl"
OUT_DIR="$SERVER_DIR/.generated"

mkdir -p "$OUT_DIR"

echo "Generating live-blog stubs from $IDL_FILE"
"$NPIDL_BIN" "$IDL_FILE" --swift --ts --output-dir "$OUT_DIR"

echo "Generated files written to $OUT_DIR"
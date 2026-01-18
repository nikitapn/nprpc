#!/bin/bash
# Test script for generated Swift code (no runtime dependency)

set -e

cd "$(dirname "${BASH_SOURCE[0]}")"

echo "Testing generated Swift code (marshal/unmarshal)..."
echo ""

# Run tests for generated code only (no libnprpc.so needed)
swift test \
    -Xcc -stdlib=libc++ \
    -Xlinker -lc++ \
    -Xlinker -lc++abi \
    --filter GeneratedCodeTests \
    "$@"

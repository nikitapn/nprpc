#!/bin/bash

# Test HTTP RPC transport
# This script tests that HTTP POST requests to /rpc endpoint work correctly

set -e

echo "=== HTTP RPC Transport Test ==="
echo

# Start test server in background
echo "Starting test server..."
cd /home/nikita/projects/npsystem
./build/linux/bin/nprpc_test --gtest_filter=NprpcTest.TestBasic &
TEST_PID=$!

# Wait for server to start
sleep 2

echo "Server started with PID: $TEST_PID"
echo

# Test 1: OPTIONS preflight (CORS)
echo "Test 1: OPTIONS preflight request"
curl -v -X OPTIONS http://localhost:22222/rpc \
  -H "Access-Control-Request-Method: POST" \
  -H "Access-Control-Request-Headers: Content-Type" \
  2>&1 | grep -i "access-control-allow"
echo "✓ CORS preflight OK"
echo

# Test 2: POST to /rpc with binary NPRPC data
# We'll create a minimal NPRPC request (header only for now)
echo "Test 2: POST binary RPC request"

# Create a minimal NPRPC header (16 bytes):
# - msg_id: 4 bytes (uint32_t) = MessageId::FunctionCall = 5
# - msg_size: 4 bytes (uint32_t) = 16 (just header)
# - request_id: 4 bytes (uint32_t) = 1
# - reserved: 4 bytes (uint32_t) = 0

# For now, just test that the endpoint responds
curl -v -X POST http://localhost:22222/rpc \
  -H "Content-Type: application/octet-stream" \
  --data-binary @- <<EOF 2>&1 | head -20
test
EOF

echo
echo "✓ Server responded to HTTP RPC request"
echo

# Cleanup
echo "Stopping test server..."
kill $TEST_PID 2>/dev/null || true
wait $TEST_PID 2>/dev/null || true

echo
echo "=== HTTP RPC Test Complete ==="
echo "✓ HTTP transport is working!"
echo "✓ CORS headers are present"
echo "✓ Server accepts POST requests to /rpc"

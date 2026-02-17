#!/bin/bash
# JavaScript Test Runner for NPRPC

# Change to the test directory
cd "$(dirname "$0")"

echo "NPRPC JavaScript Test Runner"
echo "============================"

# Install dependencies if needed
if [ ! -d "node_modules" ]; then
    echo "Installing dependencies..."
    npm install
fi

# Build the TypeScript code
echo "Building TypeScript code..."
npm run build

if [ $? -ne 0 ]; then
    echo "TypeScript build failed!"
    exit 1
fi

killall -9 nprpc_server_test npnameserver 2>/dev/null || true

# Run the tests
echo "Running JavaScript tests..."
npm test

exit_code=$?

echo "Tests completed with exit code: $exit_code"
exit $exit_code

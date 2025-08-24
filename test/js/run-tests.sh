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

# Try to find and start the nameserver
echo "Looking for nameserver binary..."
NAMESERVER_PATHS=(
    "../../../build/linux/bin/npnameserver"
    "../../build/linux/bin/npnameserver" 
    "./build/linux/bin/npnameserver"
    "/home/nikita/projects/npsystem/build/linux/bin/npnameserver"
)

NAMESERVER_PID=""
for path in "${NAMESERVER_PATHS[@]}"; do
    if [ -x "$path" ]; then
        echo "Starting nameserver: $path"
        $path &
        NAMESERVER_PID=$!
        sleep 2
        break
    fi
done

if [ -z "$NAMESERVER_PID" ]; then
    echo "Warning: Could not find or start nameserver"
    echo "Tests may fail if nameserver is not running"
fi

# Cleanup function
cleanup() {
    echo "Cleaning up..."
    if [ ! -z "$NAMESERVER_PID" ]; then
        echo "Stopping nameserver (PID: $NAMESERVER_PID)"
        kill $NAMESERVER_PID 2>/dev/null
        wait $NAMESERVER_PID 2>/dev/null
    fi
}

# Set up trap to cleanup on exit
trap cleanup EXIT

# Run the tests
echo "Running JavaScript tests..."
npm test

exit_code=$?

echo "Tests completed with exit code: $exit_code"
exit $exit_code

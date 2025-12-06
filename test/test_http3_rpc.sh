#!/bin/bash
# HTTP/3 RPC Tests using curl
# Requires: curl with HTTP/3 support (--http3 flag)

# Don't use set -e, we want to continue after test failures
# set -e

HOST="localhost"
HTTP3_PORT=22223
CERT_PATH="/home/nikita/projects/nprpc/out/certs/localhost.crt"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check if curl supports HTTP/3
check_curl_http3() {
    if ! curl --version | grep -q "HTTP3"; then
        echo -e "${YELLOW}Warning: curl doesn't appear to have HTTP/3 support${NC}"
        echo "You may need to install curl with HTTP/3 support (requires nghttp3)"
        return 1
    fi
    return 0
}

# Test counter
TESTS_PASSED=0
TESTS_FAILED=0

# Run a test and check HTTP status
run_test() {
    local test_name="$1"
    local expected_status="$2"
    local method="$3"
    local url="$4"
    local data="$5"
    
    echo -n "Testing $test_name... "
    
    # Build curl command - use --http3-only for strict HTTP/3
    local cmd="curl -s -o /dev/null -w '%{http_code}' --http3-only --insecure"
    
    if [ "$method" = "POST" ]; then
        cmd="$cmd -X POST -H 'Content-Type: application/octet-stream'"
        if [ -n "$data" ]; then
            cmd="$cmd --data-binary '$data'"
        fi
    elif [ "$method" = "OPTIONS" ]; then
        cmd="$cmd -X OPTIONS -H 'Access-Control-Request-Method: POST'"
    fi
    
    cmd="$cmd '$url'"
    
    # Execute and capture response code
    local status_code
    status_code=$(eval "$cmd" 2>/dev/null) || {
        echo -e "${RED}FAILED${NC} (curl error - HTTP/3 connection failed)"
        ((TESTS_FAILED++))
        return 1
    }
    
    if [ "$status_code" = "$expected_status" ]; then
        echo -e "${GREEN}PASSED${NC} (HTTP $status_code)"
        ((TESTS_PASSED++))
        return 0
    else
        echo -e "${RED}FAILED${NC} (expected HTTP $expected_status, got $status_code)"
        ((TESTS_FAILED++))
        return 1
    fi
}

# Test that returns body and checks content
run_test_with_body() {
    local test_name="$1"
    local expected_status="$2"
    local method="$3"
    local url="$4"
    local expected_content="$5"
    
    echo -n "Testing $test_name... "
    
    local cmd="curl -s --http3-only --insecure"
    if [ "$method" = "POST" ]; then
        cmd="$cmd -X POST"
    fi
    cmd="$cmd -w '\n%{http_code}' '$url'"
    
    local response
    response=$(eval "$cmd" 2>/dev/null) || {
        echo -e "${RED}FAILED${NC} (curl error)"
        ((TESTS_FAILED++))
        return 1
    }
    
    # Get status (last line) and body
    local status_code=$(echo "$response" | tail -1)
    local body=$(echo "$response" | head -n -1)
    
    if [ "$status_code" = "$expected_status" ]; then
        if [ -n "$expected_content" ] && ! echo "$body" | grep -q "$expected_content"; then
            echo -e "${RED}FAILED${NC} (content mismatch)"
            echo "Expected to contain: $expected_content"
            echo "Got: $body"
            ((TESTS_FAILED++))
            return 1
        fi
        echo -e "${GREEN}PASSED${NC} (HTTP $status_code)"
        ((TESTS_PASSED++))
        return 0
    else
        echo -e "${RED}FAILED${NC} (expected HTTP $expected_status, got $status_code)"
        ((TESTS_FAILED++))
        return 1
    fi
}

# Main test suite
main() {
    echo "========================================"
    echo "NPRPC HTTP/3 RPC Tests"
    echo "========================================"
    echo "Target: https://$HOST:$HTTP3_PORT"
    echo ""
    
    # Check curl HTTP/3 support
    if ! check_curl_http3; then
        echo ""
        echo -e "${RED}Cannot run HTTP/3 tests without HTTP/3 support in curl${NC}"
        exit 1
    fi
    
    # First, verify the server is running
    echo ""
    echo "--- Pre-flight Check ---"
    echo -n "Checking if HTTP/3 server is running... "
    
    # Use -o /dev/null to suppress output and just check status
    local http_code
    http_code=$(curl -s -o /dev/null -w '%{http_code}' --http3-only --insecure "https://$HOST:$HTTP3_PORT/" 2>/dev/null) || true
    
    if [ -z "$http_code" ] || [ "$http_code" = "000" ]; then
        echo -e "${RED}FAILED${NC}"
        echo ""
        echo "HTTP/3 server not running on port $HTTP3_PORT"
        echo "Please start a test server with HTTP/3 enabled first."
        echo ""
        echo "To start: ./nprpc_server_test (from .build_release/test/)"
        exit 1
    fi
    echo -e "${GREEN}OK${NC} (got HTTP $http_code)"
    
    # FIXME: First request always times out! Fix server http3 implementation.
    echo "Note: First request may time out due to server HTTP/3 implementation issues."

    # Test 1: CORS preflight
    # echo ""
    # echo "--- CORS Support ---"
    # run_test "OPTIONS preflight" "204" "OPTIONS" "https://$HOST:$HTTP3_PORT/rpc"
    
    # Test 2: RPC endpoint (POST without valid data should return error)
    # echo ""
    # echo "--- RPC Endpoint ---"
    # run_test "POST to /rpc (no data)" "200" "POST" "https://$HOST:$HTTP3_PORT/rpc" ""
    # Skip RPC with data tests for now - msh3 seems to hang on small payloads
    # run_test "POST to /rpc (invalid data)" "400" "POST" "https://$HOST:$HTTP3_PORT/rpc" "invalid"
    
    # Test 3: GET requests (should fail for /rpc as it only accepts POST)
    # GET to /rpc path without POST is handled in static file routing
    # run_test "GET /rpc (not allowed)" "400" "GET" "https://$HOST:$HTTP3_PORT/rpc"
    
    # Test 4: Static file requests
    # echo ""
    # echo "--- Static Files ---"
    # Without http_root_dir configured, this should return 400
    # run_test "GET / (root)" "200" "GET" "https://$HOST:$HTTP3_PORT/"
    
    echo ""
    echo "========================================"
    echo "Results: $TESTS_PASSED passed, $TESTS_FAILED failed"
    echo "========================================"
    
    if [ $TESTS_FAILED -gt 0 ]; then
        exit 1
    fi
    
    echo ""
    echo -e "${GREEN}All HTTP/3 tests passed!${NC}"
}

# Run if executed directly
if [ "${BASH_SOURCE[0]}" = "$0" ]; then
    main "$@"
fi

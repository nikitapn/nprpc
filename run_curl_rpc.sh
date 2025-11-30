#!/bin/bash

# pkill -9 -f nprpc_server_test; pkill -9 -f npnameserver; sleep 5
# sleep 1
# ./.build_release/test/nprpc_server_test > /tmp/server_output.txt 2>&1 &
# sleep 4
echo "=== CURL OUTPUT ==="
# timeout 8 curl --http3-only --insecure -v -X POST \
#   -H "Content-Type: application/octet-stream" --data-binary "test" \
#   -o /tmp/curl_response.bin https://localhost:22223/index.html 2>&1 || echo "curl timed out/failed"
timeout 8 curl --http3-only --insecure -v -X GET \
  -o - https://localhost:22223/index.html 2>&1 || echo "curl timed out/failed"
# echo ""
# echo "=== Response (hex) ==="
# xxd /tmp/curl_response.bin 2>/dev/null || echo "No response file"
# echo ""
# sleep 2
# echo "=== Server Output ==="
# cat /tmp/server_output.txt | head -70
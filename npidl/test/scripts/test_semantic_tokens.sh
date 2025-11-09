#!/bin/bash

# Test semantic tokens functionality

LSP_SERVER="/home/nikita/projects/npsystem/build/linux/bin/npidl --lsp"
TEST_FILE="/home/nikita/projects/npsystem/nprpc/idl/nprpc_base.npidl"

# Read the file content
FILE_CONTENT=$(cat "$TEST_FILE")

# Create temporary file for communication
FIFO_IN=$(mktemp -u)
FIFO_OUT=$(mktemp -u)
mkfifo "$FIFO_IN"
mkfifo "$FIFO_OUT"

# Start LSP server
$LSP_SERVER < "$FIFO_IN" > "$FIFO_OUT" 2>&1 &
LSP_PID=$!

# Give server time to start
sleep 1

# Function to send JSON-RPC message
send_message() {
  local content="$1"
  local length=${#content}
  echo "Content-Length: $length"
  echo ""
  echo -n "$content"
}

# Function to read response
read_response() {
  # Read headers
  while IFS= read -r line; do
    line=$(echo "$line" | tr -d '\r')
    if [ -z "$line" ]; then
      break
    fi
    if [[ $line == Content-Length:* ]]; then
      CONTENT_LENGTH=${line#Content-Length: }
    fi
  done
  
  # Read content
  if [ -n "$CONTENT_LENGTH" ]; then
    dd bs=1 count=$CONTENT_LENGTH 2>/dev/null
  fi
}

exec 3>"$FIFO_IN"
exec 4<"$FIFO_OUT"

echo "Sending initialize..."
send_message '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"capabilities":{}}}' >&3

echo "Reading initialize response..."
read_response <&4 | jq .

echo ""
echo "Sending didOpen..."
# Escape the file content for JSON
ESCAPED_CONTENT=$(echo "$FILE_CONTENT" | jq -Rs .)
send_message "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{\"uri\":\"file://$TEST_FILE\",\"languageId\":\"npidl\",\"version\":1,\"text\":$ESCAPED_CONTENT}}}" >&3

sleep 1

echo ""
echo "Sending semantic tokens request..."
send_message "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"textDocument/semanticTokens/full\",\"params\":{\"textDocument\":{\"uri\":\"file://$TEST_FILE\"}}}" >&3

echo "Reading semantic tokens response..."
read_response <&4 | jq .

# Cleanup
exec 3>&-
exec 4<&-
kill $LSP_PID 2>/dev/null
rm -f "$FIFO_IN" "$FIFO_OUT"

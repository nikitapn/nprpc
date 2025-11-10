#!/usr/bin/env python3
import json
import subprocess
import sys

def send_lsp_message(proc, message):
    """Send a JSON-RPC message with proper headers"""
    content = json.dumps(message)
    header = f"Content-Length: {len(content)}\r\n\r\n"
    proc.stdin.write(header.encode('utf-8'))
    proc.stdin.write(content.encode('utf-8'))
    proc.stdin.flush()

def read_lsp_message(proc):
    """Read a JSON-RPC message with headers"""
    # Read headers
    headers = {}
    while True:
        line = proc.stdout.readline().decode('utf-8')
        if line == '\r\n' or line == '\n':
            break
        if ':' in line:
            key, value = line.split(':', 1)
            headers[key.strip()] = value.strip()
    
    # Read content
    content_length = int(headers.get('Content-Length', 0))
    if content_length > 0:
        content = proc.stdout.read(content_length).decode('utf-8')
        return json.loads(content)
    return None

# Start LSP server
lsp_server = subprocess.Popen(
    ['/home/nikita/projects/npsystem/build/linux/bin/npidl', '--lsp'],
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE
)

try:
    # Initialize
    print("Sending initialize...")
    send_lsp_message(lsp_server, {
        "jsonrpc": "2.0",
        "id": 1,
        "method": "initialize",
        "params": {"capabilities": {}}
    })
    
    response = read_lsp_message(lsp_server)
    print("Initialize response:", json.dumps(response, indent=2))
    
    # Check for semanticTokensProvider
    if response and 'result' in response:
        caps = response['result'].get('capabilities', {})
        semantic_tokens = caps.get('semanticTokensProvider')
        print("\nSemantic tokens support:", semantic_tokens)
    
    # Send didOpen
    print("\nSending didOpen...")
    test_file = "/home/nikita/projects/npsystem/nprpc/idl/nprpc_base.npidl"
    with open(test_file) as f:
        file_content = f.read()
    
    send_lsp_message(lsp_server, {
        "jsonrpc": "2.0",
        "method": "textDocument/didOpen",
        "params": {
            "textDocument": {
                "uri": f"file://{test_file}",
                "languageId": "npidl",
                "version": 1,
                "text": file_content
            }
        }
    })
    
    # Give it time to parse
    import time
    time.sleep(0.5)
    
    # Request semantic tokens
    print("\nRequesting semantic tokens...")
    send_lsp_message(lsp_server, {
        "jsonrpc": "2.0",
        "id": 2,
        "method": "textDocument/semanticTokens/full",
        "params": {
            "textDocument": {
                "uri": f"file://{test_file}"
            }
        }
    })
    
    # Read responses (might get diagnostics first)
    for _ in range(3):  # Try a few times
        response = read_lsp_message(lsp_server)
        print(f"Response: {json.dumps(response, indent=2)[:200]}...")
        if response and response.get('id') == 2:
            break
    
    # Decode first few tokens
    if response and 'result' in response and 'data' in response['result']:
        data = response['result']['data']
        print(f"\nTotal tokens: {len(data) // 5}")
        print("First 10 tokens (deltaLine, deltaCol, length, type, modifiers):")
        for i in range(min(10, len(data) // 5)):
            idx = i * 5
            token = data[idx:idx+5]
            print(f"  Token {i}: {token}")

    
finally:
    lsp_server.terminate()
    lsp_server.wait()

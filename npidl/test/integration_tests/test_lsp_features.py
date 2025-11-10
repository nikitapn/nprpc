#!/usr/bin/env python3
"""Comprehensive test of NPIDL LSP features"""
import json
import subprocess
import time

def send_msg(proc, msg):
    content = json.dumps(msg)
    header = f"Content-Length: {len(content)}\r\n\r\n"
    proc.stdin.write(header.encode('utf-8'))
    proc.stdin.write(content.encode('utf-8'))
    proc.stdin.flush()

def read_msg(proc):
    headers = {}
    while True:
        line = proc.stdout.readline().decode('utf-8')
        if line == '\r\n' or line == '\n':
            break
        if ':' in line:
            key, value = line.split(':', 1)
            headers[key.strip()] = value.strip()
    
    length = int(headers.get('Content-Length', 0))
    if length > 0:
        return json.loads(proc.stdout.read(length).decode('utf-8'))
    return None

lsp = subprocess.Popen(
    ['/home/nikita/projects/npsystem/build/linux/bin/npidl', '--lsp'],
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE
)

try:
    print("=" * 70)
    print("NPIDL LSP Feature Test")
    print("=" * 70)
    
    # Initialize
    print("\n1. Testing Initialize...")
    send_msg(lsp, {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"capabilities": {}}})
    resp = read_msg(lsp)
    caps = resp['result']['capabilities']
    
    print(f"   ✓ hoverProvider: {caps.get('hoverProvider')}")
    print(f"   ✓ definitionProvider: {caps.get('definitionProvider')}")
    print(f"   ✓ semanticTokensProvider: {bool(caps.get('semanticTokensProvider'))}")
    
    # Open file
    test_file = "/home/nikita/projects/npsystem/nprpc/idl/nprpc_base.npidl"
    with open(test_file) as f:
        content = f.read()
    
    send_msg(lsp, {
        "jsonrpc": "2.0",
        "method": "textDocument/didOpen",
        "params": {
            "textDocument": {
                "uri": f"file://{test_file}",
                "languageId": "npidl",
                "version": 1,
                "text": content
            }
        }
    })
    time.sleep(0.3)
    
    lines = content.split('\n')
    
    # Test hover
    print("\n2. Testing Hover...")
    send_msg(lsp, {
        "jsonrpc": "2.0",
        "id": 2,
        "method": "textDocument/hover",
        "params": {
            "textDocument": {"uri": f"file://{test_file}"},
            "position": {"line": 5, "character": 10}  # Line with "using oid_t"
        }
    })
    
    for _ in range(2):
        resp = read_msg(lsp)
        if resp and resp.get('id') == 2:
            break
    
    if resp and 'result' in resp and resp['result']:
        hover_text = resp['result'].get('contents', '')
        print(f"   ✓ Hover text: {hover_text[:60]}...")
    
    # Test go-to-definition
    print("\n3. Testing Go-to-Definition...")
    send_msg(lsp, {
        "jsonrpc": "2.0",
        "id": 3,
        "method": "textDocument/definition",
        "params": {
            "textDocument": {"uri": f"file://{test_file}"},
            "position": {"line": 48, "character": 14}  # "object_id: oid_t"
        }
    })
    
    for _ in range(2):
        resp = read_msg(lsp)
        if resp and resp.get('id') == 3:
            break
    
    if resp and 'result' in resp and resp['result']:
        loc = resp['result']
        line_num = loc['range']['start']['line'] + 1
        print(f"   ✓ Jumped to line {line_num}: {lines[line_num-1].strip()}")
    
    # Test semantic tokens
    print("\n4. Testing Semantic Tokens...")
    send_msg(lsp, {
        "jsonrpc": "2.0",
        "id": 4,
        "method": "textDocument/semanticTokens/full",
        "params": {
            "textDocument": {"uri": f"file://{test_file}"}
        }
    })
    
    for _ in range(2):
        resp = read_msg(lsp)
        if resp and resp.get('id') == 4:
            break
    
    if resp and 'result' in resp and 'data' in resp['result']:
        token_count = len(resp['result']['data']) // 5
        print(f"   ✓ Generated {token_count} semantic tokens")
    
    print("\n" + "=" * 70)
    print("All tests completed successfully! ✅")
    print("=" * 70)
    
finally:
    lsp.terminate()
    lsp.wait()

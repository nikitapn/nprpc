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
    headers = {}
    while True:
        line = proc.stdout.readline().decode('utf-8')
        if line == '\r\n' or line == '\n':
            break
        if ':' in line:
            key, value = line.split(':', 1)
            headers[key.strip()] = value.strip()
    
    content_length = int(headers.get('Content-Length', 0))
    if content_length > 0:
        content = proc.stdout.read(content_length).decode('utf-8')
        return json.loads(content)
    return None

lsp_server = subprocess.Popen(
    ['/home/nikita/projects/npsystem/build/linux/bin/npidl', '--lsp'],
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE
)

try:
    send_lsp_message(lsp_server, {
        "jsonrpc": "2.0",
        "id": 1,
        "method": "initialize",
        "params": {"capabilities": {}}
    })
    read_lsp_message(lsp_server)
    
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
    
    import time
    time.sleep(0.5)
    
    lines = file_content.split('\n')
    
    # Find line with "object_id: oid_t;" in ObjectId struct
    # Line 6 has: using oid_t = u64;
    # Around line 68 has: object_id: oid_t;
    
    for i, line in enumerate(lines):
        if 'object_id: oid_t' in line and 'ObjectId' in '\n'.join(lines[max(0,i-5):i]):
            print(f"Found field at line {i+1}: {line.strip()}")
            # Click on "oid_t" part (after the colon and space)
            col = line.index('oid_t')
            
            print(f"\nTesting go-to-definition at line {i+1}, char {col+1}")
            
            send_lsp_message(lsp_server, {
                "jsonrpc": "2.0",
                "id": 2,
                "method": "textDocument/definition",
                "params": {
                    "textDocument": {"uri": f"file://{test_file}"},
                    "position": {"line": i, "character": col}
                }
            })
            
            for _ in range(3):
                response = read_lsp_message(lsp_server)
                if response and response.get('id') == 2:
                    break
            
            if response and 'result' in response:
                result = response['result']
                if result and result != "null" and isinstance(result, dict):
                    r = result['range']
                    start_line = r['start']['line']
                    start_char = r['start']['character']
                    print(f"\n✓ Go-to-definition SUCCESS!")
                    print(f"  Jumped to: line {start_line + 1}, char {start_char + 1}")
                    print(f"  Definition: {lines[start_line].strip()}")
                    
                    if 'using oid_t' in lines[start_line]:
                        print(f"\n✅ CORRECT: Found the alias definition!")
                    else:
                        print(f"\n⚠️  Expected to find 'using oid_t', got: {lines[start_line]}")
                else:
                    print(f"\n✗ No definition found")
            break
    
finally:
    lsp_server.terminate()
    lsp_server.wait()

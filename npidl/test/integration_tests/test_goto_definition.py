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
    print(f"Initialize: {response.get('result', {}).get('capabilities', {}).get('definitionProvider')}")
    
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
    
    # Read the file to find test positions
    lines = file_content.split('\n')
    
    # Find a field with a type (e.g., "what: string;" in ExceptionCommFailure)
    # Line 14 is "  what: string;" (0-indexed: line 13)
    test_positions = [
        {"line": 13, "char": 8, "desc": "field 'what' in ExceptionCommFailure"},
        {"line": 18, "char": 5, "desc": "ExceptionObjectNotExist declaration"},
        {"line": 28, "char": 5, "desc": "DebugLevel enum value"},
    ]
    
    for test in test_positions:
        print(f"\n{'='*70}")
        print(f"Testing go-to-definition at line {test['line']}, char {test['char']}")
        print(f"Description: {test['desc']}")
        print(f"Context: {lines[test['line']].strip()}")
        
        send_lsp_message(lsp_server, {
            "jsonrpc": "2.0",
            "id": 2,
            "method": "textDocument/definition",
            "params": {
                "textDocument": {"uri": f"file://{test_file}"},
                "position": {"line": test['line'], "character": test['char']}
            }
        })
        
        # Read responses (might get diagnostics first)
        for _ in range(3):
            response = read_lsp_message(lsp_server)
            if response and response.get('id') == 2:
                break
        
        if response and 'result' in response:
            result = response['result']
            if result and result != "null":
                print(f"✓ Definition found:")
                if isinstance(result, dict) and 'range' in result:
                    r = result['range']
                    start_line = r['start']['line']
                    start_char = r['start']['character']
                    print(f"  Location: line {start_line + 1}, char {start_char + 1}")
                    print(f"  Text: {lines[start_line].strip()}")
                else:
                    print(f"  Result: {result}")
            else:
                print(f"✗ No definition found (expected for fundamental types)")
        else:
            print(f"✗ No response or error")
    
finally:
    lsp_server.terminate()
    lsp_server.wait()

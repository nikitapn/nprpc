#!/usr/bin/env python3

import json
import subprocess
import sys

def send_request(proc, request_id, method, params=None):
    """Send a JSON-RPC request"""
    request = {
        "jsonrpc": "2.0",
        "id": request_id,
        "method": method
    }
    if params is not None:
        request["params"] = params
    
    message = json.dumps(request)
    content = f"Content-Length: {len(message)}\r\n\r\n{message}"
    proc.stdin.write(content)
    proc.stdin.flush()

def send_notification(proc, method, params=None):
    """Send a JSON-RPC notification (no response expected)"""
    notification = {
        "jsonrpc": "2.0",
        "method": method
    }
    if params is not None:
        notification["params"] = params
    
    message = json.dumps(notification)
    content = f"Content-Length: {len(message)}\r\n\r\n{message}"
    proc.stdin.write(content)
    proc.stdin.flush()

def read_response(proc):
    """Read one JSON-RPC response"""
    # Read headers
    content_length = 0
    while True:
        line = proc.stdout.readline()
        if not line:
            return None
        line = line.strip()
        if not line:
            break
        if line.startswith("Content-Length: "):
            content_length = int(line[16:])
    
    if content_length == 0:
        return None
    
    # Read content
    content = proc.stdout.read(content_length)
    return json.loads(content)

def main():
    # Start the LSP server
    proc = subprocess.Popen(
        ["/home/nikita/projects/npsystem/build/linux/bin/npidl", "--lsp"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        bufsize=0
    )
    
    print("Testing parser error diagnostics...")
    print("=" * 70)
    
    # Initialize
    send_request(proc, 1, "initialize", {
        "processId": None,
        "rootUri": None,
        "capabilities": {}
    })
    response = read_response(proc)
    print(f"Initialize response: {response.get('result', {}).get('capabilities', {})}")
    
    send_notification(proc, "initialized")
    
    # Read test file
    with open("/tmp/test_parser_errors.npidl", "r") as f:
        content = f.read()
    
    print("\nTest file content:")
    for i, line in enumerate(content.split('\n'), 1):
        print(f"  Line {i:2}: {line}")
    
    # Open document with errors
    send_notification(proc, "textDocument/didOpen", {
        "textDocument": {
            "uri": "file:///tmp/test_parser_errors.npidl",
            "languageId": "npidl",
            "version": 1,
            "text": content
        }
    })
    
    # Wait for diagnostics notification
    print("\n" + "=" * 70)
    print("Waiting for diagnostics notification...")
    
    diagnostics_received = False
    for _ in range(5):  # Try up to 5 messages
        response = read_response(proc)
        if not response:
            break
        
        print(f"\nReceived: {json.dumps(response, indent=2)[:500]}...")
        
        if response.get("method") == "textDocument/publishDiagnostics":
            diagnostics_received = True
            params = response.get("params", {})
            diagnostics = params.get("diagnostics", [])
            
            print("\n" + "=" * 70)
            print(f"✅ Diagnostics received: {len(diagnostics)} errors")
            print("=" * 70)
            
            for i, diag in enumerate(diagnostics, 1):
                line = diag["range"]["start"]["line"]
                char = diag["range"]["start"]["character"]
                message = diag["message"]
                print(f"\nError {i}:")
                print(f"  Location: line {line+1}, column {char+1}")
                print(f"  Message: {message}")
            
            break
    
    if not diagnostics_received:
        print("\n❌ No diagnostics notification received!")
    
    # Shutdown
    send_request(proc, 2, "shutdown")
    read_response(proc)
    send_notification(proc, "exit")
    
    proc.terminate()
    proc.wait()

if __name__ == "__main__":
    main()

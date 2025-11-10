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
    
    print("Testing diagnostic range details...")
    print("=" * 70)
    
    # Initialize
    send_request(proc, 1, "initialize", {
        "processId": None,
        "rootUri": None,
        "capabilities": {}
    })
    response = read_response(proc)
    
    send_notification(proc, "initialized")
    
    # Read test file
    with open("/tmp/test_parser_errors.npidl", "r") as f:
        content = f.read()
    
    lines = content.split('\n')
    
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
    for _ in range(5):
        response = read_response(proc)
        if not response:
            break
        
        if response.get("method") == "textDocument/publishDiagnostics":
            params = response.get("params", {})
            diagnostics = params.get("diagnostics", [])
            
            print(f"\n✅ Received {len(diagnostics)} diagnostics\n")
            
            for i, diag in enumerate(diagnostics, 1):
                start_line = diag["range"]["start"]["line"]
                start_char = diag["range"]["start"]["character"]
                end_line = diag["range"]["end"]["line"]
                end_char = diag["range"]["end"]["character"]
                message = diag["message"]
                
                # Get the actual line content
                if start_line < len(lines):
                    line_content = lines[start_line]
                    
                    # Extract the highlighted portion
                    if end_char <= len(line_content):
                        highlighted = line_content[start_char:end_char]
                    else:
                        highlighted = line_content[start_char:]
                    
                    print(f"Error {i}: {message}")
                    print(f"  Line {start_line + 1}, chars {start_char}-{end_char}")
                    print(f"  Full line: {line_content}")
                    print(f"  Highlighted: '{highlighted}'")
                    print(f"  Range length: {end_char - start_char} characters")
                    print()
            
            break
    
    # Shutdown
    send_request(proc, 2, "shutdown")
    read_response(proc)
    send_notification(proc, "exit")
    
    proc.terminate()
    proc.wait()

if __name__ == "__main__":
    main()

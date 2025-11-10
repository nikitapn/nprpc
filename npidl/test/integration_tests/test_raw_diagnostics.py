#!/usr/bin/env python3

import json
import subprocess

def send_request(proc, request_id, method, params=None):
    request = {"jsonrpc": "2.0", "id": request_id, "method": method}
    if params is not None:
        request["params"] = params
    message = json.dumps(request)
    content = f"Content-Length: {len(message)}\r\n\r\n{message}"
    proc.stdin.write(content)
    proc.stdin.flush()

def send_notification(proc, method, params=None):
    notification = {"jsonrpc": "2.0", "method": method}
    if params is not None:
        notification["params"] = params
    message = json.dumps(notification)
    content = f"Content-Length: {len(message)}\r\n\r\n{message}"
    proc.stdin.write(content)
    proc.stdin.flush()

def read_response(proc):
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
    content = proc.stdout.read(content_length)
    return json.loads(content)

# Start LSP server
proc = subprocess.Popen(
    ["/home/nikita/projects/npsystem/build/linux/bin/npidl", "--lsp"],
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
    text=True,
    bufsize=0
)

# Initialize
send_request(proc, 1, "initialize", {"processId": None, "rootUri": None, "capabilities": {}})
read_response(proc)
send_notification(proc, "initialized")

# Read test file
with open("/tmp/test_parser_errors.npidl", "r") as f:
    content = f.read()

lines = content.split('\n')

# Open document
send_notification(proc, "textDocument/didOpen", {
    "textDocument": {
        "uri": "file:///tmp/test_parser_errors.npidl",
        "languageId": "npidl",
        "version": 1,
        "text": content
    }
})

# Wait for diagnostics
for _ in range(5):
    response = read_response(proc)
    if not response:
        break
    
    if response.get("method") == "textDocument/publishDiagnostics":
        diagnostics = response.get("params", {}).get("diagnostics", [])
        
        print("RAW DIAGNOSTIC DATA:")
        print(json.dumps(diagnostics, indent=2))
        print("\n" + "="*70 + "\n")
        
        for i, diag in enumerate(diagnostics, 1):
            start_line = diag["range"]["start"]["line"]
            start_char = diag["range"]["start"]["character"]
            end_char = diag["range"]["end"]["character"]
            
            if start_line < len(lines):
                line_content = lines[start_line]
                print(f"Error {i}: {diag['message']}")
                print(f"  Position: line={start_line+1}, start_col={start_char+1}, end_col={end_char}")
                print(f"  Line: '{line_content}'")
                print(f"        {' ' * start_char}{'↑' * (end_char - start_char)}")
                if start_char < len(line_content):
                    print(f"  At start_char: '{line_content[start_char]}'")
                if end_char <= len(line_content):
                    print(f"  Highlighted: '{line_content[start_char:end_char]}'")
                print()
        
        break

# Shutdown
send_request(proc, 2, "shutdown")
read_response(proc)
send_notification(proc, "exit")
proc.terminate()
proc.wait()

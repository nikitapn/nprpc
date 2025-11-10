#!/usr/bin/env python3
"""Debug: Check what's in the position index"""
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

# Create a test npidl file
test_content = """module test;

Footstep : flat {
  x: int32;
  y: int32;
}

interface Calculator {
  void SendFootstep(footstep: in Footstep);
}
"""

test_file = "/tmp/test_goto_param.npidl"
with open(test_file, 'w') as f:
    f.write(test_content)

lsp = subprocess.Popen(
    ['/home/nikita/projects/npsystem/build/linux/bin/npidl', '--lsp'],
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE
)

try:
    # Initialize
    send_msg(lsp, {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"capabilities": {}}})
    read_msg(lsp)
    
    # Open file
    send_msg(lsp, {
        "jsonrpc": "2.0",
        "method": "textDocument/didOpen",
        "params": {
            "textDocument": {
                "uri": f"file://{test_file}",
                "languageId": "npidl",
                "version": 1,
                "text": test_content
            }
        }
    })
    time.sleep(0.3)
    
    # Try hover on different positions to see what's indexed
    positions = [
        (2, 0, "Footstep struct"),
        (8, 7, "SendFootstep function name"),
        (8, 20, "footstep parameter name"),
        (8, 34, "Footstep type"),
    ]
    
    for line, char, desc in positions:
        send_msg(lsp, {
            "jsonrpc": "2.0",
            "id": line * 100 + char,
            "method": "textDocument/hover",
            "params": {
                "textDocument": {"uri": f"file://{test_file}"},
                "position": {"line": line, "character": char}
            }
        })
        
        for _ in range(2):
            resp = read_msg(lsp)
            if resp and resp.get('id') == line * 100 + char:
                break
        
        if resp and 'result' in resp and resp['result']:
            hover = resp['result'].get('contents', '')
            print(f"Line {line+1}, char {char+1} ({desc}):")
            print(f"  {hover[:80]}...")
        else:
            print(f"Line {line+1}, char {char+1} ({desc}): NO HOVER DATA")
    
finally:
    lsp.terminate()
    lsp.wait()

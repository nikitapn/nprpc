#!/usr/bin/env python3
"""Debug: Check stderr for parsing errors"""
import json
import subprocess
import time
import threading

def read_stderr(proc):
    """Read stderr in background"""
    for line in proc.stderr:
        print(f"[STDERR] {line.decode('utf-8').rstrip()}")

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

# Start stderr reader thread
stderr_thread = threading.Thread(target=read_stderr, args=(lsp,), daemon=True)
stderr_thread.start()

try:
    print("Sending initialize...")
    send_msg(lsp, {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"capabilities": {}}})
    resp = read_msg(lsp)
    print(f"Initialize response: {resp is not None}")
    
    time.sleep(0.2)
    
    print("\nSending didOpen...")
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
    
    time.sleep(1)  # Give it time to parse and print stderr
    
    print("\nSending hover request...")
    send_msg(lsp, {
        "jsonrpc": "2.0",
        "id": 2,
        "method": "textDocument/hover",
        "params": {
            "textDocument": {"uri": f"file://{test_file}"},
            "position": {"line": 2, "character": 5}
        }
    })
    
    for _ in range(2):
        resp = read_msg(lsp)
        if resp and resp.get('id') == 2:
            print(f"Hover response: {resp}")
            break
    
    time.sleep(0.5)  # Wait for any remaining stderr
    
finally:
    lsp.terminate()
    lsp.wait()

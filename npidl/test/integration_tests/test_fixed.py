#!/usr/bin/env python3
"""Test go-to-definition on function parameter types"""
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

test_file = "/tmp/test_goto_param_fixed.npidl"
with open(test_file) as f:
    test_content = f.read()

lsp = subprocess.Popen(
    ['/home/nikita/projects/npsystem/build/linux/bin/npidl', '--lsp'],
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE
)

try:
    print("Testing go-to-definition on function parameter type...")
    print("=" * 70)
    
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
    time.sleep(0.5)
    
    lines = test_content.split('\n')
    
    # Line 8 (0-indexed): "  void SendFootstep(footstep: in Footstep);"
    test_line = 8
    line_text = lines[test_line]
    type_pos = line_text.rfind('Footstep')
    
    print(f"Test file content:")
    for i, line in enumerate(lines):
        marker = " <-- Testing here" if i == test_line else ""
        print(f"  Line {i+1}: {line}{marker}")
    
    print(f"\nClicking on 'Footstep' type at line {test_line+1}, char {type_pos+1}")
    print(f"Context: {line_text.strip()}")
    
    # Request go-to-definition
    send_msg(lsp, {
        "jsonrpc": "2.0",
        "id": 2,
        "method": "textDocument/definition",
        "params": {
            "textDocument": {"uri": f"file://{test_file}"},
            "position": {"line": test_line, "character": type_pos}
        }
    })
    
    # Read responses
    for _ in range(3):
        resp = read_msg(lsp)
        if resp and resp.get('id') == 2:
            break
    
    print("\n" + "=" * 70)
    if resp and 'result' in resp and resp['result']:
        result = resp['result']
        if isinstance(result, dict) and 'range' in result:
            target_line = result['range']['start']['line']
            target_char = result['range']['start']['character']
            print(f"✅ SUCCESS: Jumped to line {target_line+1}, char {target_char+1}")
            print(f"   Definition: {lines[target_line].strip()}")
            
            if 'Footstep : flat' in lines[target_line]:
                print(f"\n✅ CORRECT: Found the Footstep struct definition!")
            elif 'void SendFootstep' in lines[target_line]:
                print(f"\n❌ WRONG: Jumped to function instead of type definition")
            else:
                print(f"\n⚠️  Unexpected target: {lines[target_line]}")
    else:
        print(f"❌ FAILED: No definition found")
    
finally:
    lsp.terminate()
    lsp.wait()

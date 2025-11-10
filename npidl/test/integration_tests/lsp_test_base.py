#!/usr/bin/env python3
"""
Base class for LSP integration tests
Provides common functionality for communicating with the LSP server
"""

import json
import subprocess
import sys
from pathlib import Path

class LspTestClient:
    """Simple LSP client for testing"""
    
    def __init__(self, npidl_path=None):
        """Initialize LSP client and start server"""
        if npidl_path is None:
            # Try to find npidl in standard locations
            npidl_path = self._find_npidl()
        
        self.npidl_path = npidl_path
        print(f"Using npidl at: {npidl_path}")
        
        # Start the LSP server
        self.process = subprocess.Popen(
            [npidl_path, '--lsp'],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=0
        )
        
        self.request_id = 0
    
    def _find_npidl(self):
        """Find npidl executable"""
        # Check environment variable
        import os
        if 'NPIDL_BIN' in os.environ:
            return os.environ['NPIDL_BIN']
        
        # Check common build locations
        script_dir = Path(__file__).parent
        candidates = [
            script_dir / '../../../../build/linux/bin/npidl',
            script_dir / '../../../../bin/npidl',
            script_dir / '../../../build/bin/npidl',
        ]
        
        for candidate in candidates:
            if candidate.exists():
                return str(candidate.resolve())
        
        raise FileNotFoundError(
            "Could not find npidl executable. "
            "Set NPIDL_BIN environment variable or build the project first."
        )
    
    def send_request(self, method, params=None):
        """Send a JSON-RPC request and return response"""
        self.request_id += 1
        request = {
            "jsonrpc": "2.0",
            "id": self.request_id,
            "method": method,
            "params": params or {}
        }
        
        request_json = json.dumps(request)
        message = f"Content-Length: {len(request_json)}\r\n\r\n{request_json}"
        
        self.process.stdin.write(message)
        self.process.stdin.flush()
        
        return self.read_response()
    
    def send_notification(self, method, params=None):
        """Send a JSON-RPC notification (no response expected)"""
        notification = {
            "jsonrpc": "2.0",
            "method": method,
            "params": params or {}
        }
        
        notification_json = json.dumps(notification)
        message = f"Content-Length: {len(notification_json)}\r\n\r\n{notification_json}"
        
        self.process.stdin.write(message)
        self.process.stdin.flush()
    
    def read_response(self):
        """Read a JSON-RPC response from server"""
        # Read headers
        content_length = 0
        while True:
            line = self.process.stdout.readline()
            if not line:
                raise Exception("Server closed connection")
            
            line = line.strip()
            if not line:
                break  # Empty line, end of headers
            
            if line.startswith("Content-Length:"):
                content_length = int(line.split(":")[1].strip())
        
        if content_length == 0:
            raise Exception("No Content-Length header")
        
        # Read content
        content = self.process.stdout.read(content_length)
        return json.loads(content)
    
    def initialize(self):
        """Send initialize request"""
        response = self.send_request("initialize", {
            "processId": None,
            "rootUri": None,
            "capabilities": {}
        })
        
        # Send initialized notification
        self.send_notification("initialized", {})
        
        return response
    
    def open_document(self, uri, text):
        """Open a document"""
        self.send_notification("textDocument/didOpen", {
            "textDocument": {
                "uri": uri,
                "languageId": "npidl",
                "version": 1,
                "text": text
            }
        })
    
    def hover(self, uri, line, character):
        """Request hover information"""
        return self.send_request("textDocument/hover", {
            "textDocument": {"uri": uri},
            "position": {"line": line, "character": character}
        })
    
    def goto_definition(self, uri, line, character):
        """Request go-to-definition"""
        return self.send_request("textDocument/definition", {
            "textDocument": {"uri": uri},
            "position": {"line": line, "character": character}
        })
    
    def semantic_tokens(self, uri):
        """Request semantic tokens"""
        return self.send_request("textDocument/semanticTokens/full", {
            "textDocument": {"uri": uri}
        })
    
    def shutdown(self):
        """Shutdown the server"""
        self.send_request("shutdown", {})
        self.send_notification("exit", {})
        self.process.wait(timeout=5)
    
    def __enter__(self):
        """Context manager entry"""
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        """Context manager exit"""
        try:
            self.shutdown()
        except:
            self.process.kill()

# Helper function for creating test URIs
def file_uri(path):
    """Convert a file path to a file:// URI"""
    from pathlib import Path
    abs_path = Path(path).resolve()
    return f"file://{abs_path}"

#!/usr/bin/env python3
"""
Base class for LSP integration tests
Provides common functionality for communicating with the LSP server
"""

import json
import os
import select
import subprocess
import sys
import threading
import time
from pathlib import Path


class LspTestClient:
    """Simple LSP client for testing"""

    def __init__(self, npidl_path=None):
        """Initialize LSP client and start server"""
        if npidl_path is None:
            npidl_path = self._find_npidl()

        self.npidl_path = npidl_path
        print(f"Using npidl at: {npidl_path}", file=sys.stderr)

        self.process = subprocess.Popen(
            [npidl_path, "--lsp"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            bufsize=0,
        )

        self.request_id = 0
        self.stderr_lines = []
        self._stderr_thread = threading.Thread(target=self._drain_stderr, daemon=True)
        self._stderr_thread.start()

    def _drain_stderr(self):
        try:
            for line in iter(self.process.stderr.readline, b""):
                self.stderr_lines.append(line.decode("utf-8", errors="replace"))
        except Exception:
            pass

    def _find_npidl(self):
        """Find npidl executable"""
        if "NPIDL_BIN" in os.environ:
            return os.environ["NPIDL_BIN"]

        script_dir = Path(__file__).resolve().parent
        repo_root = script_dir.parent.parent.parent
        candidates = [
            repo_root / ".build_relwith_debinfo" / "npidl" / "npidl",
            repo_root / ".build_relwith_debinfo" / "bin" / "npidl",
            repo_root / "build" / "linux" / "bin" / "npidl",
            repo_root / "build" / "bin" / "npidl",
            script_dir / "../../../../build/linux/bin/npidl",
        ]

        for candidate in candidates:
            if candidate.exists():
                return str(candidate)

        raise FileNotFoundError(
            "Could not find npidl executable. "
            "Set NPIDL_BIN environment variable or build the project first."
        )

    def _write(self, payload: dict):
        raw = json.dumps(payload)
        message = f"Content-Length: {len(raw)}\r\n\r\n{raw}"
        self.process.stdin.write(message.encode("utf-8"))
        self.process.stdin.flush()

    def send_request(self, method, params=None, timeout=5.0):
        """Send a JSON-RPC request and return the matching response"""
        self.request_id += 1
        req_id = self.request_id
        self._write(
            {
                "jsonrpc": "2.0",
                "id": req_id,
                "method": method,
                "params": params or {},
            }
        )
        return self.read_response(req_id, timeout=timeout)

    def send_notification(self, method, params=None):
        """Send a JSON-RPC notification (no response expected)"""
        self._write(
            {
                "jsonrpc": "2.0",
                "method": method,
                "params": params or {},
            }
        )

    def _read_one_message(self, timeout=5.0):
        stdout = self.process.stdout
        deadline = time.time() + timeout
        headers = b""
        while b"\r\n\r\n" not in headers:
            remaining = deadline - time.time()
            if remaining <= 0:
                raise TimeoutError("Timed out waiting for LSP headers")
            ready, _, _ = select.select([stdout], [], [], remaining)
            if not ready:
                raise TimeoutError("Timed out waiting for LSP headers")
            chunk = stdout.read(1)
            if not chunk:
                raise Exception("Server closed connection")
            headers += chunk

        header_text = headers.decode("utf-8")
        content_length = 0
        for line in header_text.split("\r\n"):
            if line.lower().startswith("content-length:"):
                content_length = int(line.split(":", 1)[1].strip())

        if content_length == 0:
            raise Exception("No Content-Length header")

        body = b""
        while len(body) < content_length:
            remaining = deadline - time.time()
            if remaining <= 0:
                raise TimeoutError("Timed out waiting for LSP body")
            ready, _, _ = select.select([stdout], [], [], remaining)
            if not ready:
                raise TimeoutError("Timed out waiting for LSP body")
            chunk = stdout.read(content_length - len(body))
            if not chunk:
                raise Exception("Server closed connection")
            body += chunk

        return json.loads(body.decode("utf-8"))

    def read_response(self, request_id, timeout=5.0):
        """Read messages until the response with the given id arrives."""
        deadline = time.time() + timeout
        notifications = []
        while True:
            remaining = deadline - time.time()
            if remaining <= 0:
                raise TimeoutError(
                    f"Timed out waiting for response id={request_id}"
                )
            msg = self._read_one_message(timeout=remaining)
            if msg.get("id") == request_id:
                msg["_notifications"] = notifications
                return msg
            notifications.append(msg)

    def wait_for_notification(self, method, timeout=5.0):
        """Read messages until a notification with the given method arrives."""
        deadline = time.time() + timeout
        extras = []
        while True:
            remaining = deadline - time.time()
            if remaining <= 0:
                raise TimeoutError(f"Timed out waiting for {method}")
            msg = self._read_one_message(timeout=remaining)
            if msg.get("method") == method:
                return msg
            extras.append(msg)

    def initialize(self):
        """Send initialize request"""
        response = self.send_request(
            "initialize",
            {
                "processId": None,
                "rootUri": None,
                "capabilities": {},
            },
        )
        self.send_notification("initialized", {})
        return response

    def open_document(self, uri, text, version=1):
        """Open a document and wait for diagnostics"""
        self.send_notification(
            "textDocument/didOpen",
            {
                "textDocument": {
                    "uri": uri,
                    "languageId": "npidl",
                    "version": version,
                    "text": text,
                }
            },
        )
        return self.wait_for_notification("textDocument/publishDiagnostics")

    def change_document(self, uri, text, version):
        """Replace document contents (full sync) and wait for diagnostics"""
        self.send_notification(
            "textDocument/didChange",
            {
                "textDocument": {"uri": uri, "version": version},
                "contentChanges": [{"text": text}],
            },
        )
        return self.wait_for_notification("textDocument/publishDiagnostics")

    def change_document_incremental(self, uri, changes, version):
        """Apply ranged content changes and wait for diagnostics"""
        self.send_notification(
            "textDocument/didChange",
            {
                "textDocument": {"uri": uri, "version": version},
                "contentChanges": changes,
            },
        )
        return self.wait_for_notification("textDocument/publishDiagnostics")

    def hover(self, uri, line, character):
        return self.send_request(
            "textDocument/hover",
            {
                "textDocument": {"uri": uri},
                "position": {"line": line, "character": character},
            },
        )

    def goto_definition(self, uri, line, character):
        return self.send_request(
            "textDocument/definition",
            {
                "textDocument": {"uri": uri},
                "position": {"line": line, "character": character},
            },
        )

    def semantic_tokens(self, uri):
        return self.send_request(
            "textDocument/semanticTokens/full",
            {"textDocument": {"uri": uri}},
        )

    def document_symbol(self, uri):
        return self.send_request(
            "textDocument/documentSymbol",
            {"textDocument": {"uri": uri}},
        )

    def shutdown(self):
        """Shutdown the server"""
        try:
            self.send_request("shutdown", {}, timeout=3.0)
            self.send_notification("exit", {})
            self.process.wait(timeout=5)
        except Exception:
            self.process.kill()
            self.process.wait(timeout=5)

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        try:
            self.shutdown()
        except Exception:
            self.process.kill()


def file_uri(path):
    """Convert a file path to a file:// URI"""
    abs_path = Path(path).resolve()
    return f"file://{abs_path}"


def find_in_text(text, needle, occurrence=0):
    """Return 0-based (line, character) of a substring."""
    idx = -1
    for _ in range(occurrence + 1):
        idx = text.find(needle, idx + 1)
        if idx < 0:
            raise ValueError(f"{needle!r} not found in text")
    line = text.count("\n", 0, idx)
    last_nl = text.rfind("\n", 0, idx)
    col = idx if last_nl < 0 else idx - last_nl - 1
    return line, col

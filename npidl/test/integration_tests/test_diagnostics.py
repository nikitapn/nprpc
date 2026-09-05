#!/usr/bin/env python3
"""Parser error diagnostics."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from lsp_test_base import LspTestClient

URI = "file:///tmp/npidl_diagnostics.npidl"
SOURCE = """\
module sample;

interface Demo {
  void bad(i32 x, i32 y)
  void good(z: i32);
}
"""


def main():
    with LspTestClient() as client:
        client.initialize()
        msg = client.open_document(URI, SOURCE)
        diags = msg["params"]["diagnostics"]
        assert diags, "expected at least one diagnostic for the missing semicolon"
        for diag in diags:
            assert "range" in diag
            assert diag["range"]["start"]["line"] >= 0
            assert diag.get("message")
        print(f"✓ diagnostics: {len(diags)} error(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

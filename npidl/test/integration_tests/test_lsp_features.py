#!/usr/bin/env python3
"""Hover and general LSP features."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from lsp_test_base import LspTestClient, find_in_text

URI = "file:///tmp/npidl_features.npidl"
SOURCE = """\
module sample;

alias coord_t = i32;

message Point {
  x: coord_t;
  y: coord_t;
}

interface Demo {
  void Move(p: Point);
}
"""


def main():
    with LspTestClient() as client:
        init = client.initialize()
        caps = init["result"]["capabilities"]
        assert caps.get("hoverProvider")
        assert caps.get("definitionProvider")
        assert caps.get("documentSymbolProvider")

        msg = client.open_document(URI, SOURCE)
        assert msg["params"]["diagnostics"] == [], msg["params"]["diagnostics"]

        line, col = find_in_text(SOURCE, "Move")
        hover = client.hover(URI, line, col)
        result = hover.get("result")
        assert result, f"hover failed: {hover}"
        contents = str(result.get("contents", ""))
        assert "Move" in contents, contents

        symbols = client.document_symbol(URI)
        result = symbols.get("result") or []
        names = [s.get("name") for s in result]
        assert "Point" in names
        assert "Demo" in names
        assert "coord_t" in names
        for s in result:
            assert isinstance(s.get("kind"), int), (
                f"DocumentSymbol.kind must be a number, got {s.get('kind')!r}"
            )
            assert "range" in s and "selectionRange" in s
        print("✓ hover and document symbols")
    return 0


if __name__ == "__main__":
    sys.exit(main())

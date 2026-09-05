#!/usr/bin/env python3
"""Diagnostic range accuracy."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from lsp_test_base import LspTestClient

URI = "file:///tmp/npidl_diagnostic_ranges.npidl"
SOURCE = """\
module sample;

message Point {
  x: NotAType;
}
"""


def main():
    with LspTestClient() as client:
        client.initialize()
        msg = client.open_document(URI, SOURCE)
        diags = msg["params"]["diagnostics"]
        assert diags, "expected a diagnostic for unknown type"
        diag = diags[0]
        start = diag["range"]["start"]
        end = diag["range"]["end"]
        assert start["line"] == end["line"]
        assert end["character"] > start["character"], "range must span the token"
        line = SOURCE.splitlines()[start["line"]]
        highlighted = line[start["character"] : end["character"]]
        assert highlighted, f"empty highlight on {line!r}"
        print(f"✓ diagnostic range highlights {highlighted!r}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

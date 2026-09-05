#!/usr/bin/env python3
"""Go-to-definition on type aliases."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from lsp_test_base import LspTestClient, find_in_text

URI = "file:///tmp/npidl_goto_alias.npidl"
SOURCE = """\
module sample;

alias coord_t = i32;

message Point {
  x: coord_t;
  y: coord_t;
}
"""


def main():
    with LspTestClient() as client:
        client.initialize()
        msg = client.open_document(URI, SOURCE)
        assert msg["params"]["diagnostics"] == [], msg["params"]["diagnostics"]

        line, col = find_in_text(SOURCE, "coord_t", occurrence=1)
        resp = client.goto_definition(URI, line, col)
        result = resp.get("result")
        assert result, f"no definition: {resp}"
        target = result["range"]["start"]["line"]
        assert "alias coord_t" in SOURCE.splitlines()[target], (
            f"jumped to unexpected line {target}: {SOURCE.splitlines()[target]}"
        )
        print("✓ go-to-definition on alias coord_t")
    return 0


if __name__ == "__main__":
    sys.exit(main())

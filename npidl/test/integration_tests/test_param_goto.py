#!/usr/bin/env python3
"""Go-to-definition on function parameter types."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from lsp_test_base import LspTestClient, find_in_text

URI = "file:///tmp/npidl_param_goto.npidl"
SOURCE = """\
module sample;

message Footstep {
  x: i32;
  y: i32;
}

interface Calculator {
  void SendFootstep(footstep: Footstep);
}
"""


def main():
    with LspTestClient() as client:
        client.initialize()
        msg = client.open_document(URI, SOURCE)
        assert msg["params"]["diagnostics"] == [], msg["params"]["diagnostics"]

        line, col = find_in_text(SOURCE, ": Footstep")
        col += 2  # skip ": " to land on the type name
        resp = client.goto_definition(URI, line, col)
        result = resp.get("result")
        assert result, f"no definition: {resp}"
        target = result["range"]["start"]["line"]
        text = SOURCE.splitlines()[target]
        assert "message Footstep" in text, f"jumped to function instead of type: {text}"
        print("✓ go-to-definition on parameter type Footstep")
    return 0


if __name__ == "__main__":
    sys.exit(main())

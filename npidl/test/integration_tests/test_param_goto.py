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

enum PanelEdge: u32 {
  top,
  bottom,
  left,
  right
};

interface Calculator {
  void SendFootstep(footstep: Footstep);
  u32 CreatePanel(arenaId: in string, edge: in PanelEdge,
                  thickness: in u32, reserve: in boolean,
                  title: in string, appId: in string);
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

        line, col = find_in_text(SOURCE, "in PanelEdge")
        col += 3  # skip "in " to land on the enum type name
        resp = client.goto_definition(URI, line, col)
        result = resp.get("result")
        assert result, f"no definition for PanelEdge: {resp}"
        target = result["range"]["start"]["line"]
        text = SOURCE.splitlines()[target]
        assert "enum PanelEdge" in text, (
            f"enum param jumped to unexpected line {target}: {text}"
        )
        print("✓ go-to-definition on parameter types Footstep and PanelEdge")
    return 0


if __name__ == "__main__":
    sys.exit(main())

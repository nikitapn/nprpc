#!/usr/bin/env python3
"""Go-to-definition on type names."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from lsp_test_base import LspTestClient, find_in_text

URI = "file:///tmp/npidl_goto.npidl"
SOURCE = """\
module sample;

message Point {
  x: i32;
  y: i32;
}

message ThemeAck {
  ok: boolean;
}

message SystemTheme {
  name: string;
}

exception SurfaceNotFound {
  id: u32;
}

interface Demo {
  void Move(p: Point);
  bidi_stream<ThemeAck, SystemTheme> SubscribeSystemTheme();
  void DestroySurface(surfaceId: in u32) raises(SurfaceNotFound);
}
"""


def jump_to(client, needle, occurrence=0):
    line, col = find_in_text(SOURCE, needle, occurrence=occurrence)
    resp = client.goto_definition(URI, line, col)
    result = resp.get("result")
    assert result, f"no definition for {needle!r}: {resp}"
    target = result["range"]["start"]["line"]
    return target, SOURCE.splitlines()[target]


def main():
    with LspTestClient() as client:
        client.initialize()
        msg = client.open_document(URI, SOURCE)
        assert msg["params"]["diagnostics"] == []

        target, text = jump_to(client, "Point", occurrence=1)
        assert "message Point" in text, f"jumped to unexpected line {target}: {text}"

        target, text = jump_to(client, "ThemeAck", occurrence=1)
        assert "message ThemeAck" in text, (
            f"return type ThemeAck jumped to unexpected line {target}: {text}"
        )

        target, text = jump_to(client, "SystemTheme", occurrence=1)
        assert "message SystemTheme" in text, (
            f"return type SystemTheme jumped to unexpected line {target}: {text}"
        )

        target, text = jump_to(client, "SurfaceNotFound", occurrence=1)
        assert "exception SurfaceNotFound" in text, (
            f"raises(SurfaceNotFound) jumped to unexpected line {target}: {text}"
        )

        line, col = find_in_text(SOURCE, "SurfaceNotFound", occurrence=1)
        refs = client.send_request(
            "textDocument/references",
            {
                "textDocument": {"uri": URI},
                "position": {"line": line, "character": col},
                "context": {"includeDeclaration": True},
            },
        )
        ref_list = refs.get("result") or []
        assert len(ref_list) >= 2, (
            f"expected declaration + raises usage, got {ref_list}"
        )
        print("✓ go-to-definition on Point, stream return types, and raises()")
    return 0


if __name__ == "__main__":
    sys.exit(main())

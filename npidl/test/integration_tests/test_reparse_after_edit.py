#!/usr/bin/env python3
"""Reparse-after-edit coverage for the NPIDL language server."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from lsp_test_base import LspTestClient, find_in_text

URI = "file:///tmp/npidl_reparse_test.npidl"

VALID = """\
module sample;

message Point {
  x: i32;
  y: i32;
}

interface Demo {
  void Move(p: Point);
}
"""

INVALID_UNKNOWN_TYPE = """\
module sample;

message Point {
  x: UnknownType;
  y: i32;
}

interface Demo {
  void Move(p: Point);
}
"""

AFTER_ADD_FIELD = """\
module sample;

message Point {
  x: i32;
  y: i32;
  z: i32;
}

interface Demo {
  void Move(p: Point);
  void Ping();
}
"""


def diagnostics_of(msg):
    return msg.get("params", {}).get("diagnostics", [])


def assert_no_redefinition(diags, context):
    for diag in diags:
        message = diag.get("message", "")
        assert "redefinition" not in message.lower(), (
            f"{context}: stale redefinition diagnostic: {message}"
        )


def token_count(response):
    data = (response.get("result") or {}).get("data") or []
    return len(data) // 5


def decode_tokens(data, source):
    """Decode LSP semantic tokens into (line, col, length, type)."""
    tokens = []
    line = 0
    col = 0
    for i in range(0, len(data), 5):
        dline, dcol, length, ttype, _mods = data[i : i + 5]
        line += dline
        col = dcol if dline != 0 else col + dcol
        tokens.append((line, col, length, ttype))
    return tokens


def main():
    with LspTestClient() as client:
        init = client.initialize()
        caps = init["result"]["capabilities"]
        assert caps.get("hoverProvider"), "hoverProvider missing"
        assert caps.get("definitionProvider"), "definitionProvider missing"

        # 1. Open a valid file — no errors.
        diags_msg = client.open_document(URI, VALID, version=1)
        diags = diagnostics_of(diags_msg)
        assert_no_redefinition(diags, "didOpen")
        assert diags == [], f"valid file should have no diagnostics, got {diags}"

        hover_line, hover_col = find_in_text(VALID, "Move")
        hover = client.hover(URI, hover_line, hover_col)
        assert hover.get("result"), f"hover on Move failed: {hover}"
        assert "Move" in str(hover["result"].get("contents", ""))

        def_line, def_col = find_in_text(VALID, "Point", occurrence=1)
        goto = client.goto_definition(URI, def_line, def_col)
        result = goto.get("result")
        assert result, f"go-to-definition on Point failed: {goto}"
        target_line = result["range"]["start"]["line"]
        assert "message Point" in VALID.splitlines()[target_line]

        tokens_before = client.semantic_tokens(URI)
        n_before = token_count(tokens_before)
        assert n_before > 0, "expected semantic tokens on valid file"

        data = tokens_before["result"]["data"]
        decoded = decode_tokens(data, VALID)
        # Token type 4 is function. "Move" must be a 4-character function token.
        fn_tokens = [t for t in decoded if t[3] == 4]
        assert fn_tokens, f"no function semantic tokens: {decoded}"
        _line, _col, length, _ty = fn_tokens[0]
        assert length == len("Move"), (
            f"function token should cover just the name, got length={length}"
        )

        # 2. Introduce an error — diagnostics, but NOT type redefinition.
        diags_msg = client.change_document(URI, INVALID_UNKNOWN_TYPE, version=2)
        diags = diagnostics_of(diags_msg)
        assert diags, "expected diagnostics after introducing UnknownType"
        assert_no_redefinition(diags, "didChange invalid")

        # 3. Restore the original file — errors must clear, features still work.
        diags_msg = client.change_document(URI, VALID, version=3)
        diags = diagnostics_of(diags_msg)
        assert_no_redefinition(diags, "didChange restore")
        assert diags == [], f"restored file should have no diagnostics, got {diags}"

        hover = client.hover(URI, hover_line, hover_col)
        assert hover.get("result"), f"hover after restore failed: {hover}"

        goto = client.goto_definition(URI, def_line, def_col)
        assert goto.get("result"), f"definition after restore failed: {goto}"

        # 4. Edit that adds a field and a method — no redefinition, new symbols.
        diags_msg = client.change_document(URI, AFTER_ADD_FIELD, version=4)
        diags = diagnostics_of(diags_msg)
        assert_no_redefinition(diags, "didChange add field")
        assert diags == [], f"expanded file should have no diagnostics, got {diags}"

        ping_line, ping_col = find_in_text(AFTER_ADD_FIELD, "Ping")
        hover = client.hover(URI, ping_line, ping_col)
        assert hover.get("result"), f"hover on new method Ping failed: {hover}"
        assert "Ping" in str(hover["result"].get("contents", ""))

        symbols = client.document_symbol(URI)
        names = [s.get("name") for s in (symbols.get("result") or [])]
        assert "Point" in names, f"Point missing from document symbols: {names}"
        assert "Demo" in names, f"Demo missing from document symbols: {names}"

        tokens_after = client.semantic_tokens(URI)
        n_after = token_count(tokens_after)
        assert n_after >= n_before, (
            f"expected at least as many tokens after adding a method "
            f"(before={n_before}, after={n_after})"
        )

        # 5. Incremental (ranged) edit: change Move -> Walk in place.
        walk_src = AFTER_ADD_FIELD.replace("void Move(", "void Walk(")
        move_line, move_col = find_in_text(AFTER_ADD_FIELD, "Move")
        diags_msg = client.change_document_incremental(
            URI,
            [
                {
                    "range": {
                        "start": {"line": move_line, "character": move_col},
                        "end": {
                            "line": move_line,
                            "character": move_col + len("Move"),
                        },
                    },
                    "text": "Walk",
                }
            ],
            version=5,
        )
        diags = diagnostics_of(diags_msg)
        assert_no_redefinition(diags, "incremental rename")
        assert diags == [], f"incremental rename should stay valid, got {diags}"

        walk_line, walk_col = find_in_text(walk_src, "Walk")
        hover = client.hover(URI, walk_line, walk_col)
        assert hover.get("result"), f"hover on renamed Walk failed: {hover}"
        assert "Walk" in str(hover["result"].get("contents", ""))

    print("✓ reparse-after-edit tests passed")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        print(f"✗ reparse-after-edit tests failed: {exc}", file=sys.stderr)
        raise

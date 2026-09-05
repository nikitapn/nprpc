#!/usr/bin/env python3
"""Semantic tokens generation."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from lsp_test_base import LspTestClient, find_in_text

URI = "file:///tmp/npidl_semantic_tokens.npidl"
SOURCE = """\
module sample;

message Point {
  x: i32;
}

message ThemeAck {
  ok: boolean;
}

message SystemTheme {
  name: string;
}

enum Color {
  Red,
  Green = 2,
  Blue
}

interface Demo {
  void Move(p: Point);
  bidi_stream<ThemeAck, SystemTheme> SubscribeSystemTheme();
}
"""

# Must match the legend advertised in handle_initialize.
TT_INTERFACE = 1
TT_CLASS = 2
TT_FUNCTION = 4
TT_PARAMETER = 5
TT_PROPERTY = 6
TT_TYPE = 7
TT_KEYWORD = 8
TT_ENUM_MEMBER = 9


def decode(data):
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
        provider = init["result"]["capabilities"].get("semanticTokensProvider")
        assert provider, "semanticTokensProvider missing"

        msg = client.open_document(URI, SOURCE)
        assert msg["params"]["diagnostics"] == []

        resp = client.semantic_tokens(URI)
        data = resp["result"]["data"]
        assert data, "no semantic tokens"
        tokens = decode(data)
        types = {t[3] for t in tokens}
        assert TT_CLASS in types, f"missing struct token: {tokens}"
        assert TT_INTERFACE in types, f"missing interface token: {tokens}"
        assert TT_FUNCTION in types, f"missing function token: {tokens}"
        assert TT_PARAMETER in types, f"missing parameter token: {tokens}"
        assert TT_PROPERTY in types, f"missing field token: {tokens}"

        fn = next(t for t in tokens if t[3] == TT_FUNCTION)
        assert fn[2] == len("Move"), f"function token length {fn[2]} != 4"

        line, col = find_in_text(SOURCE, "Move")
        assert fn[0] == line and fn[1] == col, (
            f"function token at {fn[0]}:{fn[1]}, expected {line}:{col}"
        )

        kw_line, kw_col = find_in_text(SOURCE, "bidi_stream")
        stream_kw = [
            t for t in tokens
            if t[0] == kw_line and t[1] == kw_col and t[2] == len("bidi_stream")
        ]
        assert stream_kw, f"missing bidi_stream keyword token: {tokens}"
        assert stream_kw[0][3] == TT_KEYWORD, (
            f"bidi_stream token type {stream_kw[0][3]} != keyword"
        )

        ack_line, ack_col = find_in_text(SOURCE, "ThemeAck", occurrence=1)
        ack_tok = [
            t for t in tokens if t[0] == ack_line and t[1] == ack_col
        ]
        assert ack_tok, f"missing ThemeAck return-type token: {tokens}"
        assert ack_tok[0][3] == TT_CLASS, (
            f"ThemeAck token type {ack_tok[0][3]} != class"
        )

        red_line, red_col = find_in_text(SOURCE, "Red")
        red_tok = [
            t for t in tokens if t[0] == red_line and t[1] == red_col
        ]
        assert red_tok, f"missing enum key token for Red: {tokens}"
        assert red_tok[0][3] == TT_ENUM_MEMBER, (
            f"Red token type {red_tok[0][3]} != enumMember"
        )
        assert red_tok[0][2] == len("Red")
        print(f"✓ semantic tokens: {len(tokens)} tokens")
    return 0


if __name__ == "__main__":
    sys.exit(main())

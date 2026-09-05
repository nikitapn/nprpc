#!/usr/bin/env bash
# Compile, package, and install the NPIDL VS Code extension.
set -euo pipefail

cd "$(dirname "$0")"

if ! command -v npm >/dev/null 2>&1; then
  echo "error: npm is required (install Node.js)" >&2
  exit 1
fi

find_code() {
  if [[ -n "${CODE_BIN:-}" ]]; then
    if command -v "$CODE_BIN" >/dev/null 2>&1; then
      echo "$CODE_BIN"
      return 0
    fi
    echo "error: CODE_BIN=$CODE_BIN not found" >&2
    return 1
  fi
  local c
  for c in code code-insiders codium; do
    if command -v "$c" >/dev/null 2>&1; then
      echo "$c"
      return 0
    fi
  done
  return 1
}

echo "==> npm install"
npm install

echo "==> compile"
npm run compile

echo "==> package"
npm run package

version="$(node -p "require('./package.json').version")"
name="$(node -p "require('./package.json').name")"
vsix="${name}-${version}.vsix"

if [[ ! -f "$vsix" ]]; then
  echo "error: expected $vsix after packaging" >&2
  ls -la ./*.vsix 2>/dev/null || true
  exit 1
fi

if ! code_bin="$(find_code)"; then
  echo "Packed $(pwd)/$vsix"
  echo "error: VS Code CLI not found (code / code-insiders / codium)." >&2
  echo "Install the CLI or set CODE_BIN, then run:" >&2
  echo "  code --install-extension $(pwd)/$vsix --force" >&2
  exit 1
fi

echo "==> install $vsix ($code_bin)"
"$code_bin" --install-extension "$vsix" --force

echo
echo "Installed $vsix"
echo "Set npidl.lsp.path to your npidl binary, then reload the window."
echo "  Example: /home/nikita/projects/nprpc/.build_relwith_debinfo/npidl/npidl"
echo "Logs: /tmp/npidl-lsp.log"

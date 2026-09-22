#!/usr/bin/env bash
# Assemble the static root served by the NPRPC backend.
#
#   client/src/islands/  --vite------>  <static>/islands.js
#   web/styles/app.css   --tailwind-->  <static>/app.css
#   web/vendor/*         --copy----->   <static>/vendor/
#
# Tailwind scans templates/**/*.mustache (see @source in web/styles/app.css),
# so utility classes come from the server-rendered templates.  No framework, no
# SSR, and no Node in the serving path — this is a build-time step only.
#
# host.json is written by the server into the same directory and is left alone.
#
# Usage:
#   ./build-assets.sh            # minified
#   ./build-assets.sh --watch    # rebuild CSS on template change
#   ./build-assets.sh --debug    # unminified, easier to read

set -euo pipefail

ROOT_DIR="$(dirname "$(readlink -e "${BASH_SOURCE[0]}")")/.."
cd "$ROOT_DIR"

STATIC_ROOT="client/build/client"
CSS_IN="web/styles/app.css"
CSS_OUT="$STATIC_ROOT/app.css"

TAILWIND_ARGS=(--minify)
WATCH=0

for arg in "$@"; do
  case $arg in
    --watch) WATCH=1 ;;
    --debug) TAILWIND_ARGS=() ;;
    *)
      echo "Unknown argument: $arg"
      echo "Usage: $0 [--watch] [--debug]"
      exit 1
      ;;
  esac
done

# The Tailwind CLI is hoisted to the monorepo root by npm workspaces.
TAILWIND="../../node_modules/.bin/tailwindcss"
if [ ! -x "$TAILWIND" ]; then
  echo "Tailwind CLI not found at $TAILWIND" >&2
  echo "Run: npm install --ignore-scripts -w examples/live-blog/client" >&2
  exit 1
fi

mkdir -p "$STATIC_ROOT/vendor"

echo "=== Islands (vite) ==="
(cd client && npx --no-install vite build)

echo
echo "=== Vendored assets ==="
for f in web/vendor/*; do
  [ -e "$f" ] || continue
  cp -v "$f" "$STATIC_ROOT/vendor/"
done

echo
echo "=== Tailwind ==="
if [ "$WATCH" -eq 1 ]; then
  exec "$TAILWIND" -i "$CSS_IN" -o "$CSS_OUT" "${TAILWIND_ARGS[@]}" --watch
fi

"$TAILWIND" -i "$CSS_IN" -o "$CSS_OUT" "${TAILWIND_ARGS[@]}"

echo
echo "Static root: $(readlink -e "$STATIC_ROOT")"
ls -l "$STATIC_ROOT" "$STATIC_ROOT/vendor"

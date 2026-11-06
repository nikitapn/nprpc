#!/usr/bin/env bash
# Brings the JS runtime in nprpc_js/dist and the generated stubs up to date, for
# the tests in test/js. Those import the runtime from dist, and a build configured
# with NPRPC_BUILD_JS=OFF has no CMake target that rebuilds it.
#
# Usage: build_if_stale.sh <cmake build dir>
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
build_dir="$(cd "${1:?usage: build_if_stale.sh <cmake build dir>}" && pwd)"

cmake --build "$build_dir" --target nprpc_stub_gen nprpc_test_gen

# Only copies what changed, so an unchanged stub doesn't trigger a rebuild.
sync_stubs() {
  local from="$1" to="$2"
  mkdir -p "$to"
  for file in "$from"/*.ts; do
    cmp -s "$file" "$to/$(basename "$file")" || cp "$file" "$to/"
  done
}
sync_stubs "$build_dir/nprpc_stub/src/gen/js" "$root/nprpc_js/src/gen"
sync_stubs "$build_dir/nprpc_test_stub/src/gen/js" "$root/test/js/src/gen"

cd "$root/nprpc_js"
newest_output=dist/index.node.js
if [[ -f "$newest_output" ]] && [[ -z "$(find src package.json tsconfig.json webpack.config.js \
    fix-declaration-paths.js -newer "$newest_output" -print -quit)" ]]; then
  echo "JS runtime is up to date"
  exit 0
fi

echo "Rebuilding JS runtime"
# nprpc_js is an npm workspace: install from the root, since `npm ci` run in a
# workspace folder replaces the shared node_modules with that workspace's deps.
[[ -d "$root/node_modules/nprpc" ]] || (cd "$root" && npm ci)
npm run build
# Webpack may leave a file alone when its content is unchanged.
touch dist/index.js dist/index.esm.js dist/index.node.js

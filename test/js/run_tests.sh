#!/usr/bin/env bash
# Deprecated: use `just run-js-tests` from the repo root.
set -euo pipefail
cd "$(dirname "$0")/../.."
exec just run-js-tests "$@"

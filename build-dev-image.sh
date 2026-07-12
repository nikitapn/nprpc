#!/usr/bin/env bash
# Deprecated entry point: use `just build-dev-image` from the repo root.
# Kept so CMake's nprpc_dev_docker target keeps working.
set -euo pipefail
cd "$(dirname "$0")"
export IMAGE_NAME="${IMAGE_NAME:-nprpc-dev}"
export IMAGE_TAG="${IMAGE_TAG:-latest}"
exec just build-dev-image "$IMAGE_NAME" "$IMAGE_TAG"

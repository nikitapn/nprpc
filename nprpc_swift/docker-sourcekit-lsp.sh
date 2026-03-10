#!/usr/bin/env bash
# docker-sourcekit-lsp.sh
#
# Runs sourcekit-lsp from the nprpc-dev:latest image, forwarding the LSP
# protocol over stdin/stdout.  VS Code never knows it is talking to Docker.
#
# Set in .vscode/settings.json:
#   "sourcekit-lsp.serverPath": "${workspaceFolder}/nprpc_swift/docker-sourcekit-lsp.sh"
#
# nprpc, Boost, and OpenSSL are pre-installed in nprpc-dev:latest under /opt/.
# The host nprpc_swift/ source tree is mounted at the same absolute path so
# all file URIs the editor sends are valid paths inside the container.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HOME_DIR="$HOME"
IMAGE="nprpc-dev:latest"

# Ensure cache dirs exist on the host so the mounts are valid
mkdir -p "$HOME_DIR/.cache" "$HOME_DIR/.local" "$HOME_DIR/.swiftpm"

exec docker run --rm -i \
  --user "$(id -u):$(id -g)" \
  -e HOME=/home/ubuntu \
  \
  `# Mount the nprpc_swift source at the same absolute path so LSP URIs match` \
  -v "$SCRIPT_DIR:$SCRIPT_DIR" \
  \
  `# Cache directories so index-store and build artefacts survive across LSP sessions` \
  -v "$HOME_DIR/.cache:/home/ubuntu/.cache" \
  -v "$HOME_DIR/.local:/home/ubuntu/.local" \
  -v "$HOME_DIR/.swiftpm:/home/ubuntu/.swiftpm" \
  \
  `# Work inside the nprpc_swift sub-package so SPM resolution just works` \
  -w "$SCRIPT_DIR" \
  \
  "$IMAGE" \
  /usr/bin/sourcekit-lsp

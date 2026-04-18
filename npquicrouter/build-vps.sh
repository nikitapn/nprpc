#!/usr/bin/env bash
# Build npquicrouter inside a Debian 12 container and extract the binary.
# Run from anywhere; the script locates the nprpc repo root automatically.
#
# Usage:
#   ./npquicrouter/build-vps.sh [user@vps]
#
# With an optional ssh destination the binary is scp'd to the remote host:
#   ./npquicrouter/build-vps.sh deploy@my-vps.example.com
set -euo pipefail

REPO_ROOT=$(dirname "$(dirname "$(readlink -e "${BASH_SOURCE[0]}")")")
OUT="$REPO_ROOT/npquicrouter/npquicrouter-linux-amd64"
IMAGE="npquicrouter-build:debian12"
CONTAINER="npquicrouter-extract-$$"

cd "$REPO_ROOT"

echo "==> Building Docker image ($IMAGE) ..."
docker build \
  -f npquicrouter/Dockerfile.debian12 \
  --target builder \
  -t "$IMAGE" \
  .

echo "==> Extracting binary ..."
docker create --name "$CONTAINER" "$IMAGE" >/dev/null
docker cp "$CONTAINER:/src/build/npquicrouter" "$OUT"
docker rm "$CONTAINER" >/dev/null

echo "==> Binary written to: $OUT"
echo "    $(file "$OUT")"

if [[ "${1:-}" != "" ]]; then
  DEST="$1"
  echo "==> Deploying to $DEST ..."
  scp "$OUT" "$DEST:/usr/local/bin/npquicrouter"
  # Allow binding port 443 without root
  ssh "$DEST" "sudo setcap cap_net_bind_service+ep /usr/local/bin/npquicrouter"
  echo "==> Done. Restart the service on the VPS."
fi

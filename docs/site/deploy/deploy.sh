#!/bin/bash
# Deploy the NPRPC documentation site to a Docker host behind npquicrouter.
# Run from anywhere in the nprpc checkout; see usage below.

set -euo pipefail

DEPLOY_DIR=$(dirname "$(readlink -e "${BASH_SOURCE[0]}")")
SITE_DIR=$(dirname "$DEPLOY_DIR")
REPO_DIR=$(readlink -e "$SITE_DIR/../..")

SSH_TARGET=""
HOSTNAME="nprpc.nikitapn.com"
PORT="9443"
CERT_DIR="/etc/letsencrypt"
CERT_NAME=""
CONTAINER_NAME="nprpc-docs"
IMAGE_NAME="nprpc-docs:latest"
RUNTIME_IMAGE="nprpc-runtime:latest"
DEV_IMAGE="nprpc-dev:latest"
REMOTE_TMP_DIR="/tmp/nprpc-docs-release"
HTTP3=1
SHM_CHANNEL="nprpc_docs"
SHM_DIR="/dev/shm/npquicrouter"
BUILD_API=1

usage() {
  cat <<'EOF'
Usage: docs/site/deploy/deploy.sh --ssh user@server [options]

Builds api.json and a release docs-server, packages them on the nprpc-runtime
image, and runs the container on the server. npquicrouter, in front of it,
routes the hostname to --port on the server's loopback.

  --ssh <user@server>       SSH target (required)
  --hostname <name>         Public hostname (default: nprpc.nikitapn.com)
  --port <port>             Loopback port the router forwards TCP and UDP to
                            (default: 9443)
  --cert-dir <path>         certbot's config dir on the server, mounted read-only
                            at /certs (default: /etc/letsencrypt)
  --cert-name <name>        Certificate lineage under live/ (default: the hostname)
  --container <name>        Container name (default: nprpc-docs)
  --image <name>            Image tag built on the server (default: nprpc-docs:latest)
  --runtime-image <name>    NPRPC runtime base image (default: nprpc-runtime:latest,
                            resolved to its versioned tag)
  --dev-image <name>        Image docs-server is compiled in (default: nprpc-dev:latest).
                            Must be the image the runtime image was built from.
  --no-http3                Serve HTTPS only
  --shm-channel <name>      npquicrouter shared-memory channel for HTTP/3
                            (default: nprpc_docs). The router's route must name it
                            as shm_ingress_channel and shm_egress_channel.
  --no-shm                  Plain loopback UDP to the router instead
  --shm-dir <path>          npquicrouter's shared-memory directory, mounted as the
                            container's /dev/shm (default: /dev/shm/npquicrouter)
  --skip-api                Reuse the existing api.json instead of running `just docs-api`

The certificate must exist on the server before the first deploy:

  certbot certonly --webroot -w /var/www/acme -d <hostname> \
    --deploy-hook 'docker kill -s HUP nprpc-docs'

The server reloads its certificate on SIGHUP without dropping connections.
EOF
}

while [ $# -gt 0 ]; do
  case "$1" in
    --ssh) SSH_TARGET="$2"; shift ;;
    --hostname) HOSTNAME="$2"; shift ;;
    --port) PORT="$2"; shift ;;
    --cert-dir) CERT_DIR="$2"; shift ;;
    --cert-name) CERT_NAME="$2"; shift ;;
    --container) CONTAINER_NAME="$2"; shift ;;
    --image) IMAGE_NAME="$2"; shift ;;
    --runtime-image) RUNTIME_IMAGE="$2"; shift ;;
    --dev-image) DEV_IMAGE="$2"; shift ;;
    --no-http3) HTTP3=0 ;;
    --shm-channel) SHM_CHANNEL="$2"; shift ;;
    --no-shm) SHM_CHANNEL="" ;;
    --shm-dir) SHM_DIR="$2"; shift ;;
    --skip-api) BUILD_API=0 ;;
    --help|-h) usage; exit 0 ;;
    *) echo "Unknown argument: $1" >&2; usage >&2; exit 1 ;;
  esac
  shift
done

if [ -z "$SSH_TARGET" ]; then
  usage >&2
  exit 1
fi
CERT_NAME=${CERT_NAME:-$HOSTNAME}

cd "$REPO_DIR"
BUILD_DIR=${BUILD_DIR:-.build_relwith_debinfo}
API_JSON="$REPO_DIR/$BUILD_DIR/docs/api.json"

# ── runtime image: pin its versioned tag, and check it matches the dev image ──

RUNTIME_ID=$(docker image inspect --format '{{.Id}}' "$RUNTIME_IMAGE") || {
  echo "Runtime image $RUNTIME_IMAGE not found; build it with 'just build-runtime-image'" >&2
  exit 1
}
RUNTIME_TAG=$(docker image inspect --format '{{join .RepoTags "\n"}}' "$RUNTIME_IMAGE" | grep -v ':latest$' | head -n1)
RUNTIME_TAG=${RUNTIME_TAG:-$RUNTIME_IMAGE}

# docs-server links the Swift bridge statically, so it must run against the
# libnprpc it was compiled with.
RUNTIME_LIB_SHA=$(docker image inspect --format '{{index .Config.Labels "io.nprpc.libnprpc.sha256"}}' "$RUNTIME_TAG")
DEV_LIB_SHA=$(docker run --rm --entrypoint sha256sum "$DEV_IMAGE" /opt/nprpc/lib/libnprpc.so.1.0.0 | cut -d' ' -f1)
if [ "$RUNTIME_LIB_SHA" != "$DEV_LIB_SHA" ]; then
  cat >&2 <<MSG
$RUNTIME_TAG was not built from $DEV_IMAGE (their libnprpc differs):
  runtime: ${RUNTIME_LIB_SHA:-<no label>}
  dev:     $DEV_LIB_SHA
Rebuild it: just build-runtime-image $DEV_IMAGE
MSG
  exit 1
fi

# ── build ─────────────────────────────────────────────────────────────────────

if [ "$BUILD_API" = 1 ]; then
  just docs-api
fi
[ -f "$API_JSON" ] || { echo "Missing $API_JSON; run 'just docs-api'" >&2; exit 1; }

# Compile in the dev image, against its prebuilt /opt/nprpc_swift. A separate
# scratch path keeps this build apart from the host's .build.
echo "Building docs-server (release) in $DEV_IMAGE ..."
docker run --rm \
  --user "$(id -u):$(id -g)" \
  -e HOME=/tmp \
  -v "$SITE_DIR":/src \
  -w /src \
  "$DEV_IMAGE" \
  swift build -c release --product docs-server --scratch-path /src/.build-docker

STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$STAGE/context"
cp "$SITE_DIR/.build-docker/release/docs-server" "$STAGE/context/"
cp -a "$SITE_DIR/templates" "$SITE_DIR/web" "$STAGE/context/"
cp "$API_JSON" "$STAGE/context/api.json"
cp "$DEPLOY_DIR/Dockerfile" "$STAGE/context/"
tar -C "$STAGE/context" -czf "$STAGE/nprpc-docs.tar.gz" .

# ── ship ──────────────────────────────────────────────────────────────────────

# docker save/load keeps layer digests, so every site built FROM the runtime
# image on the server shares its layers.
if ssh "$SSH_TARGET" "docker image inspect --format '{{.Id}}' '$RUNTIME_TAG' 2>/dev/null" | grep -qx "$RUNTIME_ID"; then
  echo "Server already has $RUNTIME_TAG"
else
  echo "Sending $RUNTIME_TAG to $SSH_TARGET ..."
  docker save "$RUNTIME_TAG" | gzip | ssh "$SSH_TARGET" 'gunzip | docker load'
fi

scp "$STAGE/nprpc-docs.tar.gz" "$SSH_TARGET:$REMOTE_TMP_DIR.tar.gz"

ssh "$SSH_TARGET" \
  CERT_DIR="$CERT_DIR" \
  CERT_NAME="$CERT_NAME" \
  CONTAINER_NAME="$CONTAINER_NAME" \
  HOSTNAME="$HOSTNAME" \
  HTTP3="$HTTP3" \
  SHM_CHANNEL="$SHM_CHANNEL" \
  SHM_DIR="$SHM_DIR" \
  IMAGE_NAME="$IMAGE_NAME" \
  PORT="$PORT" \
  REMOTE_TMP_DIR="$REMOTE_TMP_DIR" \
  RUNTIME_TAG="$RUNTIME_TAG" \
  'bash -se' <<'EOF'
set -euo pipefail

# certbot's live/ is root-only; look as the container will, as root.
if ! docker run --rm -v "$CERT_DIR:/certs:ro" --entrypoint test "$RUNTIME_TAG" \
     -r "/certs/live/$CERT_NAME/fullchain.pem" -a -r "/certs/live/$CERT_NAME/privkey.pem"; then
  echo "No certificate at $CERT_DIR/live/$CERT_NAME/ on the server. Issue it first:" >&2
  echo "  certbot certonly --webroot -w /var/www/acme -d $HOSTNAME --deploy-hook 'docker kill -s HUP $CONTAINER_NAME'" >&2
  exit 1
fi

rm -rf "$REMOTE_TMP_DIR"
mkdir -p "$REMOTE_TMP_DIR"
tar -xzf "$REMOTE_TMP_DIR.tar.gz" -C "$REMOTE_TMP_DIR"

OLD_IMAGE_ID=$(docker image inspect --format '{{.Id}}' "$IMAGE_NAME" 2>/dev/null || true)
docker build -t "$IMAGE_NAME" \
  --build-arg NPRPC_RUNTIME_IMAGE="$RUNTIME_TAG" \
  "$REMOTE_TMP_DIR"

# SIGTERM first, with time to finish: a process killed mid-operation can
# strand state it shares with npquicrouter. rm -f alone is a SIGKILL.
docker stop -t 15 "$CONTAINER_NAME" >/dev/null 2>&1 || true
docker rm -f "$CONTAINER_NAME" >/dev/null 2>&1 || true

# Root inside the container, only so it can read certbot's root-owned key; the
# filesystem is read-only and every capability but binding :443 is dropped.
# Ports are published on loopback only: npquicrouter is the public entry.
#
# HTTP/3 goes through npquicrouter's shared-memory rings: its directory is
# mounted as /dev/shm (the directory, never the ring files, whose inodes the
# router replaces on every restart). The directory's group grants access:
# the rings are 0660 and root here has no CAP_DAC_OVERRIDE.
SHM_ARGS=()
if [ "$HTTP3" = 1 ] && [ -n "$SHM_CHANNEL" ]; then
  if [ ! -d "$SHM_DIR" ]; then
    echo "npquicrouter's shared-memory directory $SHM_DIR does not exist; see" >&2
    echo "nprpc/npquicrouter/README.md, or deploy with --no-shm." >&2
    exit 1
  fi
  SHM_ARGS=(-v "$SHM_DIR:/dev/shm" --group-add "$(stat -c %g "$SHM_DIR")"
            -e "DOCS_SHM_CHANNEL=$SHM_CHANNEL")
fi
docker run -d \
  --name "$CONTAINER_NAME" \
  --restart unless-stopped \
  --read-only \
  --tmpfs /tmp \
  --cap-drop ALL \
  --cap-add NET_BIND_SERVICE \
  -p "127.0.0.1:$PORT:443/tcp" \
  -p "127.0.0.1:$PORT:443/udp" \
  -v "$CERT_DIR:/certs:ro" \
  -e "DOCS_HOSTNAME=$HOSTNAME" \
  -e "DOCS_TLS_CERT=/certs/live/$CERT_NAME/fullchain.pem" \
  -e "DOCS_TLS_KEY=/certs/live/$CERT_NAME/privkey.pem" \
  -e "DOCS_HTTP3=$HTTP3" \
  "${SHM_ARGS[@]}" \
  "$IMAGE_NAME"

rm -rf "$REMOTE_TMP_DIR" "$REMOTE_TMP_DIR.tar.gz"

sleep 2
if [ "$(docker inspect -f '{{.State.Running}}' "$CONTAINER_NAME")" != true ]; then
  echo "Container exited:" >&2
  docker logs "$CONTAINER_NAME" >&2
  exit 1
fi
docker logs "$CONTAINER_NAME"

# Drop this site's previous image; the runtime layers it shared stay.
if [ -n "$OLD_IMAGE_ID" ] && [ "$OLD_IMAGE_ID" != "$(docker image inspect --format '{{.Id}}' "$IMAGE_NAME")" ]; then
  docker image rm "$OLD_IMAGE_ID" >/dev/null 2>&1 || true
fi
EOF

cat <<EOF

Deployed: https://$HOSTNAME/

npquicrouter needs a route for it (once), in its config's "routes":
  { "sni": "$HOSTNAME", "tcp_backend": "127.0.0.1:$PORT", "udp_backend": "127.0.0.1:$PORT",
    "shm_ingress_channel": "$SHM_CHANNEL", "shm_egress_channel": "$SHM_CHANNEL",
    "shm_ingress_ring_kib": 512, "shm_egress_ring_kib": 1024 }
(Without --shm-channel, leave out the four shm_ fields.)
EOF

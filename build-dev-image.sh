#!/bin/bash
# Build NPRPC development Docker image with all artifacts

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$SCRIPT_DIR"

IMAGE_NAME="${IMAGE_NAME:-nprpc-dev}"
IMAGE_TAG="${IMAGE_TAG:-latest}"

echo "Building NPRPC development image..."
echo "  Image: ${IMAGE_NAME}:${IMAGE_TAG}"
echo "  Context: ${PROJECT_ROOT}"

# Build from nprpc root (so we can copy parent directory)
cd "$PROJECT_ROOT"

export DOCKER_BUILDKIT=1

docker build \
    -f Dockerfile.dev \
    -t "${IMAGE_NAME}:${IMAGE_TAG}" \
    .

echo ""
echo "✓ Image built successfully: ${IMAGE_NAME}:${IMAGE_TAG}"
echo ""
echo "Usage examples:"
echo "  # Run interactive shell"
echo "  docker run -it --rm ${IMAGE_NAME}:${IMAGE_TAG}"
echo ""
echo "  # Mount your project and build"
echo "  docker run --rm -v \$(pwd):/project -w /project ${IMAGE_NAME}:${IMAGE_TAG} swift build"
echo ""
echo "  # Use in Dockerfile"
echo "  FROM ${IMAGE_NAME}:${IMAGE_TAG}"
echo ""

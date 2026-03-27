#!/bin/bash
# Build NPRPC development Docker image with all artifacts

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$SCRIPT_DIR"

IMAGE_NAME="${IMAGE_NAME:-nprpc-dev}"
IMAGE_TAG="${IMAGE_TAG:-latest}"

get_git_commit() {
    local dir="$1"

    if git -C "$dir" rev-parse HEAD >/dev/null 2>&1; then
        git -C "$dir" rev-parse HEAD
    else
        printf '%s' "unavailable"
    fi
}

build_third_party_commit_list() {
    local third_party_dir="$PROJECT_ROOT/third_party"
    local commit_list=""
    local name

    [ -d "$third_party_dir" ] || return 0

    while IFS= read -r name; do
        [ -n "$name" ] || continue

        if [ -n "$commit_list" ]; then
            commit_list+="|"
        fi

        commit_list+="${name}=$(get_git_commit "$third_party_dir/$name")"
    done < <(find "$third_party_dir" -mindepth 1 -maxdepth 1 -type d -printf '%f\n' | sort)

    printf '%s' "$commit_list"
}

PROJECT_GIT_COMMIT="${PROJECT_GIT_COMMIT:-$(get_git_commit "$PROJECT_ROOT")}"
THIRD_PARTY_GIT_COMMITS="${THIRD_PARTY_GIT_COMMITS:-$(build_third_party_commit_list)}"

echo "Building NPRPC development image..."
echo "  Image: ${IMAGE_NAME}:${IMAGE_TAG}"
echo "  Context: ${PROJECT_ROOT}"

# Build from nprpc root (so we can copy parent directory)
cd "$PROJECT_ROOT"

export DOCKER_BUILDKIT=1

docker build \
    -f Dockerfile.dev \
    -t "${IMAGE_NAME}:${IMAGE_TAG}" \
    --build-arg NPRPC_PROJECT_GIT_COMMIT="${PROJECT_GIT_COMMIT}" \
    --build-arg NPRPC_THIRD_PARTY_GIT_COMMITS="${THIRD_PARTY_GIT_COMMITS}" \
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

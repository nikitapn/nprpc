#!/usr/bin/env bash
# Prepare a fresh Ubuntu 24.04 checkout for a full build and the test suite:
# system packages, every submodule, and the test certificate.
#
#   scripts/setup-dev-ubuntu.sh            # packages + submodules + certs
#   scripts/setup-dev-ubuntu.sh --no-apt   # skip the package install
#
# Then configure with GCC 14 (see docs/BUILD.md):
#   cmake -G Ninja -S . -B .build -DCMAKE_C_COMPILER=gcc-14 \
#     -DCMAKE_CXX_COMPILER=g++-14 -DNPRPC_USE_BORINGSSL=ON \
#     -DNPRPC_ENABLE_QUIC=ON -DNPRPC_ENABLE_HTTP3=ON
#
# Safe to re-run: every step skips what is already in place.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

install_packages=1
[[ "${1:-}" == "--no-apt" ]] && install_packages=0

sudo=""
[[ ${EUID:-$(id -u)} -ne 0 ]] && sudo="sudo"

if (( install_packages )); then
  # g++-14: GCC 13 lacks deducing this (P0847), which npidl uses, and Clang 18
  # with GCC 13's libstdc++ has no std::expected.
  # libgmock-dev, libbenchmark-dev: GTest's CMake package wants gmock; the
  # benchmarks (and benchmark_server) are skipped without Google Benchmark.
  $sudo apt-get update
  $sudo env DEBIAN_FRONTEND=noninteractive apt-get install -y \
    build-essential cmake ninja-build pkg-config git \
    gcc-14 g++-14 \
    libboost-dev libboost-program-options-dev \
    liburing-dev libssl-dev \
    libgtest-dev libgmock-dev libbenchmark-dev
fi

# .gitmodules uses SSH URLs; rewrite them to HTTPS so a machine without a
# GitHub SSH key can fetch. Passed per command, so the repo config is untouched.
git_https=(git -c "url.https://github.com/.insteadOf=git@github.com:")

# A submodule directory that exists but has no files: never fetched, or left
# empty by an interrupted update.
needs_checkout() {
  [[ -z "$(ls -A "$1" 2>/dev/null | grep -v '^\.git$')" ]]
}

# BoringSSL's home, boringssl.googlesource.com, is unreachable from some
# networks. Try it, then fall back to the GitHub mirror at the pinned commit.
boringssl=third_party/boringssl
if needs_checkout "$boringssl"; then
  if ! "${git_https[@]}" submodule update --init --depth 1 -- "$boringssl"; then
    pin="$(git ls-tree HEAD "$boringssl" | awk '{print $3}')"
    echo "googlesource unreachable; fetching BoringSSL $pin from GitHub" >&2
    rm -rf "$boringssl"
    git init -q "$boringssl"
    git -C "$boringssl" remote add origin https://github.com/google/boringssl.git
    git -C "$boringssl" fetch -q --depth 1 origin "$pin"
    git -C "$boringssl" checkout -q FETCH_HEAD
  fi
fi

# Everything else, including the submodules of ngtcp2, nghttp2 and nghttp3.
mapfile -t others < <(git config -f .gitmodules --get-regexp '\.path$' |
                      awk '{print $2}' | grep -vx "$boringssl")
"${git_https[@]}" submodule update --init --recursive --depth 1 -- "${others[@]}"

# An update interrupted mid-checkout leaves the files staged as deleted and
# the directory empty; restore them from the commit already fetched.
for path in "${others[@]}"; do
  if needs_checkout "$path"; then
    git -C "$path" checkout -f HEAD -- .
  fi
done

# The short-lived localhost certificate the HTTPS, QUIC and WebTransport tests use.
bash certs/create.sh --if-stale

echo "Ready. Configure with GCC 14, e.g.:"
echo "  cmake -G Ninja -S . -B .build -DCMAKE_C_COMPILER=gcc-14 -DCMAKE_CXX_COMPILER=g++-14 \\"
echo "    -DNPRPC_USE_BORINGSSL=ON -DNPRPC_ENABLE_QUIC=ON -DNPRPC_ENABLE_HTTP3=ON"

# NPRPC task runner — run `just` to list recipes.
# Config: source `.env` for BUILD_DIR / BUILD_TYPE (defaults below).

set dotenv-load := true
set shell := ["bash", "-euo", "pipefail", "-c"]

build_dir := env_var_or_default("BUILD_DIR", ".build_relwith_debinfo")
build_type := env_var_or_default("BUILD_TYPE", "RelWithDebInfo")
nproc := `nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4`

# ── default ──────────────────────────────────────────────────────────────────

# List available recipes
default:
    @just --list --unsorted

# ── configure / build ────────────────────────────────────────────────────────

# Configure the CMake project (Ninja, all features)
configure:
    cmake -G Ninja -S . -B "{{build_dir}}" \
      -DCMAKE_BUILD_TYPE="{{build_type}}" \
      -DNPRPC_USE_BORINGSSL=ON \
      -DNPRPC_BUILD_TESTS=ON \
      -DNPRPC_BUILD_TOOLS=ON \
      -DNPRPC_ENABLE_QUIC=ON \
      -DNPRPC_ENABLE_HTTP3=ON \
      -DNPRPC_ENABLE_SSR=ON \
      -DNPRPC_BUILD_DEV_DOCKER=ON \
      -DNPRPC_BUILD_EXAMPLES=ON

# Build a single CMake target (e.g. `just bt nprpc_test`)
bt target:
    cmake --build "{{build_dir}}" --target "{{target}}" -j{{nproc}}

# Build the whole project
build:
    cmake --build "{{build_dir}}" -j{{nproc}}

# ── helpers ──────────────────────────────────────────────────────────────────

[private]
_ensure-bpf binary:
    #!/usr/bin/env bash
    set -euo pipefail
    binary="{{binary}}"
    if [[ ! -x "$binary" ]]; then
      echo "binary not found or not executable: $binary" >&2
      exit 1
    fi
    command -v getcap >/dev/null
    command -v setcap >/dev/null
    caps="$(getcap "$binary" 2>/dev/null || true)"
    if [[ "$caps" == *"cap_net_admin"* && "$caps" == *"cap_bpf"* ]]; then
      exit 0
    fi
    if [[ ${EUID:-$(id -u)} -eq 0 ]]; then
      setcap cap_net_admin,cap_bpf+ep "$binary"
    else
      sudo setcap cap_net_admin,cap_bpf+ep "$binary"
    fi

[private]
_kill-test-procs:
    #!/usr/bin/env bash
    killall -9 npnameserver nprpc_server_test nprpc_test benchmark_server \
      grpc_benchmark_server capnp_benchmark_server 2>/dev/null || true

# ── tests ────────────────────────────────────────────────────────────────────

# Build and run C++ tests (pass ctest args after --, e.g. `just run-cpp-tests -R TestBasic`)
run-cpp-tests *args: (bt "nprpc_test") (bt "nprpc_server_test") (bt "test_http_utils")
    #!/usr/bin/env bash
    set -euo pipefail
    just _ensure-bpf "{{build_dir}}/test/nprpc_test"
    just _kill-test-procs
    set +e
    timeout 60 ctest --test-dir "{{build_dir}}/test" --output-on-failure {{args}}
    code=$?
    echo "Tests exited with code $code"
    exit $code

# Build server binary and run JavaScript/TypeScript tests
run-js-tests: (bt "nprpc_server_test")
    #!/usr/bin/env bash
    set -euo pipefail
    cd test/js
    if [[ ! -d node_modules ]]; then
      npm ci
    fi
    npm run build
    killall -9 nprpc_server_test npnameserver 2>/dev/null || true
    npm test

# Generate Swift stubs then run Swift package tests (Docker)
run-swift-tests: gen-swift-stubs
    #!/usr/bin/env bash
    set -euo pipefail
    cd nprpc_swift
    if [[ ! -d ../.build_ubuntu_swift/boost_install/include/boost ]]; then
      echo "Boost not found – building via docker-build-boost.sh …"
      bash docker-build-boost.sh
    fi
    bash docker-build-nprpc.sh
    bash docker-build-swift.sh --test

# Run C++, JS, and Swift test suites (wrapper around run_all_tests.py)
test-all *args:
    python3 run_all_tests.py {{args}}

# Alias: run C++ tests
test: run-cpp-tests

# ── stubs / codegen ──────────────────────────────────────────────────────────

# Generate Swift stubs from IDL (requires npidl built)
gen-swift-stubs: (bt "npidl")
    #!/usr/bin/env bash
    set -euo pipefail
    export NPIDL="{{build_dir}}/npidl/npidl"
    export BUILD_DIR="{{build_dir}}"
    bash nprpc_swift/gen_stubs.sh

# Quick npidl smoke: regenerate nprpc_test stubs into the build tree
gen-test-idl: (bt "npidl")
    #!/usr/bin/env bash
    set -euo pipefail
    NPIDL="{{build_dir}}/npidl/npidl"
    "$NPIDL" --cpp --output-dir /tmp test/idl/nprpc_test.npidl
    cp /tmp/nprpc_test.cpp "{{build_dir}}/nprpc_test_stub/src/gen/"
    cp /tmp/nprpc_test.hpp "{{build_dir}}/nprpc_test_stub/src/gen/include/"

# ── benchmarks ───────────────────────────────────────────────────────────────

# Build and run Google Benchmark suite (pass filters after --)
run-benchmarks *args: (bt "nprpc_benchmarks")
    #!/usr/bin/env bash
    set -euo pipefail
    just _ensure-bpf "{{build_dir}}/benchmark/benchmark_server"
    just _kill-test-procs
    "./{{build_dir}}/benchmark/nprpc_benchmarks" {{args}} 2>&1 #\
      #| awk '/----------/{f=1} /NPRPC Benchmark Environment Teardown/{f=0} f'

# ── docker / images ──────────────────────────────────────────────────────────

# Build the nprpc-dev Docker image (`just build-dev-image` or `just build-dev-image myrepo/nprpc 1.0`)
build-dev-image image="nprpc-dev" tag="latest":
    #!/usr/bin/env bash
    set -euo pipefail
    root="$(pwd)"
    image="${IMAGE_NAME:-{{image}}}"
    tag="${IMAGE_TAG:-{{tag}}}"
    get_git_commit() {
      if git -C "$1" rev-parse HEAD >/dev/null 2>&1; then
        git -C "$1" rev-parse HEAD
      else
        printf '%s' "unavailable"
      fi
    }
    third_party_commits="${THIRD_PARTY_GIT_COMMITS:-}"
    if [[ -z "$third_party_commits" && -d third_party ]]; then
      while IFS= read -r name; do
        [[ -n "$name" ]] || continue
        [[ -n "$third_party_commits" ]] && third_party_commits+="|"
        third_party_commits+="${name}=$(get_git_commit "third_party/$name")"
      done < <(find third_party -mindepth 1 -maxdepth 1 -type d -printf '%f\n' | sort)
    fi
    project_commit="${PROJECT_GIT_COMMIT:-$(get_git_commit "$root")}"
    export DOCKER_BUILDKIT=1
    echo "Building ${image}:${tag} …"
    docker build \
      -f Dockerfile.dev \
      -t "${image}:${tag}" \
      --build-arg NPRPC_PROJECT_GIT_COMMIT="${project_commit}" \
      --build-arg NPRPC_THIRD_PARTY_GIT_COMMITS="${third_party_commits}" \
      .
    echo "✓ Image built: ${image}:${tag}"

# ── profiling / misc ─────────────────────────────────────────────────────────

# Profile LargeData10MB TCP benchmark with perf
profile:
    #!/usr/bin/env bash
    set -euo pipefail
    cmake -S . -B .build_perf -G Ninja \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_CXX_FLAGS="-fno-omit-frame-pointer" \
      -DNPRPC_BUILD_TESTS=ON \
      -DNPRPC_BUILD_TOOLS=ON \
      -DNPRPC_ENABLE_QUIC=ON \
      -DNPRPC_ENABLE_HTTP3=ON \
      -DNPRPC_ENABLE_SSR=ON \
      -DNPRPC_BUILD_EXAMPLES=ON
    cmake --build .build_perf --target nprpc_benchmarks -j{{nproc}}
    just _kill-test-procs
    NPRPC_URING=1 perf record -g -F 999 \
      .build_perf/benchmark/nprpc_benchmarks \
      --benchmark_filter="LargeData10MB/1" \
      --benchmark_min_time=5s
    perf report --no-children -g graph,0.5,caller

# Profile HTTP/3 1MB serving (delegates to profile_http3_1mb.sh)
profile-http3-1mb *args:
    ./profile_http3_1mb.sh {{args}}

# Line counts (cloc), excluding generated/third_party noise
statistics:
    cloc . --exclude-lang=SVG,XML,zsh \
      --fullpath \
      --not-match-d='build|gen|Generated|node_modules|third_party|dist|\.build_.*|\.cache|\.github|\.svelte-kit|\.clang-format' \
      --not-match-f='nprpc_nameserver\.hpp|package-lock\.json|nprpc_node\.hpp|nprpc_base\.hpp'

# ── examples ─────────────────────────────────────────────────────────────────

# Live-blog: generate stubs
live-blog-stubs:
    python3 examples/live-blog/scripts/gen_stubs.py

# Live-blog: build Swift server
live-blog-build-swift *args:
    bash examples/live-blog/scripts/build-swift-server.sh {{args}}

# Live-blog: run Swift server
live-blog-run-swift *args:
    bash examples/live-blog/scripts/run-swift-server.sh {{args}}

# ── npquicrouter ─────────────────────────────────────────────────────────────

# Build (and optionally deploy) npquicrouter for VPS
npquicrouter-build-vps *args:
    bash npquicrouter/build-vps.sh {{args}}

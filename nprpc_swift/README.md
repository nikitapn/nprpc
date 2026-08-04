# NPRPC Swift Bindings

Swift bindings for [NPRPC](https://github.com/) using Swift 6+ C++ interoperability.

## Requirements

- Swift 6.3+ (C++ interop)
- **libnprpc** installed system-wide **or** a monorepo CMake build (developer mode)
- Linux (primary) or macOS
- System Boost (same version/ABI as the libnprpc you link)

## Consumer install (GitHub / SPM-friendly)

Build and install the C++ library once:

```bash
cmake -S . -B .build_release -DCMAKE_BUILD_TYPE=Release -DNPRPC_BUILD_TESTS=OFF
cmake --build .build_release -j$(nproc)
sudo cmake --install .build_release
sudo ldconfig
```

Headers land under the install prefix’s `include/` (e.g. `/usr/local/include`),
the shared library under `lib/` (e.g. `/usr/local/lib`).

Then either build this package in isolation:

```bash
cd nprpc_swift
swift build
swift test
```

or depend on it from another package (no `unsafeFlags` in consumer mode):

```swift
// Package.swift
dependencies: [
    .package(url: "https://github.com/<org>/nprpc.git", from: "1.0.0"),
    // if the Swift package lives at monorepo path nprpc_swift/:
    // use a dedicated tag/path as you publish it
],
targets: [
    .target(name: "MyApp", dependencies: [
        .product(name: "NPRPC", package: "nprpc"), // adjust package name
    ]),
]
```

`Package.swift` uses **no `unsafeFlags`** when it does not detect a monorepo
tree and `NPRPC_ROOT` is unset (or when `NPRPC_SYSTEM_ONLY=1`). Linking is
simply `-lnprpc` on the default search path.

## Developer / monorepo mode

When this package lives next to the nprpc CMake tree (`../include/nprpc/...`),
`Package.swift` **auto-detects** the monorepo root and adds include/lib/rpath
flags for the in-tree build (defaults to `.build_relwith_debinfo`).

Override explicitly:

```bash
export NPRPC_ROOT=/path/to/nprpc
export NPRPC_BUILD_DIR=.build_relwith_debinfo   # or absolute path
cd nprpc_swift && swift build
```

From the monorepo root:

```bash
just run-swift-benchmarks
# or
cd nprpc_swift && NPRPC_BUILD_DIR=.build_relwith_debinfo swift test
```

> **Note:** monorepo mode uses SPM `unsafeFlags` for `-I`/`-L`/`-rpath`. That is
> fine for local development. Published / dependency use must rely on a
> **system install** so the resolved package has no unsafe flags.

## Generate stubs

```bash
# from monorepo root
./nprpc_swift/gen_stubs.sh
# or
just gen-swift-stubs
```

## Benchmarks

```bash
just run-swift-benchmarks
just run-swift-benchmarks-baseline
```

Results: `benchmark/results/swift/`.

## Layout

```
nprpc_swift/
├── Package.swift                 # system-default; monorepo via env/auto-detect
├── Sources/
│   ├── CNprpc/                   # C++ bridge (C API for Swift)
│   │   ├── include/
│   │   │   ├── module.modulemap
│   │   │   ├── nprpc_bridge.hpp
│   │   │   └── nprpc_bridge_log.hpp
│   │   └── nprpc_bridge.cpp
│   ├── NPRPC/                    # Swift API
│   └── NPRPCBenchmark/           # Swift↔Swift microbenchmarks
└── Tests/NPRPCTests/
```

## Architecture

```
Your Swift code
    → NPRPC (async wrappers, marshalling)
    → CNprpc (C bridge + FlatBuffer helpers)
    → libnprpc.so (transports, POA, streams)
```

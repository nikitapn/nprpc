# Building and Installing NPRPC

## Prerequisites

- **Linux.** The TCP transport is built on epoll and io_uring, so any build
  with TCP enabled is Linux-only. On other platforms, configure with
  `-DNPRPC_ENABLE_TCP=OFF`; that configuration is not tested.
- **A C++23 compiler:** a recent GCC or Clang.
- **CMake 3.15+**, and **pkg-config**.
- **Boost** (headers; Boost.ProgramOptions for the tools).
- **liburing**, when TCP is enabled.
- **OpenSSL**, for HTTPS/WSS. Not needed with `NPRPC_USE_BORINGSSL=ON`, which
  builds BoringSSL from the bundled submodule, or when TLS is off.
- Optional: **GoogleTest** for the tests, **Node.js** for the TypeScript
  package, **Swift 6.3** for the Swift package, **libclang** and **md4c** for
  the API docs tool.

On Debian or Ubuntu:

```bash
sudo apt install -y build-essential cmake ninja-build pkg-config \
  libboost-dev libboost-program-options-dev liburing-dev libssl-dev libgtest-dev
```

## Building

```bash
git clone --recursive https://github.com/nikitapn/nprpc.git
cd nprpc
cmake -G Ninja -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
sudo cmake --install build   # optional
```

If you cloned without `--recursive`, run `git submodule update --init
--recursive` first; QUIC, HTTP/3 and BoringSSL are built from submodules.

The `justfile` wraps the common configurations. `just configure` enables
everything, including QUIC, HTTP/3, BoringSSL, tests and examples.

### Options

| Option | Default | Effect |
|---|---|---|
| `NPRPC_ENABLE_TCP` | ON | Native TCP transport (Linux, needs liburing) |
| `NPRPC_ENABLE_HTTP` | ON | HTTP/1.1 server: static files, RPC over HTTP, WebSocket upgrades |
| `NPRPC_ENABLE_WEBSOCKET` | ON | WebSocket transport (WS/WSS) |
| `NPRPC_ENABLE_SSL` | ON | TLS for HTTPS and WSS. HTTP/3 and QUIC bring their own TLS. |
| `NPRPC_ENABLE_QUIC` | OFF | Native QUIC transport (builds MsQuic) |
| `NPRPC_ENABLE_HTTP3` | OFF | HTTP/3 and WebTransport (nghttp3/ngtcp2) |
| `NPRPC_USE_BORINGSSL` | OFF | Use the bundled BoringSSL instead of system OpenSSL |
| `BUILD_SHARED_LIBS` | ON | Shared rather than static library |
| `NPRPC_BUILD_TOOLS` | ON* | `npidl`, `npnameserver`, and `npdoc` if libclang and md4c are found |
| `NPRPC_BUILD_TESTS` | ON* | Test suite (needs TCP, HTTP, WebSocket and SSL all on) |
| `NPRPC_BUILD_JS` | ON* | TypeScript package |
| `NPRPC_BUILD_ROUTER` | ON* | `npquicrouter`, the SNI router for HTTP/3 sites |
| `NPRPC_BUILD_EXAMPLES` | OFF | Examples, including the Docker-built Swift live-blog server |
| `NPRPC_BUILD_DEV_DOCKER` | OFF | The `nprpc-dev:latest` image; see [DOCKER_DEV_IMAGE.md](DOCKER_DEV_IMAGE.md) |
| `NPRPC_INSTALL` | ON* | Install targets |

\* ON when NPRPC is the top-level project, OFF when it is added with
`add_subdirectory`.

Shared memory is always available. Every transport can be switched off; a
shared-memory-only build needs neither OpenSSL nor liburing:

```bash
cmake -S . -B build \
  -DNPRPC_ENABLE_TCP=OFF -DNPRPC_ENABLE_HTTP=OFF -DNPRPC_ENABLE_WEBSOCKET=OFF \
  -DNPRPC_ENABLE_SSL=OFF -DNPRPC_ENABLE_QUIC=OFF -DNPRPC_ENABLE_HTTP3=OFF
```

Each enabled transport defines `NPRPC_ENABLE_<NAME>` for code that uses the
library, so you can compile features conditionally. Asking `RpcBuilder` for a
transport that was compiled out throws from `build()`.

## Using NPRPC in your project

### CMake

After installing:

```cmake
find_package(nprpc REQUIRED)

npidl_generate_idl_files("${CMAKE_CURRENT_SOURCE_DIR}/idl/myservice.npidl" myservice_stub)

add_executable(myapp main.cpp ${myservice_stub_GENERATED_SOURCES})
target_include_directories(myapp PRIVATE ${myservice_stub_INCLUDE_DIR})
target_link_libraries(myapp PRIVATE nprpc::nprpc)
```

`npidl_generate_idl_files` runs npidl on the IDL and exposes the generated
sources and include directory. To vendor NPRPC instead, use
`add_subdirectory(external/nprpc)` and the same `nprpc::nprpc` target.

### Swift

Add the `nprpc_swift` package and depend on its `NPRPC` product (and
`NPRPCWeb` for server-rendered pages). Enable C++ interoperability on targets
that import it. The [development image](DOCKER_DEV_IMAGE.md) has it pre-built.

### TypeScript

The `nprpc` npm package is built from `nprpc_js/`. Generate stubs with
`npidl --ts`.

## Installed files

| What | Where |
|---|---|
| Headers | `${CMAKE_INSTALL_PREFIX}/include/nprpc/` |
| Library | `${CMAKE_INSTALL_PREFIX}/lib/` |
| `npidl`, `npnameserver` | `${CMAKE_INSTALL_PREFIX}/bin/` |
| CMake package | `${CMAKE_INSTALL_PREFIX}/lib/cmake/nprpc/` |

CMake has no uninstall target; `xargs rm < build/install_manifest.txt`
removes what was installed.

## Troubleshooting

- **OpenSSL not found:** `-DOPENSSL_ROOT_DIR=/path/to/openssl`, or use
  `-DNPRPC_USE_BORINGSSL=ON`.
- **Boost not found:** `-DBOOST_ROOT=/path/to/boost`.
- **GTest not found:** install it, pass `-DGTest_DIR=...`, or disable tests
  with `-DNPRPC_BUILD_TESTS=OFF`.
- **liburing not found:** install `liburing-dev`, or build without TCP.

## For contributors

### Tests

```bash
ctest --test-dir build --output-on-failure   # C++
just run-js-tests                           # TypeScript
just run-swift-tests-host                   # Swift, against the CMake build
just run-swift-tests                        # Swift, inside Docker (slower)
```

Run the whole suite (C++, TypeScript and Swift) before merging to `main`:

```bash
just test-all --swift-host
```

Host Swift tests need `sudo setcap cap_net_admin,cap_bpf+ep` on the test
binary so HTTP/3 can attach its eBPF socket router; the script does this and
may prompt for your password.

### Docs

`just docs-api` extracts the API from the C++ headers, the Swift package and
the IDL, and `just docs-serve` serves the documentation site at
<http://localhost:8080>. See `npdoc/README.md` and `docs/site/README.md`.

### Development image

```bash
cmake -S . -B build -DNPRPC_BUILD_DEV_DOCKER=ON
cmake --build build --target nprpc_dev_docker          # builds if inputs changed
cmake --build build --target nprpc_dev_docker_rebuild  # forces a rebuild
```

For production, `just build-runtime-image` makes a small base image from the
dev image; see [DOCKER_RUNTIME_IMAGE.md](DOCKER_RUNTIME_IMAGE.md).

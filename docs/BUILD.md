# Building and Installing NPRPC

## Prerequisites

- **Linux.** The TCP transport is built on epoll and io_uring, so any build
  with TCP enabled is Linux-only. On other platforms, configure with
  `-DNPRPC_ENABLE_TCP=OFF`; that configuration is not tested.
- **A C++23 compiler:** GCC 14+, or a Clang whose standard library has
  `std::expected`. GCC 13 (Ubuntu 24.04's default) lacks deducing `this`, and
  Clang 18 with GCC 13's libstdc++ has no `std::expected`; both fail in npidl.
- **CMake 3.15+**, and **pkg-config**.
- **Boost** (headers; Boost.ProgramOptions for the tools).
- **liburing**, when TCP is enabled.
- **OpenSSL**, for HTTPS/WSS, native QUIC and HTTP/3. Native QUIC and HTTP/3
  require OpenSSL 3.5+ for the ngtcp2 OpenSSL backend. Alternatively, use
  `NPRPC_USE_BORINGSSL=ON` to build BoringSSL from the bundled submodule.
  Neither library is needed when HTTPS/WSS, QUIC and HTTP/3 are all disabled.
- Optional: **GoogleTest** for the tests, **Node.js** for the TypeScript
  package, **Swift 6.4.1** for the Swift package, **libclang** and **md4c** for
  the API docs tool.

On Ubuntu 24.04, `scripts/setup-dev-ubuntu.sh` (or `just setup-ubuntu`)
installs the packages below, fetches every submodule over HTTPS, and creates
the test certificate. By hand:

```bash
sudo apt install -y build-essential cmake ninja-build pkg-config gcc-14 g++-14 \
  libboost-dev libboost-program-options-dev liburing-dev libssl-dev \
  libgtest-dev libgmock-dev libbenchmark-dev
```

and configure with `-DCMAKE_C_COMPILER=gcc-14 -DCMAKE_CXX_COMPILER=g++-14`.
`libbenchmark-dev` is optional; without it the benchmarks, `benchmark_server`
included, are skipped.

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
| `NPRPC_ENABLE_SSL` | ON | TLS for HTTPS and WSS. QUIC and HTTP/3 configure TLS separately. |
| `NPRPC_ENABLE_QUIC` | OFF | Native QUIC RPC transport using ngtcp2 |
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

### QUIC and HTTP/3

Native QUIC and HTTP/3 share the bundled ngtcp2 library. HTTP/3 also uses
nghttp3 for HTTP framing; native QUIC carries binary NPRPC frames directly.
They use separate listeners and ports: `.with_quic(...)` enables native RPC,
while `.enable_http3()` enables HTTP/3 on the HTTP port.

Both use the TLS backend selected by `NPRPC_USE_BORINGSSL`. For a system
OpenSSL older than 3.5, select the bundled BoringSSL backend:

```bash
cmake -G Ninja -S . -B build \
  -DNPRPC_ENABLE_QUIC=ON -DNPRPC_ENABLE_HTTP3=ON \
  -DNPRPC_USE_BORINGSSL=ON
cmake --build build
```

Enabling either transport builds ngtcp2 and links it statically into
`libnprpc`. `NPRPC_ENABLE_SSL=OFF` only disables HTTPS/WSS; QUIC and HTTP/3
still require their TLS backend. See the monitor's
[QUIC authentication guide](../nprpc_monitor/README.md#quic-authentication)
for server trust, client certificates and mTLS configuration.

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
- **ngtcp2 OpenSSL backend lacks QUIC support:** use OpenSSL 3.5+ or configure
  with `-DNPRPC_USE_BORINGSSL=ON`.
- **Boost not found:** `-DBOOST_ROOT=/path/to/boost`.
- **GTest not found:** install it, pass `-DGTest_DIR=...`, or disable tests
  with `-DNPRPC_BUILD_TESTS=OFF`.
- **liburing not found:** install `liburing-dev`, or build without TCP.
- **`expected identifier before 'this'` or `no template named 'expected'`
  in npidl:** the compiler is too old; use GCC 14 (see Prerequisites).
- **Undefined `boost::program_options` symbols linking npidl:** install
  `libboost-program-options-dev`; the Boost headers alone are not enough.
- **Submodule clone fails with `Permission denied (publickey)`:**
  `.gitmodules` uses SSH URLs. Without a GitHub SSH key, run
  `git -c url."https://github.com/".insteadOf="git@github.com:" submodule update --init --recursive`,
  which is what the setup script does.
- **BoringSSL clone fails (googlesource.com unreachable):** fetch the pinned
  commit from the GitHub mirror, `https://github.com/google/boringssl.git`;
  the setup script falls back to it on its own.
- **`Cannot find source file: sfparse/sfparse.c`:** nghttp3's own
  submodules are missing; update with `--recursive`.

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

Host Swift tests need no `setcap`: they run HTTP/3 with a single worker, which
needs no eBPF socket router. (File capabilities would also stop the test
runners loading their libraries, which they find relative to themselves.)

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

# Notes for working in this repo

Build and test instructions for people live in [docs/BUILD.md](docs/BUILD.md).
This file keeps what a fresh machine, such as a cloud session, trips over.

## Setting up a fresh checkout

```bash
scripts/setup-dev-ubuntu.sh      # apt packages, all submodules, test cert
cmake -G Ninja -S . -B .build -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_COMPILER=gcc-14 -DCMAKE_CXX_COMPILER=g++-14 \
  -DNPRPC_USE_BORINGSSL=ON -DNPRPC_ENABLE_QUIC=ON -DNPRPC_ENABLE_HTTP3=ON \
  -DNPRPC_BUILD_JS=OFF
cmake --build .build -j"$(nproc)"
```

- Submodules start empty in a fresh clone. `.gitmodules` uses SSH URLs and
  BoringSSL lives on googlesource.com; the script rewrites the URLs to HTTPS
  and falls back to BoringSSL's GitHub mirror.
- Use GCC 14. Ubuntu 24.04's GCC 13, and Clang 18 on its libstdc++, both fail
  to compile npidl.
- `just` may not be installed; the recipes in `justfile` are thin wrappers
  around the cmake/ctest commands, so run those directly.
- A full build from scratch is slow (BoringSSL, ngtcp2 and the tests). Build
  single targets while iterating: `cmake --build .build --target test_http_utils`.

## Tests

```bash
ctest --test-dir .build/test -E HTTP3Transport --output-on-failure
```

- Fast unit tests for the HTTP layer: `HttpUtils` (`test_http_utils`) and
  `HttpCompression` (`test_http_compression`).
- Many tests fork `npnameserver` on a fixed port. A run that was killed leaves
  one behind, and every later run fails with "Failed to start nameserver
  process" (the real error is in `/tmp/npnameserver_stderr.log`:
  `Address already in use`). `pkill -f npnameserver` first.
- In a Claude Code cloud sandbox these failed on an unmodified checkout too
  (cause not tracked down), so compare against the base commit before
  blaming your change: `NprpcTest.TestBasic`,
  `TestException`, `TestOptional`, `TestNested`, `TestFixedSizeArrays`,
  `TestDirect`, `TestLargeMessage`, `HandedOffCallLeavesTransportFree`,
  `nprpc_all_tests`, `QuicTLS`, `QuicListenerLimits`. The `NprpcTest` ones
  pass when run on their own (`-R '^NprpcTest\.'`). `HTTP3Transport` was
  excluded above: its wrapper runs `setcap` on the server binary.
- No Swift toolchain in a plain Ubuntu image: the Swift package can't be built
  there. The C++ half of the bridge (`nprpc_bridge.cpp`) is compiled by
  `test_swift_bridge_errors`, which at least catches C++ errors.

## Running a server by hand

`benchmark/benchmark_server` serves a doc root over HTTP/1.1 and HTTP/3 and is
the quickest end-to-end check of the HTTP code. Its behaviour is set through
`NPRPC_BENCH_*` environment variables (see the source); `NPRPC_HTTP_ROOT_DIR`
picks the doc root.

- It has hardcoded paths under `/home/nikita/projects/nprpc` for the
  certificate and for `npnameserver` (looked up in `.build_relwith_debinfo`).
  Elsewhere: `mkdir -p /home/nikita/projects && ln -s "$PWD" /home/nikita/projects/nprpc`
  and `ln -s .build .build_relwith_debinfo`.
- The HTTP/3 server only opens an IPv6 (dual-stack) UDP socket, so without
  IPv6 it logs "Address family not supported" and only HTTP/1.1 runs.
- The curl in Ubuntu 24.04 has no HTTP/3. `pip install aioquic` gives a
  client; note its `connect()` also opens an IPv6 socket, so without IPv6 set
  up the datagram endpoint on `127.0.0.1` yourself.

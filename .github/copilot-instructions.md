# NPRPC Copilot Instructions
## Architecture & Key Directories
- Core runtime lives in `include/nprpc` + `src/`: `nprpc.hpp` defines Rpc/Poa/Object lifecycles, activation flags, and transport-agnostic session context.
- Transport backends reside in `src/*_connection.cpp` (TCP/WebSocket/HTTP/shared_memory/udp/quic_transport); they all share Buffer/Endpoint utilities from `include/nprpc`.
- IDL sources live under `idl/` (plus per-feature subfolders); `npidl/` houses the compiler and `cmake/npidl.cmake`'s `npidl_generate_idl_files` macro copies generated C++/TS stubs into `<build>/<module>/src/gen/**`.
- Service discovery is implemented in `npnameserver/npnameserver.cpp`; it activates a single servant inside a dedicated POA and stores bound objects by transferring ownership of their `nprpc::Object` instances.
- Benchmarks under `benchmark/src` reuse IDL from `benchmark/idl`; use them when checking regressions against gRPC/Cap'n Proto.
- JavaScript bindings are in `nprpc_js/` (webpack build) and exercised in `test/js`; the current bundle assumes a browser global `self`, so Node-based tests need a shim or a Node flavor build.
## Build & Generation Workflow
- Standard dev configure: `cmake -S . -B .build_relwith_debinfo -DCMAKE_BUILD_TYPE=RelWithDebInfo -DNPRPC_BUILD_TESTS=ON -DNPRPC_BUILD_TOOLS=ON -DNPRPC_BUILD_JS=ON`; Convienience script: `./configure.sh`.
- Build everything with `cmake --build .build_release -j$(nproc)`; npidl, npnameserver, and shared libs all come from the same tree (no separate bootstrap). Although Swift project must be built separately for now inside docker: `cd nprpc_swift && ./gen_stubs.sh && ./docker-build-boost.sh && ./docker-build-nprpc.sh && ./docker-build-swift.sh`.
- To regenerate stubs manually: `npidl path/to/foo.npidl --cpp --ts --swift --output-dir gen`; in CMake, depend on the `<module>_gen` custom target exposed by `npidl_generate_idl_files`.
- POAs are created via `nprpc::PoaBuilder` off an `Rpc`; set limits (`with_max_objects`), lifetime, and optionally `with_object_id_policy` (default system-generated, `UserSupplied` unlocks `activate_object_with_id` for deterministic IDs embedded in JS bundles) before calling the relevant activation API with explicit `ObjectActivationFlags` for allowed transports.
- Nameserver addresses (default TCP 15000, HTTP 15001) come from `RpcBuilder` settings; `Bind` takes ownership of the passed `Object*`, so avoid deleting the servant yourself afterward.
- Generated clients expose `ObjectPtr<T>` plus `.http` or UDP helpers; call `select_endpoint()` when a servant advertises multiple URLs so you pin the preferred transport.
## Testing & Quality Gates
- C++ tests live in `test/src`; building `nprpc_test` automatically triggers `ctest --output-on-failure` because of a POST_BUILD hook, so fix failures before re-running the build.
- Handy targets: `run_nprpc_tests`, `run_nprpc_tests_verbose`, and the focused executables (`test_lock_free_ring_buffer`, `test_shared_memory_endpoint`, etc.) that ctest registers as separate suites.
- JS parity tests run from `test/js` with `npm install && npm test`; ensure `nprpc_js` is built (`npm run build-prd`) and copy the generated `test.ts` from the build tree before running.
- Benchmarks build via `cmake --build .build_release --target nprpc_benchmarks` or the convenience script `./run_benchmark.sh --benchmark_filter=LatencyFixture/LargeData1MB/0` (script auto-builds `.build_release`).
- Swift tests are in `nprpc_swift/Tests`; build the test bundle with `./gen_stubs.sh && ./docker-build-boost.sh && ./docker-build-nprpc.sh && ./docker-build-swift.sh` and run with `./docker-build-swift.sh --test`.
## Contribution Conventions
- Prefer Boost.Asio idioms already used in `src/async_connect.cpp` & friends; all transports run inside the same `boost::asio::io_context`, so avoid creating extra contexts unless you can thread them through `RpcBuilder`.
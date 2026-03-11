# TODO.md

## STD
* [ ] Replace std::unordered_map with absl::flat_hash_map in performance-critical paths.

## Packaging / DX
* [ ] Write a friction-free setup guide for a mixed Swift/C++ + SvelteKit project with `nprpc_node` / `nprpc_shm.node`, including build, runtime layout, and SSR packaging expectations.
* [ ] Provide a minimal starter template or reference example that works out of the box for Swift backend + SvelteKit frontend + NPRPC SSR/addon integration.

## Runtime Configuration
* [ ] Add `RpcBuilder` options for socket send/receive buffer sizes so applications can tune kernel buffers without patching transport code.

## Stability & Security
* [ ] Add long-running soak tests that keep mixed transports active for hours to catch leaks, stuck streams, and reconnection issues.
* [ ] Add high-concurrency load tests for 1k+ simultaneous connections, including connection churn and mixed request sizes, not just latency microbenchmarks.
* [ ] Add malformed-input and protocol fuzzing coverage for HTTP, WebSocket, QUIC, and generated deserializers.
* [ ] Audit and document resource-exhaustion guards: max request size, header size, stream count, idle timeouts, and per-connection memory limits.

## Serialization
* [ ] Add hint attributes [estimated_in_size=x], [estimated_out_size=x] to IDL for preallocating buffers, before method calls.
* [ ] Support flat_buffer view mode in generated code to avoid copies when serializing/deserializing from shared memory.

## Shared Memory Transport
* [ ] Optimize shared memory server session to avoid unnecessary copies when receiving messages.
* [ ] Build on Windows is broken due to missing `sys/mman.h` and `shm_open()`. Need to implement Windows shared memory APIs using `CreateFileMapping` and `MapViewOfFile`.
* [ ] **Add cross-process atomic tests**: Current tests run client/server in same process. Need separate executables to verify atomics work correctly across true process boundaries (different address spaces). This is critical because the recent race bug was single-process (read thread vs io_context thread), not cross-process.

## TCP Transport
* [ ] Encryption support (TLS) for TCP transport.
* [ ] Deflate or zlib compression for large messages.
* [ ] Async socket connect with timeout.

## WebSocket Transport
* [ ] Support openning another WebSocket connection on demand when existing one is busy (for high-throughput clients).

## HTTP/3 Server

## QUIC Transport (MsQuic) ✅ COMPLETE

Motivation: Add a modern, high-performance transport option with built-in encryption and multiplexing. QUIC offers:
- Reliable streams with automatic retransmission
- Unreliable datagrams (RFC 9221) for fire-and-forget
- Built-in congestion control and flow control
- TLS 1.3 encryption
- 0-RTT connection establishment
- Connection migration (survives IP changes)

### Phase 1: Core Integration ✅
* [x] Add MsQuic as submodule/dependency in CMake
* [x] Create `QuicConnection` class (client-side)
  - Wraps `QUIC_CONNECTION` handle
  - Manages streams for RPC calls
  - Integrates with boost::asio io_context
* [x] Create `QuicListener` class (server-side)
  - Accepts incoming QUIC connections
  - Creates server sessions per connection
* [x] Implement `QuicServerSession` for dispatching
* [x] Wire up QUIC to RPC framework (endpoint URL: `quic://host:port`)
* [x] Add `quic` activation flag
* [x] Add `set_listen_quic_port()` to RpcBuilder

### Phase 2: Stream Management ✅
* [x] Use bidirectional streams for request/response RPC (default, reliable)
* [x] Use QUIC DATAGRAM extension for `[unreliable]` methods
* [x] Graceful shutdown (track connections, clear callbacks)
* [ ] Connection pooling and multiplexing (future optimization)

### Phase 3: IDL `[unreliable]` Attribute ✅
* [x] Support `[unreliable]` attribute on methods (not interface-level)
* [x] Transport behavior:
  - **TCP/WebSocket**: Ignore `[unreliable]` (always reliable)
  - **QUIC**: Default reliable (streams), `[unreliable]` uses datagrams

### Phase 4: Testing & Benchmarks ✅
* [x] Unit tests for QUIC transport (TestQuicBasic, TestQuicUnreliable)
* [x] Latency benchmarks vs TCP
* [ ] Throughput benchmarks
* [ ] Connection establishment time (0-RTT)

## HTTP/3 Server ✅ Core Implementation Complete

Serve web clients over HTTP/3 using nghttp3/ngtcp2

### Why HTTP/3?
- Modern web standard, shows technical credibility
- Reuse existing MsQuic infrastructure
- Lower latency for web clients (0-RTT, multiplexing)
- "My personal site runs on HTTP/3" 🚀

### Implementation Status
* [x] Create `Http3Server` class (include/nprpc/impl/http3_server.hpp, src/http3_server.cpp)
  - Integrates with existing Rpc singleton for configuration
  - Reuses certificate handling from QUIC transport
  - Manages connections and request lifecycle
* [x] HTTP/3 request routing
  - Static file serving for web assets
  - RPC endpoint for NPRPC calls (via process_http_rpc)
* [x] CMake integration (NPRPC_ENABLE_HTTP3 option)
* [x] init_http3_server() / stop_http3_server() integration in rpc_impl.cpp
* [x] Test with actual HTTP/3 client (curl --http3, browser)

### Remaining Work
* [ ] Error handling improvements
* [ ] Performance tuning
* [ ] QUIC/HTTP/3 endpoint sharing (same port, ALPN differentiation)
* [ ] CORS headers for browser requests
* [ ] Add an HTTP server option to disable or aggressively invalidate the static file cache during development so rebuilt frontend assets do not require a server restart.
* [ ] Evaluate a better cache invalidation strategy than manual restart, e.g. mtime checks, versioned asset keys, or inotify-based invalidation.

### Ideas for Future Enhancements
| Area | Improvement | Complexity |
|------|-------------|------------|
| Compression | Add gzip/brotli compression for text files (cache compressed versions) | Medium |
| ETag/Caching | Add ETag and If-None-Match support for 304 responses | Low |
| Range requests | Support Range header for video streaming / resumable downloads | Medium |
| Cache warming | Pre-load common files at startup | Low |
| Metrics | Expose cache stats (hit rate, memory usage) via /stats endpoint | Low |
| HTTP/2 | Add HTTP/2 support via nghttp2 (between HTTP/1.1 and HTTP/3) | High |
| sendfile() | For non-cached large files, use sendfile() syscall | Medium |
| Cache-Control | Respect/set Cache-Control headers for browser caching | Low |

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────┐
│                              NPRPC                                  │
├─────────────┬─────────────┬──────────────────────┬──────────────────┤
│  Shared Mem │ TCP/WS/HTTP |  HTTP3/WebTransport  │   Native QUIC    │
│  (IPC)      │ (boost)     │  (ngtcp2/nghttp3)    │     (MsQuic)     │
├─────────────┴─────────────┴──────────────────────┴──────────────────┤
│                      Transport Selection                            │
│  shm://       tcp://                                   quic://      │
│               ws://  wss://     https:// (HTTP/3)                   │
│               http:// https://  wt:// (WebTransport)                │
└─────────────────────────────────────────────────────────────────────┘
```

## Swift Language Bindings ✅ COMPLETE

Full Swift 6.2+ bindings via C++ interop. All core features working:

* [x] Swift package structure (nprpc_swift)
* [x] Swift wrappers for Rpc, Poa, ObjectPtr
* [x] npidl `--swift` code generation
* [x] Marshalling: fundamentals, enums, strings, vectors, arrays, flat/nested structs, optionals, objects
* [x] Servant base classes with dispatch
* [x] Client proxy generation
* [x] Exception propagation (Swift throws ↔ C++ exceptions)
* [x] Async methods (Swift concurrency)
* [x] Object references as parameters
* [x] Streaming RPC (AsyncStream servant → AsyncThrowingStream client)
* [x] Bad input validation ([trusted=false])
* [x] Large message support
* [x] Docker-based build pipeline
* [x] Comprehensive integration test suite
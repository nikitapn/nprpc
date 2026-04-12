# TODO.md

## STD
* [ ] Replace std::unordered_map with absl::flat_hash_map in performance-critical paths. (Partially done in http3_server_nghttp3, need to audit the rest of the codebase for hot paths like pending request tracking, connection maps, etc.)

## Packaging / DX
* [ ] Write a friction-free setup guide for a mixed Swift/C++ + SvelteKit project with `nprpc_node` / `nprpc_shm.node`, including build, runtime layout, and SSR packaging expectations.
* [x] Provide a minimal starter template or reference example that works out of the box for Swift backend + SvelteKit frontend + NPRPC SSR/addon integration.

## Runtime Configuration
* [ ] Add `RpcBuilder` options for socket send/receive buffer sizes so applications can tune kernel buffers without patching transport code.

## Stability & Security
* [ ] Add long-running soak tests that keep mixed transports active for hours to catch leaks, stuck streams, and reconnection issues.
* [ ] Add high-concurrency load tests for 1k+ simultaneous connections, including connection churn and mixed request sizes, not just latency microbenchmarks.
* [ ] Add malformed-input and protocol fuzzing coverage for HTTP, WebSocket, QUIC, and generated deserializers.
* [ ] Audit and document resource-exhaustion guards: max request size, header size, stream count, idle timeouts, and per-connection memory limits.

## Serialization
* [ ] Add hint attributes [estimated_in_size=x], [estimated_out_size=x] to IDL for preallocating buffers, before method calls.

## Shared Memory Transport
* [ ] Optimize shared memory server session to avoid unnecessary copies when sending messages. We can traverse the nested stucts first to calculate the total size, and then reserve the entire buffer once in the ring buffer and that will eliminate memcpy entirely.
* [ ] Build on Windows is broken due to missing `sys/mman.h` and `shm_open()`. Need to implement Windows shared memory APIs using `CreateFileMapping` and `MapViewOfFile`.
* [ ] **Add cross-process atomic tests**: Current tests run client/server in same process. Need separate executables to verify atomics work correctly across true process boundaries (different address spaces). This is critical because the recent race bug was single-process (read thread vs io_context thread), not cross-process.

## TCP Transport
* [ ] Encryption support (TLS) for TCP transport.
* [ ] Deflate or zlib compression for large messages.
* [ ] Async socket connect with timeout.
* [ ] **[Cancellation refactor]** `send_receive_coro` is currently only a true coroutine in `UringClientConnection`; all other transports (`SocketConnection`, WebSocket, shared memory, QUIC) inherit the blocking base-class stub that ignores the `stop_token`. Until they are ported, the only cancellation that works is pre-flight (`stop_requested()` checked at the top of every generated `*Async` body). Proper mid-flight cancellation on non-uring transports requires each one to track pending requests by ID, register a `stop_callback`, and post a resume with `status = -2` when triggered — the same pattern already used in `UringClientConnection::send_receive_coro`.

## WebSocket/WebTransport
* [ ] Support openning another WebSocket connection on demand when existing one is busy (for high-throughput clients).
* [ ] **[HIGH]** Add cancellation support for in-flight regular (non-streaming) RPC calls via `AbortSignal`. When triggered, send a `Cancel` message with the matching request ID over the transport before the reply arrives, then reject the pending promise on the client. Server-side cooperative cancellation is a bonus but not required — letting the handler finish and dropping the reply on the client is acceptable. Requires tracking in-flight request IDs in `PendingRequestMap` (or equivalent) and wiring `AbortSignal` through the generated proxy call signatures.

## HTTP/3 Server

## Streaming
* [ ] **[HIGH]** Improve the window credit flow-control system for server-side and bidi streams. Current implementation grants one credit per consumed chunk (see `send_window_update`), which can cause unnecessary round-trips and head-of-line stalling under burst workloads. Investigate: batching credit returns (return N credits after N chunks rather than 1-at-a-time), a configurable initial window size per stream, and adaptive credit sizing based on observed throughput. Also audit the interaction between the credit system and seek/cancel — credits sent to an already-cancelled stream should be silently dropped.

## HTTP/3 Server

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
* [x] Error handling improvements
* [x] Performance tuning
* [ ] QUIC/HTTP/3 endpoint sharing (same port, ALPN differentiation)
* [x] CORS headers for browser requests
* [x] Add an HTTP server option to disable or aggressively invalidate the static file cache during development so rebuilt frontend assets do not require a server restart.
* [x] Evaluate a better cache invalidation strategy than manual restart, e.g. mtime checks, versioned asset keys, or inotify-based invalidation.

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
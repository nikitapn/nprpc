# TODO.md

## Roadmap (survey, 2026-11-06)

Gaps found by checking the code, not this file, in rough priority order.
Each area lists what to build; the sections further down keep their older,
more specific items.

### 1. HTTP/1.1 parity with HTTP/3 for static files and pages
The first visit, and every client that can't use QUIC, arrives over HTTP/1.1.
* [x] ETag, Last-Modified, `Cache-Control`, `Vary`, and 304s for
  `If-None-Match` / `If-Modified-Since`, as HTTP/3 already had.
* [x] gzip/deflate for static files (the cached compressed copies HTTP/3
  uses) and for page-handler responses. One `compression()` builder option
  now covers both protocols (was `http3_compression()`).
* [x] `If-None-Match` compared weakly with list and `*` support, and
  `If-Modified-Since` at whole seconds, on both protocols.
* [x] Configurable `Cache-Control` per path or extension:
  `cache_control(pattern, value)` (`cacheControl(_:_:)` in Swift), first match
  wins, e.g. `/assets/**` → `public, max-age=31536000, immutable`, `*.html` →
  `no-cache`. Unmatched files keep `public, max-age=3600`.
* [x] Range requests on both protocols: a single `bytes` range gets a 206
  (416 past the end), with `If-Range` and `Accept-Ranges`. Several ranges get
  the whole file; ranges are served from the file, never a compressed copy.
* [x] Brotli (`br`), compressed once alongside gzip in the file cache (best
  quality up to 64 KiB, lower above so a first request stays ~100 ms) and per
  request for pages; preferred over gzip on a tie. Vendored as a submodule.
* [ ] Serve `.br`/`.gz` files a build step already wrote next to the
  original, instead of compressing at runtime.
* [ ] Multi-range requests (`multipart/byteranges`), if anything needs them.

### 2. IDL evolution without breaking deployed clients
The schema hash in the class ID makes any interface change fail `narrow()`,
so a browser holding cached JS breaks on the next server deploy.
* [ ] Additive changes that keep the hash compatible: new trailing struct
  fields, new methods; `[deprecated]` for old ones.
* [ ] An `npidl` compatibility check that diffs two versions of an interface
  and reports breaking vs compatible changes.
* [ ] `map<K, V>` in the IDL.

### 3. Operational hooks
* [ ] Interceptors/middleware around every call (auth, logging, per-method
  metrics, rate limits), in C++, Swift and TS.
* [ ] Per-call metadata independent of the connection (trace ID, auth token),
  enough for W3C `traceparent` propagation.
* [ ] Server-side deadlines: pass the caller's timeout to the servant through
  `CallContext`, so it can stop work nobody is waiting for.
* [ ] Metrics export (Prometheus/OpenMetrics endpoint or OTLP), next to the
  NPRPC Monitor feed.
* [ ] Health/readiness endpoint for systemd, Docker healthchecks and load
  balancers.
* [ ] Graceful drain for HTTP/1.1, WebSocket and TCP (HTTP/3 has GOAWAY):
  stop accepting, finish in-flight calls, then exit.

### 4. CI and robustness testing
* [ ] A CI workflow that runs `run_all_tests.py` (C++ and JS at least). The only
  workflow today packages devtools.
* [ ] Fuzzing of generated deserializers, the HTTP/1.1 path, WebSocket framing
  and QUIC (see Stability & Security below).
* [ ] Soak and 1k+ connection churn tests (see Stability & Security below).

### 5. Transports and platforms
* [ ] HTTP/2, for browsers that can't use QUIC (HTTP/1.1 caps them at ~6
  connections per origin).
* [ ] Opt-in 0-RTT for idempotent requests on QUIC/HTTP/3, with replay
  protection; it is disabled outright today.
* [ ] Decide on platforms: fix Windows (shared memory, see below) and macOS,
  or document Linux-only.
* [ ] Another client language, Python first, for scripting and tests against
  live services.

### 6. Developer experience
* [ ] More examples beside live-blog: a minimal C++ server with a browser
  client, a streaming example, an fd-passing example.
* [ ] Packaging: a vcpkg or Conan port, and `nprpc` published on npm.

## STD
* [ ] Replace std::unordered_map with absl::flat_hash_map in performance-critical paths. (Partially done in http3_server_nghttp3, need to audit the rest of the codebase for hot paths like pending request tracking, connection maps, etc.)

## Packaging / DX
* [x] Provide a minimal reference example that works out of the box for a Swift backend with server-rendered pages (`examples/live-blog`).

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
* [x] **[Cancellation refactor]** Done: `Session::send_receive_coro` now cancels mid-flight on every transport (see docs/CALLS.md). Original note: `send_receive_coro` is currently only a true coroutine in `UringClientConnection`; all other transports (`SocketConnection`, WebSocket, shared memory, QUIC) inherit the blocking base-class stub that ignores the `stop_token`. Until they are ported, the only cancellation that works is pre-flight (`stop_requested()` checked at the top of every generated `*Async` body). Proper mid-flight cancellation on non-uring transports requires each one to track pending requests by ID, register a `stop_callback`, and post a resume with `status = -2` when triggered — the same pattern already used in `UringClientConnection::send_receive_coro`.

## WebSocket/WebTransport
* [ ] Support openning another WebSocket connection on demand when existing one is busy (for high-throughput clients).
* [x] **[HIGH]** Done: the TS client takes an `AbortSignal` (`send_receive`, nprpc_js/src/nprpc.ts). Original note: Add cancellation support for in-flight regular (non-streaming) RPC calls via `AbortSignal`. When triggered, send a `Cancel` message with the matching request ID over the transport before the reply arrives, then reject the pending promise on the client. Server-side cooperative cancellation is a bonus but not required — letting the handler finish and dropping the reply on the client is acceptable. Requires tracking in-flight request IDs in `PendingRequestMap` (or equivalent) and wiring `AbortSignal` through the generated proxy call signatures.

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
| Compression | ~~gzip~~ ~~brotli~~ done (HTTP/1.1 and HTTP/3); precompressed files on disk open | Medium |
| ETag/Caching | ~~ETag and If-None-Match 304s~~ done (HTTP/1.1 and HTTP/3) | Low |
| Range requests | ~~Single-range 206/416 with If-Range~~ done; multipart/byteranges open | Medium |
| Cache warming | Pre-load common files at startup | Low |
| Metrics | Expose cache stats (hit rate, memory usage) via /stats endpoint | Low |
| HTTP/2 | Add HTTP/2 support via nghttp2 (between HTTP/1.1 and HTTP/3) | High |
| sendfile() | For non-cached large files, use sendfile() syscall | Medium |
| Cache-Control | ~~Per-path policy~~ done via `cache_control(pattern, value)` | Low |

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────┐
│                              NPRPC                                  │
├─────────────┬─────────────┬──────────────────────┬──────────────────┤
│  Shared Mem │ TCP/WS/HTTP |  HTTP3/WebTransport  │   Native QUIC    │
│  (IPC)      │ (boost)     │  (ngtcp2/nghttp3)    │     (ngtcp2)     │
├─────────────┴─────────────┴──────────────────────┴──────────────────┤
│                      Transport Selection                            │
│  shm://       tcp://                                   quic://      │
│               ws://  wss://     https:// (HTTP/3)                   │
│               http:// https://  wt:// (WebTransport)                │
└─────────────────────────────────────────────────────────────────────┘
```

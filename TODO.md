# TODO.md

## STD
* [ ] Replace std::unordered_map with absl::flat_hash_map in performance-critical paths.

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

## UDP Transport (Game Networking)

### Done
* [x] IDL: `[udp]` interface attribute and `[unreliable]` method attribute
* [x] Code generation: `send_udp()` for fire-and-forget calls
* [x] UdpConnection: async send queue, connection caching
* [x] UdpListener: receive datagrams and dispatch to servants
* [x] UDP endpoint selection and URL construction
* [x] Reliable UDP with ACK/retransmit for methods without `[unreliable]`

### Known Issues
* [ ] UdpListener copies received buffer to satisfy Buffers interface (see udp_listener.cpp:160)
      - Not trivial to fix since Buffers are generated from cpp builder
      - Consider adding a view-based Buffers variant for receive-only paths

### Future Enhancements
* [ ] CRC32 or xxHash32 checksums
* [ ] ChaCha20-Poly1305 or AES-GCM encryption
* [ ] LZ4 compression for large payloads
* [ ] Delta encoding for game state updates
* [ ] Bandwidth throttling / rate limiting

## HTTP/3 Server

## QUIC Transport (MsQuic) ✅ COMPLETE

QUIC provides everything UDP fragmentation tries to do, plus more:
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
* [x] Add `ALLOW_QUIC` activation flag
* [x] Add `set_listen_quic_port()` to RpcBuilder

### Phase 2: Stream Management ✅
* [x] Use bidirectional streams for request/response RPC (default, reliable)
* [x] Use QUIC DATAGRAM extension for `[unreliable]` methods
* [x] Graceful shutdown (track connections, clear callbacks)
* [ ] Connection pooling and multiplexing (future optimization)

### Phase 3: IDL `[unreliable]` Attribute ✅
* [x] Support `[unreliable]` attribute on methods (not interface-level)
* [x] Code generator handles `[unreliable]` for UDP (fire-and-forget)
* [x] Transport behavior:
  - **TCP/WebSocket**: Ignore `[unreliable]` (always reliable)
  - **UDP**: `[unreliable]` methods use fire-and-forget, others use ACK
  - **QUIC**: Default reliable (streams), `[unreliable]` uses datagrams
* [ ] Deprecate `[udp]` interface attribute (use URL-based transport selection)

### Phase 4: Testing & Benchmarks ✅
* [x] Unit tests for QUIC transport (TestQuicBasic, TestQuicUnreliable)
* [x] Latency benchmarks vs TCP/UDP
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
* [x] Add msh3 and ls-qpack as dependencies (git submodules in third_party/)
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
┌─────────────────────────────────────────────────────────────┐
│                         NPRPC                               │
├─────────────┬─────────────┬─────────────┬───────────────────┤
│  Shared Mem │  TCP/WS/HTTP│     UDP     │   QUIC/HTTP3      │
│  (IPC)      │  (boost)    │  (boost)    │   (MsQuic/msh3)   │
├─────────────┴─────────────┴─────────────┴───────────────────┤
│                    Transport Selection                      │
│  shm://     tcp://         udp://        quic://            │
│             ws://  wss://                https:// (HTTP/3)  │
│             http:// https://                                │
├─────────────────────────────────────────────────────────────┤
│                    Method Attributes                        │
│  [unreliable] - Use best-effort delivery where supported    │
│                 TCP/WS/HTTP: ignored (always reliable)      │
│                 UDP: fire-and-forget, no ACK                │
│                 QUIC: use DATAGRAM instead of stream        │
│                                                             │
│  (no attribute) - Default reliable delivery                 │
│                 TCP/WS/HTTP: normal RPC                     │
│                 UDP: ACK/retransmit                         │
│                 QUIC: use stream                            │
└─────────────────────────────────────────────────────────────┘
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
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

## Swift Language Bindings 🚀 IN PROGRESS

### Completed ✅
* [x] Swift package structure (nprpc_swift)
* [x] Swift wrappers for Rpc, Poa, ObjectPtr
* [x] npidl `--swift` code generation
* [x] Basic type marshalling (fundamentals, enums, structs)
* [x] Servant base classes with dispatch
* [x] Client proxy generation
* [x] Basic RPC loopback tests working

### Marshalling - Type Coverage
* [x] Fundamentals (i8, u8, i16, u16, i32, u32, i64, u64, f32, f64, bool)
* [x] Enums
* [x] Nested structs
* [ ] **Strings** - Need unmarshal_string/marshal_string helpers
* [ ] **Vectors** - Need NPRPC.unmarshal_vector/marshal_vector
* [ ] **Arrays** - Need proper bounds checking and iteration
* [ ] **Optionals** - Need presence flag + conditional marshalling
* [ ] **Objects (ObjectPtr)** - Need NPRPC.unmarshal_object_proxy/marshal_object_id
* [ ] **Bounds checking** - Swift UnsafeRawPointer doesn't validate, add explicit checks

### Client-Side (Proxy)
* [ ] **Async support** - Make proxy methods actually async (currently sync)
* [ ] **Error handling** - Map C++ exceptions to Swift errors
* [ ] **Out parameters** - Handle multi-return (tuples)
* [ ] **Streams** - Map stream<T> to AsyncSequence

### Server-Side (Servant)  
* [ ] **Async dispatch bridge** - C++ dispatch() is sync, Swift methods are async
* [ ] **Error propagation** - Swift throws → C++ exception in dispatch
* [ ] **Streams** - Servant methods returning AsyncSequence

### Next Steps
1. Complete marshalling for complex types (strings, vectors, arrays, optionals, objects)
 - [x] fundamental optional
 - [x] strings
 - [x] vector of structs
 - [x] Exceptions: fix manual __ex_id assignment in test servant (use auto-generated id)
2. Add bounds checking to all unmarshal operations
3. Implement async proxy calls
4. Implement async servant dispatch bridge
5. Add stream support (AsyncSequence)
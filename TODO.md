# TODO.md

## Build System
 [ ] Generate npnameserver stubs when building nprpc target, to avoid full rebuilds when npnameserver is built separately.

## Shared Memory Transport
 [ ] Optimize shared memory server session to avoid unnecessary copies when receiving messages.
 [ ] Build on Windows is broken due to missing `sys/mman.h` and `shm_open()`. Need to implement Windows shared memory APIs using `CreateFileMapping` and `MapViewOfFile`.

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

### Benchmark Results (Nov 2025)
```
LatencyFixture/EmptyCall/0   127 us   16.6 us   43981 calls/sec   SharedMemory
LatencyFixture/EmptyCall/1   116 us   17.7 us   39629 calls/sec   TCP
LatencyFixture/EmptyCall/2   125 us   20.7 us   34186 calls/sec   WebSocket
LatencyFixture/EmptyCall/3  76.1 us   26.0 us   27468 calls/sec   UDP
LatencyFixture/EmptyCall/4   231 us   23.4 us   30361 calls/sec   QUIC
```

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

## HTTP/3 Server (Next Goal)

Serve web clients over HTTP/3 using msh3 (Microsoft's minimal HTTP/3 on MsQuic).

### Why HTTP/3?
- Modern web standard, shows technical credibility
- Reuse existing MsQuic infrastructure
- Lower latency for web clients (0-RTT, multiplexing)
- "My personal site runs on HTTP/3" 🚀

### Implementation Plan
* [ ] Add msh3 as dependency (git submodule in third_party/)
* [ ] Create `Http3Server` class
  - Integrate with existing QuicApi singleton
  - Reuse certificate handling from QUIC transport
* [ ] HTTP/3 request routing
  - Static file serving for web assets
  - RPC endpoint for NPRPC calls (bridge to existing dispatch)
* [ ] QUIC/HTTP/3 endpoint sharing
  - Same port serves both raw QUIC RPC and HTTP/3
  - Distinguish by ALPN (nprpc vs h3)

### Considerations
* msh3 is minimal (68 stars) but integrates well with MsQuic
* For production, reverse proxy (Caddy) is more battle-tested
* HTTP/3 mainly needed for web client compatibility

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                         NPRPC                               │
├─────────────┬─────────────┬─────────────┬───────────────────┤
│  Shared Mem │  TCP/WS/HTTP│     UDP     │      QUIC         │
│  (IPC)      │  (boost)    │  (boost)    │   (MsQuic)        │
├─────────────┴─────────────┴─────────────┴───────────────────┤
│                    Transport Selection                      │
│  shm://     tcp://         udp://        quic://            │
│             ws://  wss://                                   │
│             http:// https://  (browser RPC over HTTP/1.1)   │
│                               (future: HTTP/3 over QUIC)    │
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

## Swift Language Bindings (Future)

Write business logic in Swift while reusing the C++ nprpc runtime.

### Strategy: Swift C++ Interop (Swift 5.9+)
Swift has direct C++ interoperability - call C++ code without Objective-C bridging.
This allows reusing 100% of the existing runtime (transports, serialization, POA).

### Implementation Plan
* [ ] Add `swift_builder.cpp` to npidl
  - Generate Swift structs/enums matching IDL types
  - Generate Swift protocols for interfaces
  - Generate proxy classes that call into C++ runtime
* [ ] Create `nprpc_swift/` Swift package
  - Module map exposing C++ headers to Swift
  - Swift wrappers for `ObjectPtr`, `Rpc`, `Poa`
  - async/await integration (Swift concurrency)
  - Codable support for serialization
* [ ] Bridge layer (C++ interop module)
  - Expose nprpc types to Swift's C++ interop
  - Handle memory management at boundary

### Reuse Matrix
| Component | Reuse | Notes |
|-----------|-------|-------|
| C++ Runtime (libnprpc) | ✅ 100% | All transports, serialization, POA |
| IDL Parser (npidl_core) | ✅ 100% | Just add swift_builder backend |
| Wire format | ✅ 100% | Binary compatible with C++ |
| Swift types | 🆕 Generate | Structs, enums, protocols |
| Swift proxies | 🆕 Generate | Thin wrappers calling C++ |
| Swift package | 🆕 Create | ~500 lines of wrapper code |
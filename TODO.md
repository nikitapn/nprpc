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

## HTTP/3 Server ✅ Core Implementation Complete

Serve web clients over HTTP/3 using msh3 (Microsoft's minimal HTTP/3 on MsQuic).

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

### Considerations
* msh3 is minimal (68 stars) but integrates well with MsQuic
* For production, reverse proxy (Caddy) is more battle-tested
* HTTP/3 mainly needed for web client compatibility

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

## Swift Language Bindings 🚀 NEXT MAJOR PROJECT

Write business logic in Swift while reusing the C++ nprpc runtime.

### Why Swift?

| Language | C++ Interop | Async | Safety | Linux | Ecosystem |
|----------|-------------|-------|--------|-------|-----------|
| **Swift** | ✅ Native (5.9+) | ✅ async/await | ✅ Strong | ✅ Full | ⚠️ Growing |
| Rust | ⚠️ cxx crate | ✅ tokio | ✅ Strong | ✅ Full | ✅ Large |
| Go | ❌ CGO slow | ✅ goroutines | ⚠️ GC | ✅ Full | ✅ Large |
| Kotlin | ⚠️ JNI/Native | ✅ coroutines | ✅ Strong | ⚠️ Native | ⚠️ Server |

**Swift wins because:**
- Direct C++ interop = zero FFI overhead, 100% runtime reuse
- No bindings generator needed - Swift imports C++ headers directly
- Modern concurrency (async/await, actors) maps well to NPRPC
- Type-safe, memory-safe without Rust's complexity
- Future iOS/macOS client option

### Strategy: Swift C++ Interop (Swift 5.9+)

```
┌─────────────────────────────────────────────────────────────┐
│                    Your Swift Backend                       │
│   - Business logic in Swift                                │
│   - async/await for RPC calls                              │
│   - Swift types for data structures                        │
├─────────────────────────────────────────────────────────────┤
│                  Generated Swift Code                       │
│   - Swift protocols matching IDL interfaces                │
│   - Swift structs/enums for IDL types                      │
│   - Proxy classes wrapping C++ ObjectPtr<T>                │
├─────────────────────────────────────────────────────────────┤
│                 nprpc_swift Package                         │
│   - Swift wrappers for Rpc, Poa, ObjectPtr                 │
│   - C++ interop module map                                 │
│   - Error handling bridge                                   │
├─────────────────────────────────────────────────────────────┤
│              libnprpc (100% Reused C++)                     │
│   - All transports (TCP, WS, QUIC, UDP, SharedMem)         │
│   - Serialization (flat_buffer)                            │
│   - POA, servants, activation                              │
└─────────────────────────────────────────────────────────────┘
```

### Phase 0: Setup & Proof of Concept (1 week)

**Goal:** Verify Swift can call NPRPC C++ code on Linux

* [ ] Install Swift 5.9+ on dev machine
* [ ] Create minimal Swift package that imports a C++ header
* [ ] Test calling `nprpc::Rpc::builder()` from Swift
* [ ] Verify flat_buffer can be passed across boundary
* [ ] Document any interop limitations discovered

**Deliverable:** Working "Hello NPRPC" from Swift

### Phase 1: nprpc_swift Package (2 weeks)

**Goal:** Swift wrappers for core NPRPC types

```
nprpc_swift/
├── Package.swift
├── Sources/
│   ├── NPRPC/
│   │   ├── Rpc.swift           # Wrapper for nprpc::Rpc
│   │   ├── Poa.swift           # Wrapper for nprpc::Poa  
│   │   ├── ObjectPtr.swift     # Generic wrapper for ObjectPtr<T>
│   │   ├── EndPoint.swift      # Swift-friendly endpoint
│   │   ├── Errors.swift        # NPRPC exceptions → Swift errors
│   │   └── FlatBuffer.swift    # Buffer bridging utilities
│   └── CNprpc/
│       ├── include/
│       │   └── module.modulemap  # Exposes C++ headers to Swift
│       └── shim.cpp              # Thin C++ helpers if needed
└── Tests/
    └── NPRPCTests/
```

**Tasks:**
* [ ] Create Package.swift with C++ interop settings
* [ ] Write module.modulemap exposing nprpc headers
* [ ] Implement `Rpc` wrapper with builder pattern
* [ ] Implement `Poa` wrapper for servant activation  
* [ ] Implement generic `ObjectPtr<T>` wrapper
* [ ] Bridge C++ exceptions to Swift errors
* [ ] Add async/await wrappers for RPC calls
* [ ] Unit tests for each wrapper

### Phase 2: IDL Code Generator (2 weeks)

**Goal:** Generate Swift code from .npidl files

Add `swift_builder.cpp` to npidl:

```cpp
// npidl/src/swift_builder.cpp
class SwiftBuilder : public Builder {
  void emit_struct(AstStructDecl*);    // → Swift struct
  void emit_enum(AstEnumDecl*);        // → Swift enum  
  void emit_interface(AstInterfaceDecl*); // → Swift protocol + proxy
  void emit_exception(AstExceptionDecl*); // → Swift Error type
};
```

**Generated Code Example:**
```swift
// From: interface Calculator { i32 Add(a: in i32, b: in i32); }

// Protocol for servants to implement
public protocol ICalculator {
    func Add(a: Int32, b: Int32) async throws -> Int32
}

// Proxy for calling remote Calculator
public final class Calculator: NPRPCObject {
    public func Add(a: Int32, b: Int32) async throws -> Int32 {
        // Calls into C++ proxy code
    }
}

// Servant base class
open class Calculator_Servant: NPRPCServant, ICalculator {
    open func Add(a: Int32, b: Int32) async throws -> Int32 {
        fatalError("Must override")
    }
}
```

**Tasks:**
* [ ] Add `--swift` flag to npidl CLI
* [ ] Implement type mapping (IDL → Swift)
* [ ] Generate Swift structs from IDL structs
* [ ] Generate Swift enums from IDL enums
* [ ] Generate Swift protocols from IDL interfaces
* [ ] Generate proxy classes (client-side)
* [ ] Generate servant base classes (server-side)
* [ ] Generate serialization code (to/from flat_buffer)
* [ ] Handle `stream<T>` types (AsyncSequence)
* [ ] Handle `[unreliable]` attribute
* [ ] CMake integration for Swift generation

### Phase 3: Servant Dispatch Bridge (1 week)

**Goal:** Allow Swift servants to handle RPC calls

The tricky part - C++ runtime calls dispatch(), which needs to invoke Swift code.

**Approach:** Use C++ virtual function that Swift can override:

```cpp
// In C++ (nprpc_swift shim)
class SwiftServantBridge : public ObjectServant {
  void* swift_self;  // Pointer to Swift object
  DispatchFn dispatch_fn;  // Function pointer to Swift dispatch
  
  void dispatch(SessionContext& ctx, bool from_parent) override {
    dispatch_fn(swift_self, &ctx, from_parent);
  }
};
```

```swift
// In Swift
open class NPRPCServant {
    private var bridge: SwiftServantBridge
    
    // Called from C++ via function pointer
    func dispatch(ctx: UnsafeMutablePointer<SessionContext>) {
        // Route to appropriate method based on function_idx
    }
}
```

**Tasks:**
* [ ] Design Swift↔C++ dispatch mechanism
* [ ] Implement SwiftServantBridge in C++
* [ ] Implement NPRPCServant base class in Swift
* [ ] Generate dispatch routing code per interface
* [ ] Handle async Swift methods from sync C++ dispatch
* [ ] Test round-trip: Swift client → C++ → Swift servant

### Phase 4: Your Site Backend Port (2-3 weeks)

**Goal:** Rewrite site backend in Swift using NPRPC

* [ ] Identify all IDL interfaces used by site
* [ ] Generate Swift stubs for site IDL
* [ ] Port servant implementations to Swift
* [ ] Port any business logic to Swift
* [ ] Integration testing
* [ ] Performance comparison vs C++
* [ ] Deploy!

### Risks & Mitigations

| Risk | Likelihood | Mitigation |
|------|------------|------------|
| Swift C++ interop bugs | Medium | Keep C++ shim thin, test each type |
| Async bridging complexity | Medium | Start sync-only, add async later |
| Linux Swift toolchain issues | Low | Use official Swift docker images |
| Performance overhead | Low | Profile early, optimize hot paths |
| Memory management at boundary | Medium | Clear ownership rules, use RAII |

### Reuse Matrix

| Component | Reuse | Work Needed |
|-----------|-------|-------------|
| C++ Runtime (libnprpc) | ✅ 100% | None |
| IDL Parser (npidl_core) | ✅ 100% | None |
| Wire format | ✅ 100% | None |
| Transports | ✅ 100% | None |
| npidl swift backend | 🆕 New | ~1500 LOC |
| nprpc_swift package | 🆕 New | ~800 LOC |
| Dispatch bridge | 🆕 New | ~300 LOC |

### Timeline Estimate

| Phase | Duration | Cumulative |
|-------|----------|------------|
| Phase 0: POC | 1 week | 1 week |
| Phase 1: Package | 2 weeks | 3 weeks |
| Phase 2: CodeGen | 2 weeks | 5 weeks |
| Phase 3: Dispatch | 1 week | 6 weeks |
| Phase 4: Port Site | 2-3 weeks | 8-9 weeks |

**Total: ~2 months** to have your site running on Swift backend

### Alternative: Hybrid Approach

If full Swift port feels risky, consider:
1. Keep existing C++ servants running
2. Add new features in Swift
3. Gradually migrate one servant at a time
4. Both can run in same process (mixed C++/Swift)

### Reuse Matrix
| Component | Reuse | Notes |
|-----------|-------|-------|
| C++ Runtime (libnprpc) | ✅ 100% | All transports, serialization, POA |
| IDL Parser (npidl_core) | ✅ 100% | Just add swift_builder backend |
| Wire format | ✅ 100% | Binary compatible with C++ |
| Swift types | 🆕 Generate | Structs, enums, protocols |
| Swift proxies | 🆕 Generate | Thin wrappers calling C++ |
| Swift package | 🆕 Create | ~500 lines of wrapper code |
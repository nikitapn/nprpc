# TODO.md

## Build System
* Generate npnameserver stubs when building nprpc target, to avoid full rebuilds when npnameserver is built separately.

## Shared Memory Transport
* Optimize shared memory server session to avoid unnecessary copies when receiving messages.

## UDP Transport (Game Networking)

### Done
* [x] IDL: `[udp]` interface attribute and `[reliable]` method attribute
* [x] Code generation: `send_udp()` for fire-and-forget calls
* [x] UdpConnection: async send queue, connection caching
* [x] UdpListener: receive datagrams and dispatch to servants
* [x] UDP endpoint selection and URL construction
* [x] Reliable UDP with ACK/retransmit for `[reliable]` methods

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


## QUIC Transport
* [ ] Initial implementation using msquic or quiche library

## QUIC Transport (MsQuic)

QUIC provides everything UDP fragmentation tries to do, plus more:
- Reliable streams with automatic retransmission
- Unreliable datagrams (RFC 9221) for fire-and-forget
- Built-in congestion control and flow control
- TLS 1.3 encryption
- 0-RTT connection establishment
- Connection migration (survives IP changes)

### Phase 1: Core Integration
* [x] Add MsQuic as submodule/dependency in CMake
* [x] Create `QuicConnection` class (client-side)
  - Wraps `QUIC_CONNECTION` handle
  - Manages streams for RPC calls
  - Integrates with boost::asio io_context
* [x] Create `QuicListener` class (server-side)
  - Accepts incoming QUIC connections
  - Creates server sessions per connection
* [ ] Implement `QuicServerSession` for dispatching
* [ ] Wire up QUIC to RPC framework (endpoint URL: `quic://host:port`)

### Phase 2: Stream Management
* [ ] Use bidirectional streams for request/response RPC (default, reliable)
* [ ] Use QUIC DATAGRAM extension for `[unreliable]` methods
* [ ] Connection pooling and multiplexing

### Phase 3: IDL `[unreliable]` Attribute
* [ ] Support `[unreliable]` attribute on methods (not interface-level)
* [ ] Transport behavior:
  - **TCP/WebSocket**: Ignore `[unreliable]` (always reliable)
  - **UDP**: Methods are unreliable by default, `[reliable]` forces ACK
  - **QUIC**: Default reliable (streams), `[unreliable]` uses datagrams
* [ ] Deprecate `[udp]` interface attribute (use URL-based transport selection)
* [ ] Update code generator for `[unreliable]` attribute

### Phase 4: Testing & Benchmarks
* [ ] Unit tests for QUIC transport
* [ ] Latency benchmarks vs TCP/UDP
* [ ] Throughput benchmarks
* [ ] Connection establishment time (0-RTT)

## HTTP/3 Server (Future)

Options for serving web clients over HTTP/3:
1. **msh3** - Minimal HTTP/3 on MsQuic (same author)
2. **External proxy** - Caddy/nginx with HTTP/3, proxy to NPRPC
3. **Custom** - Build HTTP/3 framing on MsQuic streams

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
│  shm://     tcp://        udp://        quic://             │
│             ws://  wss://                                   │
│             http:// https://  (browser RPC over HTTP/1.1)   │
│                               (future: HTTP/3 over QUIC)    │
├─────────────────────────────────────────────────────────────┤
│                    Method Attributes                        │
│  [unreliable] - Use best-effort delivery where supported    │
│                 TCP/WS/HTTP: ignored (always reliable)      │
│                 UDP: default behavior                       │
│                 QUIC: use DATAGRAM instead of stream        │
│                                                             │
│  [reliable]   - Force reliable delivery (UDP only, legacy)  │
└─────────────────────────────────────────────────────────────┘
```
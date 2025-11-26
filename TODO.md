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

### Known Issues
* [ ] UdpListener copies received buffer to satisfy Buffers interface (see udp_listener.cpp:160)
      - Not trivial to fix since Buffers are generated from cpp builder
      - Consider adding a view-based Buffers variant for receive-only paths

### Future Enhancements
* [ ] Sequence numbers for ordered delivery
* [ ] Reliable UDP with ACK/retransmit for `[reliable]` methods
* [ ] ChaCha20-Poly1305 or AES-GCM encryption
* [ ] LZ4 compression for large payloads
* [ ] Delta encoding for game state updates
* [ ] Bandwidth throttling / rate limiting
* [ ] MTU discovery to avoid fragmentation
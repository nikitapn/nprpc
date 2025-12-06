Client                              Server
  |                                     |
  |-------- Initial (CRYPTO) ---------->|
  |<------- Initial (CRYPTO) -----------|
  |-------- Handshake (CRYPTO) -------->|
  |<------- Handshake (CRYPTO) ---------|
  |                                     |
  |-------- GET /index.html ----------->|
  |<------- 200 OK + body --------------|
  |                                     |
  |-------- CONNECTION_CLOSE ---------->|  ← Client is done
  |                                     |
  |          (draining period)          |  ← ERR_DRAINING here
  |                                     |
  |        (connection cleaned up)      |




┌─────────────────────────────────────────────────────────────────┐
│                        Node.js Process                          │
│  ┌─────────────────┐     ┌─────────────────────────────────┐    │
│  │   SvelteKit     │────►│  nprpc_js (TypeScript)          │    │
│  │   +page.server  │     │  - Serialization                │    │
│  └─────────────────┘     │  - Object proxies               │    │
│                          └──────────────┬──────────────────┘    │
│                                         │ JS ↔ C++ boundary     │
│                          ┌──────────────▼──────────────────┐    │
│                          │  nprpc_node (Native Addon)      │    │
│                          │  - SharedMemoryChannel          │    │
│                          │  - LockFreeRingBuffer           │    │
│                          └──────────────┬──────────────────┘    │
└─────────────────────────────────────────┼───────────────────────┘
                                          │ /dev/shm/nprpc_*
┌─────────────────────────────────────────┼───────────────────────┐
│                    C++ nprpc Server     │                       │
│                          ┌──────────────▼──────────────────┐    │
│                          │  SharedMemoryServerSession      │    │
│                          │  - Same ring buffers            │    │
│                          └─────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────┘
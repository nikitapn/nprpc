# WebTransport Stream Architecture

## Overview

NPRPC uses WebTransport (over HTTP/3) as a high-performance alternative to WebSocket for browser-to-server communication. The transport layer sits on top of QUIC, multiplexing two kinds of application-level streams — **Control** and **Native** — over client-initiated QUIC bidirectional streams.

## QUIC Connection Topology

Chromium opens **two separate QUIC connections** to the same origin:

| Connection | Purpose | Streams |
|---|---|---|
| Connection 1 | Regular HTTP/3 | Page HTML, JS, CSS, images, `/host.json`, `/rpc` (initial fetch) |
| Connection 2 | WebTransport-dedicated | `CONNECT /wt` session + all WT child streams |

This is normal Chromium behavior — the browser isolates the WebTransport session onto a dedicated QUIC connection. The server sees two `Http3Connection` objects.

## Binding Protocol

Every WT bidi stream opened by the client carries a **binding prefix** as its very first bytes. This tells the server what kind of traffic will flow on the stream.

```
Control stream prefix:  [0x00]           (1 byte)
Native stream prefix:   [0x01][stream_id] (1 + 8 = 9 bytes, stream_id is little-endian u64)
```

Constants:
```
k_webtransport_bind_control       = 0x00
k_webtransport_bind_native_stream = 0x01
```

### Why a Probe Buffer?

QUIC delivers data in chunks, and the first chunk may not contain the full binding prefix (e.g., the 9-byte native header could arrive as two separate reads). The `webtransport_probe_buffer` in `Http3Stream` accumulates incoming bytes until there is enough data to determine the binding:

- 1 byte is enough to detect a Control stream
- 9 bytes are needed for a Native stream (1 byte kind + 8 byte stream_id)

Once the binding is detected, leftover bytes after the prefix are forwarded to the session and the probe buffer is cleared — it is never used again for that stream.

## Control Stream

The **Control stream** is the first bidi stream the client opens after the WebTransport session is ready. It carries **all general-purpose RPC traffic**:

- `FunctionCall` requests and replies
- `StreamInit` requests (to set up new server/client/bidi streams)
- `StreamWindowUpdate` messages
- `AddReference` / `ReleaseObject` lifetime messages

The control stream is **long-lived** — it stays open for the entire duration of the WebTransport session. If the control stream closes, the client treats it as a full connection close.

### Client-Side (TypeScript)

```typescript
// During init_webtransport():
const bidi = await this.wt_create_bidi_stream();      // Open QUIC bidi stream
this.wt_writer = bidi.writable.getWriter();            // Store writer for sending
await this.write_webtransport_bind_prefix(             // Send 0x00 prefix
    this.wt_writer, webtransport_bind_control);
void this.read_webtransport_stream(bidi.readable);     // Start read loop (no stream_id)
```

When `read_webtransport_stream` receives `done: true` with no `stream_id` parameter, it calls `on_close()` — the control stream dying kills the connection.

### Server-Side

The server's `WebTransportControlSession` tracks the control stream via `control_stream_id_`. All RPC replies and fire-and-forget messages are sent through `send_main_stream_message()`, which routes to `queue_buffer(control_stream_id_, ...)`.

## Native Streams

**Native streams** are per-NPRPC-stream dedicated QUIC bidi streams. They are created when the application opens a streaming RPC:

- `open_server_stream()` → the server writes chunks, the client reads
- `open_client_stream()` → the client writes chunks, the server reads
- `open_bidi_stream()` → both sides read and write

### When Are Native Streams Created?

1. The client calls an IDL-generated stream method (e.g., `GetVideoDashSegmentRange`)
2. The generated stub sends a **`StreamInit`** message over the **control stream**
3. The server processes `StreamInit`, registers the stream, and sends a reply over the control stream
4. After receiving the `StreamInit` reply, the client calls `ensure_native_stream(stream_id)`:
   - Opens a new QUIC bidi stream via `wt_create_bidi_stream()`
   - Sends the native binding prefix: `[0x01][stream_id as 8 bytes LE]`
   - Starts a dedicated read loop for this stream
5. The server's `recv_wt_data` detects the native binding and calls `session->bind_native_stream(stream_id, transport_stream_id)`

### Pending Writes

There's a race: the server-side stream writer may produce data **before** the client has opened and bound the native stream. The `WebTransportControlSession` handles this with `pending_native_writes_`:

```
send_stream_message(buffer)
  → extract RPC stream_id from buffer header
  → if native_stream_bindings_ has the stream_id → queue_buffer() immediately
  → else → pending_native_writes_[stream_id].push(buffer)  // Queue for later

bind_native_stream(stream_id, transport_stream_id)
  → native_stream_bindings_[stream_id] = transport_stream_id
  → drain all pending_native_writes_ for this stream_id → queue_buffer() each
```

## Write Path (Server → Client)

### Non-WT Data (Regular Reply)

For RPC replies and fire-and-forget messages on the control stream, the `send_main_stream_message()` method is used. This calls `queue_raw_stream_write()` on the control stream's transport stream ID.

### WT Native Stream Data

For streaming data on native streams, the flow is:

```
StreamManager dispatches buffer
  → WebTransportControlSession::send_stream_message()
  → looks up native_stream_bindings_ → transport_stream_id
  → Http3Connection::queue_raw_stream_write(transport_stream_id, buffer)
    → on strand:
      ├── push buffer onto stream->raw_write_queue
      ├── if first write: nghttp3_conn_open_wt_data_stream() — registers with nghttp3
      └── else: nghttp3_conn_resume_stream() — wakes up nghttp3 scheduling
    → on_write() → ngtcp2 write loop → wt_read_data_cb pulls data from queue
```

### `wt_read_data_cb` — Data Provider

nghttp3 calls this callback when it needs data to write for a WT data stream:

- Returns `NGHTTP3_ERR_WOULDBLOCK` when the queue is empty (more data may come later)
- Returns a single `nghttp3_vec` pointing into the front of `raw_write_queue` using a cumulative `wt_write_offset`
- Buffers stay alive until ACK'd — `http_acked_stream_data_cb` pops them

### Stream Completion & EOF

Native streams must eventually close so that QUIC can reclaim the bidi stream slot (`extend_max_streams_bidi`). This is done via the `wt_write_fin_pending` flag:

1. `queue_raw_stream_write` inspects each buffer's message header
2. If the message is `StreamCompletion` or `StreamError`, sets `wt_write_fin_pending = true`
3. `wt_read_data_cb`, after all data is drained and `wt_write_fin_pending` is set, returns `NGHTTP3_DATA_FLAG_EOF`
4. nghttp3 sends FIN on the QUIC stream → `on_stream_close` → `extend_max_streams_bidi(1)`

**Important:** This check is scoped to `WebTransportChildBinding::Native` only — the control stream also carries `StreamCompletion` messages (for multiplexed RPC streams) but must never be FIN'd.

## Stream Limits

The server advertises `initial_max_streams_bidi = 100` to the peer. Each native stream consumes one slot. The CONNECT session and control stream each consume one slot. After all slots are used, the client cannot open new bidi streams until existing ones close via FIN.

Without the EOF signaling described above, native streams would never close on the server side, exhausting the limit after ~98 native streams and starving any further streams.

## Message Framing

Both control and native streams use the same length-prefix framing. Each NPRPC message is prefixed with a 4-byte little-endian total length (including the length field itself):

```
[total_len: u32 LE][Header][Payload...]
```

The `append_webtransport_bytes` method (client) and `process_bytes` method (server) reassemble these framed messages from arbitrary QUIC chunk boundaries.

## Summary Diagram

```
Browser (Chromium)
├── QUIC Connection 1  (regular HTTP/3)
│   ├── GET /           → index.html
│   ├── GET /assets/... → JS, CSS
│   ├── GET /host.json  → object endpoints
│   └── POST /rpc       → initial RPC (before WT ready)
│
└── QUIC Connection 2  (WebTransport)
    ├── Stream 0:  CONNECT /wt             (WT session, long-lived)
    ├── Stream 4:  [0x00] Control stream   (all RPC messages, long-lived)
    ├── Stream 8:  [0x01][id₁] Native      (e.g., video segment stream)
    ├── Stream 12: [0x01][id₂] Native      (e.g., audio metadata stream)
    ├── Stream 16: [0x01][id₃] Native      (e.g., another video segment)
    └── ...
```

## Window Credit Flow Control

### Design

NPRPC uses a **chunk-level credit system** for stream backpressure (demand is
expressed in chunks — units of work — like Reactive Streams' `REQUEST_N`, not
in bytes like TCP/QUIC). The producer can only send as many chunks as it has
credits; the consumer refills the pool with **watermark-batched**
`StreamWindowUpdate` grants instead of one grant per consumed chunk.

#### Constants

| Constant | Value | Description |
|---|---|---|
| `kDefaultReaderWindow` (C++) / `default_reader_window` (TS) / `defaultReaderWindow` (Swift) | 32 | Window a consumer advertises in `StreamInit.initial_credits` |
| Grant threshold | window / 2 | Consumed chunks accumulated before one batched `StreamWindowUpdate` |
| `kInitialWindowSize` | 8 | Producer fallback window when `initial_credits == 0` (legacy peer, or upload direction with no advertisement) |

#### Window advertisement

`StreamInit` carries `initial_credits: u32` (offset 28, fits the tail padding
— the message stayed 32 bytes). The stub that opens a server/bidi stream sets
it to the reader's window; the producer uses it as its starting credit pool
(`StreamManager::register_stream` / `register_external_writer` /
TS `create_writer`). `0` means "not advertised" and falls back to
`kInitialWindowSize = 8`.

The upload direction (client streams, and the client→server half of bidi) has
no advertisement channel — the server-side reader assumes the legacy window of
8 and batches at threshold 4.

#### Protocol Flow (window = 32, threshold = 16)

```
Server (producer)                          Client (consumer)
    │<── StreamInit (initial_credits=32) ──────│
    │── StreamDataChunk (seq=0..15) ─────────> │  credits: 32→16
    │   ...                                    │  (client consumes 16 chunks)
    │<────────────── StreamWindowUpdate(16) ───│  credits: +16
    │── StreamDataChunk (seq=16..31) ─────────>│
    │   ...                                    │  (client consumes 16 more)
    │<────────────── StreamWindowUpdate(16) ───│
```

Two flow-control messages per 32 chunks instead of 32 — a 16× reduction in
upstream control traffic.

#### Correctness properties

- **No deadlock:** the grant threshold never exceeds the producer's window.
  If the producer stalls at zero credits, everything it sent is buffered at
  the consumer, so the consumed counter must reach the threshold and fire a
  refill.
- **No stall (steady state):** the producer goes idle only if it burns
  `window` credits before a grant makes the round trip, i.e. the window must
  satisfy `window ≥ chunk_rate × RTT + threshold`. With window 32 /
  threshold 16 there is headroom for ~16 chunks in flight per RTT.
- **Version coupling:** both ends must speak the same protocol. A consumer
  that advertises 32 and batches at 16 will deadlock against an old producer
  that ignores `initial_credits` (window 8 < threshold 16).

#### Implementation map

| Piece | Location |
|---|---|
| Producer credit pool & refill | `StreamManager::register_stream`, `on_window_update` (C++); `StreamWriter` credits (TS) |
| Consumer batching | `StreamReader::read_next` (C++), `StreamReader` async iterator (TS), `createStreamManagerReader.windowUpdateFn` (Swift) |
| Advertisement | npidl-generated stubs set `StreamInit.initial_credits`; servant dispatch passes it to the writer registration |

### Rejected alternatives

- **Byte-based window (TCP/QUIC style):** fairer for wildly variable chunk
  sizes, but message-oriented protocols inherit HTTP/2's pathology — a chunk
  can only be sent when the window covers its entire size, requiring
  window ≥ max chunk size at all times to avoid deadlock. Chunk credits +
  per-stream window tuning (`initial_credits`) covers the practical cases; a
  byte *cap* could be added alongside chunk credits later if needed.
- **Implicit QUIC flow control** (`MAX_STREAM_DATA` backpressure on native
  streams): free on QUIC, but streams also run multiplexed over the control
  stream and over non-QUIC transports, so the application-level mechanism
  must exist anyway. QUIC's stream flow control still acts as a transparent
  memory-safety net underneath native streams.
- **Adaptive window sizing:** overkill until there's evidence of widely
  varying bandwidth-delay products; `initial_credits` provides the per-stream
  tuning knob.

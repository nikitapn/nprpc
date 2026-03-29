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

### Current Design

NPRPC uses a **chunk-level credit system** for stream backpressure. The producer (typically the server) can only send a limited number of data chunks before it must wait for the consumer (typically the client) to grant more credits.

#### Constants

| Constant | Value | Description |
|---|---|---|
| `kInitialWindowSize` | 8 | Pre-granted credits before the first `StreamWindowUpdate` |
| Client `window_size_` (C++) | 16 | Initial buffer capacity in the C++ reader |

#### Protocol Flow

```
Server (producer)                          Client (consumer)
    │                                           │
    │── StreamDataChunk (seq=0) ──────────────>│  credits: 8→7
    │── StreamDataChunk (seq=1) ──────────────>│  credits: 7→6
    │── StreamDataChunk (seq=2) ──────────────>│  credits: 6→5
    │   ...                                     │  (client reads chunk 0)
    │<─────────────── StreamWindowUpdate(1) ───│  credits: 5→6
    │   ...                                     │  (client reads chunk 1)
    │<─────────────── StreamWindowUpdate(1) ───│  credits: 6→7
    │   ...                                     │
    │── StreamDataChunk (seq=7) ──────────────>│  credits: 1→0
    │   *** BLOCKED — waiting for credits ***   │
    │<─────────────── StreamWindowUpdate(1) ───│  (client reads chunk 2)
    │── StreamDataChunk (seq=8) ──────────────>│  credits: 1→0
    │   ...                                     │
```

#### How It Works

**Server side (`StreamManager`):**
- Each stream starts with `kInitialWindowSize = 8` credits
- When `write_chunk_or_queue()` is called:
  - If `credits > 0` → send immediately, decrement credits
  - If `credits == 0` → enqueue the chunk in `pending_writes`
- When `on_window_update(stream_id, credits)` arrives:
  - Add credits to the pool
  - Drain queued `pending_writes` (one credit per chunk)
  - If a coroutine is blocked, cancel its sleep timer to unblock it

**Client side (JS `StreamReader`):**
- The async iterator yields chunks one at a time
- After each `yield`, sends `send_window_update(stream_id, 1)` — granting exactly **one credit per consumed chunk**
- The window update is sent over the **control stream** (not the native stream), since it's an RPC-level message

**Client side (C++ `StreamReader`):**
- `read_next()` pops a chunk from the queue, increments `window_size_`, sends `send_window_update(1)`
- Same one-credit-per-read pattern

### Inefficiency Analysis

The current design sends **one `StreamWindowUpdate` message per consumed chunk**. For a video stream producing 30+ chunks/second, that's 30+ small control messages/second flowing upstream just for flow control. Each message is:
- 16 bytes header + 12 bytes payload = 28 bytes
- Framed as a full NPRPC message on the control stream
- Serialized, routed through nghttp3/ngtcp2, ACK'd by QUIC

This creates unnecessary overhead, especially on the control stream which also handles RPC traffic.

### Better Strategies

#### 1. Batched Credits (Simple Improvement)

Instead of sending 1 credit per read, accumulate and send in batches:

```
// Send credits when consumed count reaches a threshold (e.g., half the window)
if (++consumed_since_last_update >= window_size / 2) {
    send_window_update(stream_id, consumed_since_last_update);
    consumed_since_last_update = 0;
}
```

**Trade-off:** Slightly higher latency before the producer learns it can send more. With a window of 8 and threshold of 4, the producer may stall briefly if it sends all 8 before the first batch arrives.

**Mitigation:** Increase the initial window to 16–32 so the producer has headroom while the first batch is in flight.

#### 2. High-Watermark / Low-Watermark

Grant credits proactively at a low-watermark rather than reactively per-read:

```
// Grant credits when the consumer's buffer drops below low_watermark
on_chunk_consumed():
    buffered_count--
    if buffered_count <= low_watermark:
        grant = high_watermark - buffered_count
        send_window_update(stream_id, grant)
```

Example: `high_watermark = 16`, `low_watermark = 4`. When buffered chunks drop to 4, grant 12 credits at once. This keeps the pipeline full and sends far fewer messages.

#### 3. Byte-Based Window (Like TCP/QUIC)

Instead of counting chunks, count bytes. The consumer advertises how many bytes it can buffer. This is fairer for variable-size chunks (a 32KB video segment vs a 24-byte metadata update shouldn't cost the same credit).

```
StreamWindowUpdate {
    stream_id: u64;
    byte_credits: u64;   // Number of additional BYTES the sender may transmit
}
```

The producer tracks `remaining_bytes` and blocks when it reaches zero. This is how TCP receive window and QUIC flow control work.

**Trade-off:** Need to choose a reasonable initial byte window (e.g., 256KB–1MB for video, 64KB for metadata). Per-stream tuning becomes important.

#### 4. Implicit Credits via QUIC Flow Control (Zero Application Messages)

Since native streams map 1:1 to QUIC bidi streams, QUIC already has stream-level flow control (`MAX_STREAM_DATA`). The application could rely entirely on QUIC backpressure:

- The client's QUIC stack stops sending `MAX_STREAM_DATA` when the application is slow to read
- ngtcp2 returns `NGTCP2_ERR_STREAM_DATA_BLOCKED` when the peer's window is exhausted
- The server detects blocking and pauses the producer

This eliminates `StreamWindowUpdate` messages entirely. The downside is coupling to QUIC semantics (doesn't work over WebSocket), and less application-level visibility into backpressure.

**Hybrid approach:** Use QUIC flow control for native streams (where it's free), keep application-level credits only for the control stream mux.

#### 5. Adaptive Window Sizing

Dynamically adjust the window based on observed RTT and throughput, similar to TCP congestion control. Start small, grow aggressively if the consumer keeps up, shrink if it falls behind.

This is complex to implement correctly and is usually overkill unless you have wildly varying bandwidth conditions.

### Recommendation

For the immediate term, **Strategy 1 (batched credits)** with a larger initial window is the simplest improvement — it reduces message count by 4–8x with a one-line change on each client. For longer-term, **Strategy 4 (implicit QUIC flow control)** for native streams is the most elegant since it eliminates redundant flow control entirely on the transport that already provides it.

# HTTP/3 Backend Options

NPRPC supports HTTP/3 with two backend implementations. You can select the backend at compile time using the `NPRPC_HTTP3_BACKEND` CMake option.

## Available Backends

### 1. msh3 (Default)

The msh3 backend uses Microsoft's MsQuic library for QUIC transport with the msh3 HTTP/3 layer.

**Pros:**
- Single library for QUIC + HTTP/3
- Windows support
- Active Microsoft support

**Cons:**
- Buffer ownership semantics can be tricky (must keep data alive until SEND_COMPLETE)
- Some stability issues reported (see [curl blog post](https://daniel.haxx.se/blog/2025/07/29/carving-out-msh3/))

**CMake Configuration:**
```bash
cmake -DNPRPC_ENABLE_HTTP3=ON -DNPRPC_HTTP3_BACKEND=msh3 ...
```

### 2. nghttp3 (Experimental)

The nghttp3 backend uses ngtcp2 for QUIC transport and nghttp3 for HTTP/3 framing. This is the same stack used by curl.

**Pros:**
- Production-quality, used by curl
- Clear separation of concerns (QUIC vs HTTP/3)
- Explicit buffer management

**Cons:**
- Currently a stub - not fully implemented
- Requires more code to integrate
- Need to handle timer management and flow control explicitly

**CMake Configuration:**
```bash
cmake -DNPRPC_ENABLE_HTTP3=ON -DNPRPC_HTTP3_BACKEND=nghttp3 ...
```

## Current Status

| Backend | Status | Static Files | RPC | Browser Support |
|---------|--------|--------------|-----|-----------------|
| msh3    | Working | ✅ | ✅ | ✅ (with Alt-Svc) |
| nghttp3 | Stub   | ❌ | ❌ | ❌ |

## Building with HTTP/3

### msh3 Backend (Recommended for now)

```bash
# Full build with msh3 HTTP/3
cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DNPRPC_ENABLE_HTTP3=ON \
    -DNPRPC_HTTP3_BACKEND=msh3

cmake --build build -j$(nproc)
```

### nghttp3 Backend (Work in Progress)

```bash
# Initialize nghttp3 submodules
git submodule update --init --recursive third_party/ngtcp2 third_party/nghttp3

# Build with nghttp3 backend
cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DNPRPC_ENABLE_HTTP3=ON \
    -DNPRPC_HTTP3_BACKEND=nghttp3

cmake --build build -j$(nproc)
```

## Implementation Notes

### nghttp3 Backend TODO

The nghttp3 backend (`src/http3_server_nghttp3.cpp`) is a skeleton that needs:

1. **TLS Setup**: Configure OpenSSL with QUIC support (need quictls or OpenSSL 3.2+)
2. **ngtcp2 Callbacks**: Implement all required callbacks:
   - `recv_client_initial`
   - `recv_crypto_data`
   - `handshake_completed`
   - `recv_stream_data`
   - `acked_stream_data_offset`
   - `stream_open`
   - `stream_close`
   - etc.
3. **HTTP/3 Integration**: Wire nghttp3 to ngtcp2:
   - Bind control and QPACK streams
   - Handle `recv_header`, `recv_data`, `end_stream` callbacks
   - Submit responses with `nghttp3_conn_submit_response`
4. **Timer Management**: Handle connection timeouts and retransmissions
5. **Flow Control**: Properly extend stream/connection offsets

Reference implementation: `third_party/ngtcp2/examples/server.cc`

## Architecture

```
                    ┌─────────────────────────────────────┐
                    │           NPRPC HTTP/3              │
                    │      (init_http3_server)            │
                    └─────────────┬───────────────────────┘
                                  │
        ┌─────────────────────────┼─────────────────────────┐
        │                         │                         │
        ▼                         ▼                         ▼
┌───────────────┐       ┌────────────────┐       ┌──────────────────┐
│ msh3 Backend  │       │ nghttp3 Backend│       │ (future backends)│
│ (http3_server │       │ (http3_server_ │       │                  │
│  .cpp)        │       │  nghttp3.cpp)  │       │                  │
└───────┬───────┘       └───────┬────────┘       └──────────────────┘
        │                       │
        ▼                       ▼
┌───────────────┐       ┌───────────────┐
│    MsQuic     │       │    ngtcp2     │
│     msh3      │       │   nghttp3     │
└───────────────┘       └───────────────┘
```

Both backends expose the same interface:
- `init_http3_server(boost::asio::io_context&)` - Start HTTP/3 server
- `stop_http3_server()` - Stop HTTP/3 server

The backend is selected at compile time via `NPRPC_HTTP3_BACKEND_MSH3` or `NPRPC_HTTP3_BACKEND_NGHTTP3` defines.

# NPRPC Server-Side Rendering (SSR) Architecture

This document describes the architecture for integrating server-side rendering (SSR) frameworks like SvelteKit with NPRPC's C++ HTTP servers via high-performance shared memory IPC.

## Overview

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           Client Browser                                │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    │ HTTP/1.1, HTTP/2, HTTP/3
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                      NPRPC C++ HTTP Server                              │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │  • Static file serving (with zero-copy file cache)              │    │
│  │  • RPC endpoint handling (/rpc)                                 │    │
│  │  • SSR request routing → NodeWorkerManager                      │    │
│  └─────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    │ Shared Memory IPC
                                    │ (LockFreeRingBuffer)
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                      Node.js SSR Worker                                 │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │  SvelteKit Server (or other SSR framework)                      │    │
│  │  • server.respond(request) → HTML response                      │    │
│  │  • Prerendered page serving                                     │    │
│  │  • API route handling                                           │    │
│  └─────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────┘
```

## Components

### 1. NodeWorkerManager (C++)

Located in `include/nprpc/impl/node_worker_manager.hpp` and `src/node_worker_manager.cpp`.

Responsibilities:
- **Process Lifecycle**: Spawns and manages the Node.js worker process
- **IPC Channel**: Creates shared memory channel for bidirectional communication
- **Request Forwarding**: Serializes HTTP requests to JSON, sends via shared memory
- **Response Handling**: Receives JSON responses, deserializes back to HTTP responses

```cpp
#include <nprpc/impl/node_worker_manager.hpp>

// Create manager
nprpc::impl::NodeWorkerManager ssr_manager(io_context);

// Start Node.js worker with path to SvelteKit build
ssr_manager.start("/path/to/sveltekit/build");

// Forward an HTTP request (blocking)
NodeWorkerManager::SsrRequest req;
req.method = "GET";
req.url = "https://example.com/page";
req.headers = {{"accept", "text/html"}};
req.client_address = "192.168.1.100";

auto response = ssr_manager.forward_request(req, /*timeout_ms=*/30000);
if (response) {
    // Use response->status_code, response->headers, response->body
}

// Or async version
ssr_manager.forward_request_async(req, [](auto response) {
    if (response) {
        // Handle response
    }
}, /*timeout_ms=*/30000);

// Cleanup
ssr_manager.stop();
```

### 2. Shared Memory Channel

The communication uses `SharedMemoryChannel` which wraps two `LockFreeRingBuffer` instances:
- **Server-to-Client (s2c)**: C++ server → Node.js worker
- **Client-to-Server (c2s)**: Node.js worker → C++ server

Benefits:
- **Zero-copy**: Data is written directly to shared memory
- **Lock-free**: SPSC (Single Producer, Single Consumer) design avoids locks
- **High throughput**: ~1+ GB/s for large messages

Configuration:
- Ring buffer size: 16 MB per direction (32 MB total per connection)
- Max message size: 32 MB (same as TCP/WebSocket limits)

### 3. nprpc_node Native Addon

Located in `nprpc_node/`.

A Node.js native addon (N-API) that provides JavaScript access to the shared memory channel:

```javascript
const { ShmChannel } = require('nprpc_node');

// Connect as client to C++ server's channel
const channel = new ShmChannel(process.env.NPRPC_CHANNEL_ID, {
    isServer: false,
    create: false
});

// Send data
channel.send(new Uint8Array([...]));

// Receive data (non-blocking)
const data = channel.tryReceive();

// Check for data
if (channel.hasData()) { ... }

// Cleanup
channel.close();
```

### 4. SvelteKit Adapter

Located in `nprpc_adapter_sveltekit/`.

A custom SvelteKit adapter that:
1. Builds the SvelteKit app for Node.js
2. Generates a handler that communicates via shared memory
3. Integrates with the NPRPC server

```javascript
// svelte.config.js
import adapter from '@nprpc/adapter-sveltekit';

export default {
    kit: {
        adapter: adapter({
            // Optional: build-time channel ID (usually set via env)
            // channelId: 'my-channel-id'
        })
    }
};
```

## Protocol

### Request Format (JSON)

```json
{
    "type": "request",
    "id": 12345,
    "method": "GET",
    "url": "https://example.com/page?query=value",
    "headers": {
        "accept": "text/html",
        "cookie": "session=abc123"
    },
    "body": "base64-encoded-body-for-POST",
    "clientAddress": "192.168.1.100"
}
```

### Response Format (JSON)

```json
{
    "type": "response",
    "id": 12345,
    "status": 200,
    "headers": {
        "content-type": "text/html; charset=utf-8",
        "cache-control": "public, max-age=3600"
    },
    "body": "base64-encoded-html-content"
}
```

### Message Flow

1. **C++ Server** receives HTTP request
2. **NodeWorkerManager** serializes request to JSON using glaze
3. JSON is written to shared memory ring buffer (s2c)
4. **Node.js handler** polls for data, parses JSON
5. **SvelteKit** processes request via `server.respond()`
6. **Node.js handler** serializes response to JSON
7. JSON is written to shared memory ring buffer (c2s)
8. **NodeWorkerManager** receives response, deserializes
9. **C++ Server** sends HTTP response to client

## Build Configuration

Enable SSR support in CMake:

```bash
cmake -S . -B build \
    -DNPRPC_ENABLE_SSR=ON \
    -DNPRPC_BUILD_TOOLS=ON
```

This:
- Adds `node_worker_manager.cpp` to the build
- Links Boost.Process and Boost.Filesystem
- Includes glaze for JSON serialization
- Defines `NPRPC_SSR_ENABLED` preprocessor macro

## Integration Example

### HTTP Server Integration

```cpp
#include <nprpc/impl/node_worker_manager.hpp>
#include <nprpc/impl/http_server.hpp>

class MyHttpServer {
    boost::asio::io_context& ioc_;
    nprpc::impl::NodeWorkerManager ssr_manager_;
    
public:
    MyHttpServer(boost::asio::io_context& ioc) 
        : ioc_(ioc)
        , ssr_manager_(ioc)
    {}
    
    bool start() {
        // Start SSR worker
        if (!ssr_manager_.start("/var/www/sveltekit/build")) {
            return false;
        }
        
        // Start HTTP server...
        return true;
    }
    
    void handle_request(const HttpRequest& http_req, HttpResponseCallback cb) {
        // Check if this is an SSR request (HTML page)
        if (should_ssr(http_req)) {
            // Forward to Node.js
            NodeWorkerManager::SsrRequest ssr_req;
            ssr_req.method = http_req.method();
            ssr_req.url = http_req.url();
            // ... populate headers, body
            
            ssr_manager_.forward_request_async(ssr_req, 
                [cb](auto response) {
                    if (response) {
                        cb(make_http_response(*response));
                    } else {
                        cb(make_error_response(502, "SSR timeout"));
                    }
                });
        } else if (is_rpc_request(http_req)) {
            // Handle RPC
            handle_rpc(http_req, cb);
        } else {
            // Serve static file
            serve_static(http_req, cb);
        }
    }
    
    bool should_ssr(const HttpRequest& req) {
        // SSR for HTML requests to non-static paths
        auto accept = req.header("accept");
        return accept.find("text/html") != std::string::npos
            && !is_static_file(req.path());
    }
};
```

### SvelteKit App Setup

1. Install the adapter:
```bash
cd my-sveltekit-app
npm install /path/to/nprpc/nprpc_adapter_sveltekit
npm install /path/to/nprpc/nprpc_node
```

2. Configure `svelte.config.js`:
```javascript
import adapter from '@nprpc/adapter-sveltekit';

export default {
    kit: {
        adapter: adapter()
    }
};
```

3. Build:
```bash
npm run build
```

4. The build output in `build/` contains:
   - `index.js` - Entry point (reads `NPRPC_CHANNEL_ID` env var)
   - `handler.js` - Request handler with shared memory IPC
   - `server/` - SvelteKit server code
   - `client/` - Static client assets
   - `prerendered/` - Pre-rendered static pages

## Performance Characteristics

| Metric | Value |
|--------|-------|
| Latency overhead | ~50-100µs per request |
| Throughput | Limited by Node.js SSR, not IPC |
| Memory per channel | 32 MB (configurable) |
| Max concurrent requests | Limited by pending request map |

## Comparison with Alternatives

| Approach | Pros | Cons |
|----------|------|------|
| **Shared Memory (this)** | Lowest latency, zero-copy | Platform-specific, requires native addon |
| **Unix Domain Socket** | Portable, standard APIs | Copy overhead, syscall per message |
| **HTTP localhost** | Simple, no native code | TCP overhead, multiple syscalls |
| **Embed V8** | Single process | Complex, memory overhead |

## Debugging

### Environment Variables

- `NPRPC_CHANNEL_ID`: Shared memory channel identifier (set by C++ server)

### Logging

Enable verbose logging in NPRPC:
```cpp
nprpc::Config cfg;
cfg.debug_level = nprpc::LogLevel::warn;
```

This logs:
- Node.js process startup/shutdown
- Request/response forwarding
- Stdout/stderr from Node.js worker

### Common Issues

1. **Channel not found**: Ensure C++ server starts before Node.js worker connects
2. **Timeout**: Check Node.js worker is running (`ssr_manager.is_ready()`)
3. **JSON parse errors**: Verify request/response format matches protocol
4. **Permission denied**: Check `/dev/shm` permissions for shared memory

## Future Improvements

- [ ] Connection pooling for multiple Node.js workers
- [ ] Health checks and automatic worker restart
- [ ] Metrics/tracing integration
- [ ] Windows named pipe support
- [ ] WebAssembly SSR option (no Node.js)

## Related Documentation

- [Shared Memory Architecture](./SHARED_MEMORY_ARCHITECTURE.md)
- [HTTP/3 Server](./HTTP3_BACKENDS.md)
- [UDP Transport](./UDP_TRANSPORT.md)

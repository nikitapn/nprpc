# @nprpc/adapter-sveltekit

A SvelteKit adapter that serves SSR (Server-Side Rendering) via shared memory IPC with an NPRPC C++ server.

## Overview

This adapter enables high-performance SSR by:
1. Building your SvelteKit app for production
2. Creating a Node.js handler that communicates via shared memory
3. The C++ NPRPC server handles HTTP requests and forwards them to the Node.js handler
4. Responses are returned through the same shared memory channel

This eliminates HTTP overhead for SSR, achieving near-native performance.

## Installation

```bash
npm install @nprpc/adapter-sveltekit
```

## Usage

### svelte.config.js

```javascript
import adapter from '@nprpc/adapter-sveltekit';

export default {
    kit: {
        adapter: adapter({
            // Optional: output directory (default: 'build')
            out: 'build',
            
            // Optional: fixed channel ID (default: from environment)
            channelId: 'my-app-channel',
            
            // Optional: precompress static assets (default: true)
            precompress: true
        })
    }
};
```

### Building

```bash
npm run build
```

### Running

The built app expects a C++ NPRPC server to be running with the shared memory channel:

```bash
# Set the channel ID (if not using fixed channelId in config)
export NPRPC_CHANNEL_ID=my-app-channel

# Start the Node.js handler
node build/index.js
```

## Architecture

```
┌─────────────────┐     HTTP      ┌───────────────────┐
│     Browser     │ ◄──────────►  │  NPRPC C++ Server │
└─────────────────┘               └────────┬──────────┘
                                           │
                                    Shared Memory
                                           │
                                  ┌────────▼────────┐
                                  │  SvelteKit SSR  │
                                  │   (Node.js)     │
                                  └─────────────────┘
```

### Request Flow

1. Browser sends HTTP request to C++ server
2. C++ server serializes request to JSON
3. Request sent via shared memory to Node.js
4. SvelteKit processes request (SSR)
5. Response sent back via shared memory
6. C++ server returns HTTP response to browser

### [Message Format](../idl/nprpc_node.npidl)

## C++ Server Integration

The C++ server needs to:

1. Create a shared memory channel with `SharedMemoryChannel`
2. For SSR requests, serialize the HTTP request to JSON
3. Send via shared memory and wait for response
4. Deserialize response and return to client

Example (simplified):
```cpp
#include <nprpc/impl/shared_memory_channel.hpp>
#include <nlohmann/json.hpp>

class SsrHandler {
    nprpc::impl::SharedMemoryChannel channel_;
    
public:
    SsrHandler(boost::asio::io_context& ioc, const std::string& channel_id)
        : channel_(ioc, channel_id, true, true)  // server, create
    {}
    
    std::string handle_ssr_request(
        const std::string& method,
        const std::string& url,
        const std::map<std::string, std::string>& headers,
        const std::string& body
    ) {
        // Serialize request
        nlohmann::json req = {
            {"type", "request"},
            {"id", next_request_id_++},
            {"method", method},
            {"url", url},
            {"headers", headers},
            {"body", base64_encode(body)}
        };
        
        // Send and wait for response
        channel_.send(req.dump());
        // ... wait for response ...
        
        return base64_decode(response["body"]);
    }
};
```

## Performance

Shared memory IPC provides:
- **Zero network overhead** - No TCP/HTTP stack for SSR
- **~1+ GB/s throughput** - Limited only by memory bandwidth
- **Sub-millisecond latency** - Direct memory access

## License

MIT

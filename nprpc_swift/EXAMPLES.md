# NPRPC Swift HTTP3 Server Example

This example demonstrates how to create an HTTP3/QUIC server in Swift using NPRPC's C++ RPC framework through Swift's C++ interoperability.

## Features

- **HTTP/3 (QUIC) Protocol**: Modern, fast, multiplexed transport
- **TLS 1.3 Encryption**: Secure connections by default
- **Fluent Builder API**: Matches C++ `RpcBuilder` pattern
- **Zero-Copy Interop**: Direct Swift ↔ C++ calls without marshalling overhead
- **Static File Serving**: Built-in HTTP server for web content

## Quick Start

### Build

```bash
cd nprpc_swift
LD_LIBRARY_PATH=../.build_ubuntu_swift swift build --product http3-server
```

### Run

```bash
# Option 1: Direct run
LD_LIBRARY_PATH=../.build_ubuntu_swift \
  ./.build/x86_64-unknown-linux-gnu/debug/http3-server

# Option 2: Using Docker
./run_http3_server.sh
```

### Test

```bash
# With curl (HTTP/3 support requires curl 7.66+)
curl --http3 -k https://localhost:3000

# Or visit in browser (Chrome/Firefox with HTTP/3 enabled):
# https://localhost:3000
```

## Code Structure

```swift
import NPRPC

let rpc = try RpcBuilder()
    .setLogLevel(.trace)
    .setHostname("localhost")
    .withHttp(3000)
        .ssl(
            certFile: "/path/to/cert.crt",
            keyFile: "/path/to/key.key"
        )
        .enableHttp3()
        .rootDir("/path/to/web/root")
    .build()

try rpc.run()  // Blocks until shutdown
```

## API Reference

### RpcBuilder Methods

| Method | Description |
|--------|-------------|
| `.setLogLevel(_:)` | Set logging level (trace/debug/info/warning/error) |
| `.setHostname(_:)` | Set hostname for advertised URLs |
| `.withHttp(_:)` | Enable HTTP/WebSocket on port, returns HTTP-specific builder |
| `.withTcp(_:)` | Enable TCP transport on port |
| `.withUdp(_:)` | Enable UDP transport on port |
| `.withQuic(_:)` | Enable QUIC transport on port |
| `.build()` | Build and initialize Rpc instance |

### RpcBuilderHttp Methods (HTTP-specific)

| Method | Description |
|--------|-------------|
| `.ssl(certFile:keyFile:)` | Enable TLS with certificate files |
| `.enableHttp3()` | Enable HTTP/3 (QUIC) protocol |
| `.enableSsr(handlerDir:)` | Enable server-side rendering |
| `.rootDir(_:)` | Set HTTP document root directory |

## Comparison with C++

### C++ Version
```cpp
auto rpc = nprpc::RpcBuilder()
    .set_log_level(nprpc::LogLevel::trace)
    .set_hostname("localhost")
    .with_http(3000)
    .ssl("cert.crt", "key.key")
    .enable_http3()
    .root_dir("/path")
    .build(ioc);
```

### Swift Version
```swift
let rpc = try RpcBuilder()
    .setLogLevel(.trace)
    .setHostname("localhost")
    .withHttp(3000)
        .ssl(certFile: "cert.crt", keyFile: "key.key")
        .enableHttp3()
        .rootDir("/path")
    .build()
```

The APIs are nearly identical - Swift just uses camelCase and `try` for error handling.

## Requirements

- **Swift**: 6.2.3+ with C++ interop support
- **NPRPC**: Built with Clang 17 + libstdc++
- **Boost**: 1.89.0+ built with same toolchain
- **OpenSSL**: 3.0+ (for TLS/certificates)
- **Certificates**: Valid TLS certificates (see `/certs/create.sh`)

## Certificate Generation

```bash
cd ../../certs
./create.sh
```

This creates self-signed certificates in `certs/out/` for local testing.

## Architecture

```
┌─────────────┐
│ Swift Code  │  (High-level API, type safety)
└──────┬──────┘
       │ Swift C++ Interop (zero-copy)
┌──────▼──────┐
│ C++ Bridge  │  (nprpc_bridge.cpp)
└──────┬──────┘
       │
┌──────▼──────┐
│ NPRPC Core  │  (libnprpc.so - RPC framework)
└──────┬──────┘
       │
┌──────▼──────┐
│ Boost.Asio  │  (Async I/O, HTTP3/QUIC)
└─────────────┘
```

## Next Steps

1. **Implement RPC Services**: Define `.npidl` interfaces and generate Swift stubs
2. **Add SSR Support**: Enable server-side rendering with `.enableSsr()`
3. **Connect to Nameserver**: Register services for discovery
4. **Add Authentication**: Implement auth middleware
5. **Production Deployment**: Configure logging, monitoring, health checks

## Troubleshooting

### Library Not Found
```
error: libnprpc.so.1: cannot open shared object file
```
**Solution**: Set `LD_LIBRARY_PATH` to point to NPRPC build directory:
```bash
export LD_LIBRARY_PATH=/path/to/.build_ubuntu_swift:$LD_LIBRARY_PATH
```

### Certificate Errors
```
Error starting server: SSL certificate not found
```
**Solution**: Generate certificates or update paths in code:
```bash
cd ../../certs && ./create.sh
```

### Port Already in Use
```
Error: Address already in use (port 3000)
```
**Solution**: Change port or kill existing process:
```bash
# Change port
.withHttp(3001)  // Use different port

# Or kill existing
lsof -ti:3000 | xargs kill -9
```

## License

MIT License - see LICENSE file in repository root.

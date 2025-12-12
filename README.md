# NPRPC - Multi-Transport RPC Framework

NPRPC is a high-performance, multi-transport RPC (Remote Procedure Call) framework designed for distributed systems. It features an efficient binary protocol with flat buffers serialization and supports multiple transport layers for maximum flexibility.

## 🚀 Key Features

- **Multiple Transport Options**: Choose the best transport for your use case
  - **WebSocket** - Persistent bidirectional communication
  - **Secure WebSocket (WSS)** - Encrypted persistent connections
  - **HTTP** - Stateless request/response for web applications
  - **HTTP/3** - Modern web transport over QUIC (via nghttp3/ngtcp2)
  - **TCP** - Direct socket communication
  - **Shared Memory** - Zero-copy IPC with 8x memory efficiency
  - **UDP** - Fire-and-forget for game networking
  - **QUIC** - Next-gen encrypted transport (via MsQuic)
- **Server-Side Rendering**: Built-in SvelteKit SSR support via shared memory IPC
- **Efficient Binary Protocol**: FlatBuffers-based serialization for minimal overhead
- **Type-Safe IDL**: Interface Definition Language with code generation for C++ and TypeScript
- **Cross-Language Support**: Seamless C++ ↔ TypeScript/JavaScript communication
- **Modern Async API**: Built on Boost.Asio (C++) and async/await (TypeScript)
- **Built-in Object Management**: POA (Portable Object Adapter) for lifecycle management
- **Nameserver**: Service discovery and object binding
- **SSL/TLS Support**: Secure communications out of the box
- **Exception Handling**: Type-safe exception propagation across language boundaries

## 📦 Transport Comparison

| Transport | Use Case | Pros | Cons |
|-----------|----------|------|------|
| **WebSocket** | Real-time apps, persistent connections | Bidirectional, low latency, stateful | Requires persistent connection |
| **HTTP** | Web apps, stateless APIs, SSR | Browser compatible, SSR-capable | Higher overhead per request |
| **TCP** | High-performance IPC | Low overhead, reliable | No browser support |
| **Shared Memory** | Same-machine IPC | Zero-copy, 8x memory efficient | Local only |
| **UDP** | Game networking, low-latency | 76µs latency, fire-and-forget | Connectionless, size limits |
| **QUIC** | Next-gen transport | Multiplexed, encrypted, 0-RTT, ~43k calls/sec | Requires MsQuic |
| **HTTP/3** | Modern web, SSR | HTTP/3 over QUIC, SSR-capable | Requires nghttp3/ngtcp2 |

## � Server-Side Rendering (SSR) Support

NPRPC includes built-in support for serving SvelteKit applications with server-side rendering over HTTP/3. This enables high-performance web applications with:

- **Full SSR** - Initial page loads are rendered server-side for SEO and fast first paint
- **Client-side Navigation** - SvelteKit's `__data.json` endpoints handled seamlessly
- **Form Actions** - POST requests with `?/action` patterns fully supported
- **Shared Memory IPC** - Zero-copy communication between C++ and Node.js
- **HTTP/3** - Modern QUIC-based transport for optimal performance

### Architecture

```
Browser ──HTTP/3──► C++ Server ──Shared Memory──► Node.js (SvelteKit)
                        │
                        └── Static files (zero-copy cache)
```

### Quick Start

1. **Build your SvelteKit app** with `@nprpc/adapter-sveltekit`:

```javascript
// svelte.config.js
import adapter from '@nprpc/adapter-sveltekit';

export default {
  kit: {
    adapter: adapter()
  }
};
```

2. **Configure the C++ server**:

```cpp
#include <nprpc/nprpc.hpp>

int main() {
    boost::asio::io_context ioc;
    
    auto rpc = nprpc::RpcBuilder()
        .set_hostname("myserver")
        .with_http()
            .port(3000)
            .root_dir("/path/to/build/client")  // Static assets
            .ssl("cert.pem", "key.pem")
            .enable_http3()
            .enable_ssr("/path/to/build")       // SSR handler directory
        .build(ioc);

    ioc.run();
    return 0;
}
```

3. **Build with SSR support**:

```bash
cmake -DNPRPC_ENABLE_SSR=ON -DNPRPC_BUILD_HTTP3=ON ..
cmake --build .
```

### How It Works

- **HTML Page Requests** - Forwarded to Node.js for SSR, returns fully rendered HTML
- **Data Requests** (`__data.json`) - SvelteKit client navigation data
- **Form Actions** (`POST ?/action`) - Server-side form handling
- **Static Assets** - Served directly from C++ with zero-copy file cache
- **RPC Calls** - Still handled by NPRPC's binary protocol at `/rpc`

See [SSR_ARCHITECTURE.md](docs/SSR_ARCHITECTURE.md) for detailed documentation.

## �🎮 UDP Transport

UDP support is designed for game networking and other latency-sensitive applications.
The payload size is limited to fit within a single UDP datagram (typically ~1200 bytes).

```typescript
// game.npidl
module game;

message vector3 {
  x: f32;
  y: f32;
  z: f32;
}

[udp]
interface GameUpdates {
  [unreliable]  // Fire-and-forget, no ACK
  async PlayerMoved(x: f32, y: f32, z: f32);

  [unreliable]
  async BulletFired(weapon_id: u32, weapon_id, dir: vector3);

  // ACK-based reliable delivery
  void PlayerDied(killer_id: u32, victim_id: u32);
}
```

**Features:**
- **Fire-and-forget**: Send and don't wait - ideal for position updates
- **Reliable mode**: ACK-based delivery with retransmission
- **Connection caching**: Reuse connections for ~40k calls/sec throughput
- **Low latency**: ~76µs vs ~119µs for TCP on same machine

See [UDP_TRANSPORT.md](docs/UDP_TRANSPORT.md) for details.

## 🛠️ Installation

### Prerequisites

- C++23 compiler
- CMake 3.15+
- OpenSSL
- Boost (optional, for npidl tool)
- Node.js 16+ (optional, for TypeScript/JavaScript bindings)

### Quick Install

```bash
# Ubuntu/Debian
sudo apt install build-essential cmake libssl-dev

# macOS
brew install cmake openssl

# Clone and build
git clone https://github.com/yourusername/nprpc.git
cd nprpc
mkdir build && cd build
cmake ..
cmake --build .
sudo cmake --install .
```

See [BUILD.md](BUILD.md) for detailed build instructions and options.

### Using in Your Project

**With CMake:**

```cmake
find_package(nprpc REQUIRED)
add_executable(myapp main.cpp)
target_link_libraries(myapp PRIVATE nprpc::nprpc)
```

## 📝 Quick Start

### 1. Define Your Interface (IDL)

Create a `.npidl` file describing your service:

```cpp
// calculator.npidl
module example;

exception CalculationError {
  message: string;
  code: i32;
}

interface Calculator {
  f64 Add(a: in f64, b: in f64);
  f64 Subtract(a: in f64, b: in f64);
  f64 Multiply(a: in f64, b: in f64);
  f64 Divide(a: in f64, b: in f64) raises(CalculationError);
}
```

### 2. Generate Code

```bash
npidl calculator.npidl --cpp --ts
```

This generates:
- `calculator.hpp` / `calculator.cpp` - C++ stubs
- `calculator.ts` - TypeScript client

### 3. Implement Server (C++)

```cpp
#include <nprpc/nprpc.hpp>
#include "calculator.hpp"

class CalculatorImpl : public example::ICalculator_Servant {
public:
  double Add(double a, double b) override {
    return a + b;
  }

  double Subtract(double a, double b) override {
    return a - b;
  }

  double Multiply(double a, double b) override {
    return a * b;
  }

  double Divide(double a, double b) override {
    if (b == 0.0) {
      throw example::CalculationError{"Division by zero", 1};
    }
    return a / b;
  }
};

int main() {
  boost::asio::io_context ioc;

  // Create RPC with multiple transports
  auto rpc = nprpc::RpcBuilder()
    .set_log_level(nprpc::LogLevel::Error)
    .set_listen_tcp_port(15000)
    .set_listen_http_port(8080)
    .set_http_root_dir("./public")
    .set_hostname("localhost")
    .build(ioc);

  // Create POA for object management
  auto poa = nprpc::PoaBuilder(rpc)
    .with_max_objects(10)
    .with_lifespan(nprpc::PoaPolicy::Lifespan::Persistent)
    .build();

  // Activate object with multiple transports
  auto calc = std::make_shared<CalculatorImpl>();
  auto oid = poa->activate_object(
    calc.get(),
    nprpc::ObjectActivationFlags::ALLOW_TCP |
    nprpc::ObjectActivationFlags::ALLOW_WEBSOCKET |
    nprpc::ObjectActivationFlags::ALLOW_HTTP
  );

  // Optional: Register with nameserver
  auto nameserver = nprpc::get_nameserver("localhost:15001");
  nameserver->Bind(oid, "calculator");

  ioc.run();
  return 0;
}
```

### 4. Use Client (TypeScript/JavaScript)

#### WebSocket Client (Persistent Connection)

```typescript
import * as NPRPC from 'nprpc';
import * as example from './gen/calculator';

// Initialize RPC with WebSocket
const rpc = await NPRPC.init();

// Get object from nameserver
const nameserver = NPRPC.get_nameserver('localhost:15001');
const objRef = NPRPC.make_ref<NPRPC.ObjectProxy>();
await nameserver.Resolve('calculator', objRef);

// Narrow to specific type
const calculator = NPRPC.narrow(objRef.value, example.Calculator);

// Call methods - uses WebSocket
const sum = await calculator.Add(10, 20);        // 30
const diff = await calculator.Subtract(20, 10);  // 10
const product = await calculator.Multiply(5, 6); // 30

try {
  await calculator.Divide(10, 0);
} catch (err) {
  if (err instanceof example.CalculationError) {
    console.error(`Error ${err.code}: ${err.message}`);
  }
}
```

#### HTTP Client (Stateless)

```typescript
import * as NPRPC from 'nprpc';
import * as example from './gen/calculator';

// Get calculator proxy (from host.json or nameserver)
const calculator = NPRPC.narrow(host_info.objects.calculator, example.Calculator);

// Call via HTTP - returns values directly, no out parameters
const sum = await calculator.http.Add(10, 20);        // 30
const diff = await calculator.http.Subtract(20, 10);  // 10
const product = await calculator.http.Multiply(5, 6); // 30

try {
  await calculator.http.Divide(10, 0);
} catch (err) {
  if (err instanceof example.CalculationError) {
    console.error(`Error ${err.code}: ${err.message}`);
  }
}
```

## 🔧 Advanced Features

### Nameserver for Service Discovery

**Server Side:**
```cpp
// Bind objects to names
auto nameserver = nprpc::get_nameserver("localhost:15001");
nameserver->Bind(calculator_oid, "calculator");
nameserver->Bind(auth_oid, "authorizator");
nameserver->Bind(chat_oid, "chat");
```

**Client Side:**
```typescript
const nameserver = NPRPC.get_nameserver('localhost:15001');

// Resolve by name
const calcRef = NPRPC.make_ref<NPRPC.ObjectProxy>();
const found = await nameserver.Resolve('calculator', calcRef);

if (found) {
  const calculator = NPRPC.narrow(calcRef.value, nscalc.Calculator);
  // Use calculator...
}
```

### POA Object ID Policy (deterministic IDs)

Use `with_object_id_policy(PoaPolicy::ObjectIdPolicy::UserSupplied)` when you need stable object IDs (for example, when bundling pre-known IDs into a web client). In this mode you must provide the ID explicitly:

```cpp
auto static_poa = rpc->create_poa()
  .with_lifespan(nprpc::PoaPolicy::Lifespan::Persistent)
  .with_object_id_policy(nprpc::PoaPolicy::ObjectIdPolicy::UserSupplied)
  .with_max_objects(10)
  .build();

constexpr nprpc::oid_t calculator_id = 0;

static_poa->activate_object_with_id(
  calculator_id,
  calculator_servant.get(),
  nprpc::ObjectActivationFlags::ALLOW_TCP | nprpc::ObjectActivationFlags::ALLOW_HTTP
);
```

NOTE: Ensure the IDs are unique and in the valid range (0 to max_objects - 1).

POAs default to `SystemGenerated`, so `activate_object_with_id` is only available when the policy is set to `UserSupplied`, and plain `activate_object` is disabled to prevent accidental mismatches.

### SSL/TLS Support

```cpp
auto rpc = nprpc::RpcBuilder()
  .set_listen_http_port(443)
  .enable_ssl_server(
    "cert.pem",      // public key
    "key.pem",       // private key
    "dhparam.pem"    // DH parameters
  )
  .build(ioc);

// Activate with SSL WebSocket only
poa->activate_object(
  obj.get(),
  nprpc::ObjectActivationFlags::ALLOW_SSL_WEBSOCKET
);
```

```typescript
// Client automatically uses wss:// when connecting to HTTPS
const rpc = await NPRPC.init(); // Detects protocol from page URL
```

### Shared Memory Transport (IPC)

For same-machine communication with zero-copy efficiency:

```cpp
// Server
auto rpc = nprpc::RpcBuilder()
  .set_shared_memory_size(64 * 1024 * 1024) // 64MB
  .build(ioc);

poa->activate_object(
  obj.get(),
  nprpc::ObjectActivationFlags::ALLOW_SHARED_MEMORY
);
```

```cpp
// Client
auto rpc = nprpc::init_client_only();
auto obj = nprpc::resolve_shared_memory<MyInterface>("my_object");
obj->MyMethod(data);
```

### Complex Data Types

NPRPC supports rich data types in IDL:

```typescript
message UserProfile {
  username: string;
  email: string;
  avatar?: avatar_url; // optional field
  roles: vector<string>; // dynamic array
}

// Alias for convenience
alias UserList = vector<UserProfile>;

struct NestedData {
  id: i32;
  users: UserList;
  tenItemsOfSomething: f64[10]; // fixed-size array
}


interface DataService {
  UserProfile GetUser(username: in string);
  UserList SearchUsers(query: in string);
  void UpdateProfile(profile: in UserProfile);
  NestedData GetComplexData() raises(DataError);
  // Async method
  async GetIdsAsync(u32 count, ids: out vector<i32>);
}
```

### Object References

Pass objects as parameters:
NOTE: For the time being, use `object` to define parameter type for custom interfaces. Support for typed interface parameters is planned for future releases.

```typescript
interface IDataProcessor {
  void ProcessData(data: in vector<u8>);
}

interface ObjectManager {
  object CreateProcessor(type: string);
  void RegisterProcessor(processor: in object);
}
```

```typescript
// Implement a processor
class MyProcessor extends example.IDataProcessor_Servant {
  ProcessData(data: Uint8Array): void {
    // Process data...
  }
}
// Object manager that creates processors
class MyObjectManager extends example.I_Servant {
  nprpc::Poa* poa_; // Assume initialized
  std::vector<std::shared_ptr<MyProcessor>> processors_;
  std::vector<nprpc::ObjectPtr<IDataProcessor>> remote_processors_;
public:
  nprpc::ObjectId CreateProcessor(type: string) override {
    auto processor = std::make_shared<MyProcessor>();
    processors_.push_back(processor);
    // This will make the object accessible remotely for everyone
    return poa->activate_object(processor.get(), nprpc::ObjectActivationFlags::ALLOW_ALL);
    // If you want to restrict access for anyone else but this connection, add session context parameter
    return poa->activate_object(processor.get(), nprpc::ObjectActivationFlags::ALLOW_ALL, &nprpc::get_context());
  }

  void RegisterProcessor(processor: in object) override {
    const auto proc = nprpc::narrow<IDataProcessor>(processor);
    if (!proc) {
      throw nprpc::Exception("Invalid processor object");
    }
    remote_processors_.push_back(proc);
  }
}
```
You can pass your local javascript servant objects as parameters too and your server can call back into them! Assuming you use a bidirectional transport like WebSocket.
```typescript
class MyDataProcessor extends example.IDataProcessor_Servant {
  // This is now callable from the server side
  ProcessData(data: Uint8Array): void {
    // Process data...
  }
}
const manager = NPRPC.narrow(host_info.objects.object_manager, example.ObjectManager);
const processor = new MyDataProcessor();
await manager.RegisterProcessor(processor); // Pass servant as parameter
```

## 🎯 Transport Selection Guide

### Use WebSocket when:
- You need bidirectional communication (server push)
- Low latency is critical
- You're building real-time applications (chat, notifications, live updates)
- Connection overhead is amortized over many calls

### Use HTTP when:
- You're building web applications
- You need stateless operations
- You want simple request/response patterns
- You need maximum browser compatibility
- You're behind restrictive firewalls

### Use TCP when:
- You need maximum performance
- You're not in a browser environment
- You have direct network access

### Use Shared Memory when:
- Both client and server are on the same machine
- You need zero-copy performance
- You're transferring large amounts of data
- Memory efficiency is critical (8x reduction vs TCP)

## 📊 Performance

NPRPC includes comprehensive benchmarks comparing against gRPC and Cap'n Proto.

*Benchmarks built with -O3 optimization. Results from November 2025.*

### Benchmark Results

#### Empty Call Latency (no payload)
| Framework | Time | Calls/sec |
|-----------|------|-----------|
| **NPRPC UDP** | **76 μs** | **38.8k/s** |
| **NPRPC TCP** | 119 μs | 55.2k/s |
| **NPRPC WebSocket** | 127 μs | 47.9k/s |
| **NPRPC SharedMemory** | 131 μs | 60.9k/s |
| gRPC | 332 μs | 16.7k/s |
| Cap'n Proto | 10,185 μs | 16.0k/s |

#### Call With Return Value
| Framework | Time | Calls/sec |
|-----------|------|-----------|
| **NPRPC TCP** | 117 μs | 55.6k/s |
| **NPRPC WebSocket** | 126 μs | 48.7k/s |
| **NPRPC SharedMemory** | 136 μs | 54.0k/s |
| gRPC | 329 μs | 16.9k/s |
| Cap'n Proto | 10,197 μs | 15.1k/s |

#### Large Data Transfer (1 MB payload)
| Framework | Time | Throughput |
|-----------|------|------------|
| **NPRPC SharedMemory** | **0.85 ms** | **4.50 GiB/s** |
| gRPC | 2.64 ms | 2.42 GiB/s |
| **NPRPC TCP** | 9.39 ms | 843 MiB/s |
| Cap'n Proto | 11.9 ms | 1.56 GiB/s |
| **NPRPC WebSocket** | 82.8 ms | 2.69 GiB/s |

#### Large Data Transfer (10 MB payload)
| Framework | Time | Throughput |
|-----------|------|------------|
| gRPC | 13.8 ms | 2.27 GiB/s |
| **NPRPC SharedMemory** | 17.6 ms | 2.59 GiB/s |
| **NPRPC TCP** | 18.4 ms | 1.91 GiB/s |
| Cap'n Proto | 29.5 ms | 1.04 GiB/s |
| **NPRPC WebSocket** | 43.3 ms | 2.23 GiB/s |

**Key Takeaways:**
- **UDP is fastest** for fire-and-forget calls at 76μs latency
- NPRPC is **3-4x faster** than gRPC for RPC calls
- NPRPC SharedMemory achieves **4.50 GiB/s** throughput for 1MB payloads
- Cap'n Proto has high latency due to 10ms polling interval

### Running Benchmarks

```bash
# Build with benchmarks
cmake -DNPRPC_BUILD_TESTS=ON ..
cmake --build .

# Run all benchmarks
./benchmark/nprpc_benchmarks

# Run specific benchmark suite
./benchmark/nprpc_benchmarks --benchmark_filter=LargeData
./benchmark/nprpc_benchmarks --benchmark_filter=EmptyCall
```

See [`benchmark/README.md`](benchmark/README.md) for detailed benchmark documentation.

## 🔍 IDL Language Reference

### Basic Types
- `boolean`, `i8`, `i16`, `i32`, `i64`
- `u8`, `u16`, `u32`, `u64`
- `f32`, `f64`
- `string`
- `object` - generic object reference

### Collections
- `vector<T>` - dynamic arrays
- `T[N]` - fixed-size arrays

### Modifiers
- `?` - nullable values
- `in` - input parameters
- `out` - output parameters
- `raises(ExceptionType)` - exception specification

It pretty much copies CORBA IDL with some simplifications and additions.

### Example:
```cpp
message Point { x: f64; y: f64; }

exception OutOfBounds { message: string; }

interface Geometry {
  f64 Distance(a: in Point, b: in Point);
  void Transform(pt: in Point, result: out Point);
  Point? FindCenter(points: in vector<Point>);
  vector<Point> GeneratePoints(count: in u32) raises(OutOfBounds);
}
```

## 🤝 Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

## 📄 License

See [LICENSE](../LICENSE) file in the topmost directory.

## 🙏 Acknowledgments

NPRPC is built on top of excellent open-source libraries:
- [Boost.Asio](https://www.boost.org/doc/libs/release/libs/asio/) - Async I/O, TCP/UDP
- [Boost.Beast](https://www.boost.org/doc/libs/release/libs/beast/) - HTTP/WebSocket
- [nghttp3/ngtcp2](https://github.com/ngtcp2/ngtcp2.git) - HTTP/3 and QUIC
- [MsQuic](https://github.com/microsoft/msquic.git) - QUIC transport

## 📚 More Examples

Check out the complete examples:
- [Nameserver](npnameserver/npnameserver.cpp) - Service discovery server
- [Calculator Service](../nscalc/server/src/main.cpp) - Full-featured web service
- [TypeScript Client](../nscalc/client/src/rpc/rpc.ts) - Browser client
- [Test Suite](test/js/test/nprpc-integration.test.ts) - Comprehensive tests

---

**NPRPC** - 2-4x faster than gRPC, with zero-copy shared memory reaching 4.28 GiB/s! 🚀

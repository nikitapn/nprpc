# NPRPC - Multi-Transport RPC Framework

NPRPC is a high-performance, multi-transport RPC (Remote Procedure Call) framework designed for distributed systems. It features an efficient binary protocol with flat buffers serialization and supports multiple transport layers for maximum flexibility.

## 🚀 Key Features

- **Multiple Transport Options**: Choose the best transport for your use case
  - **WebSocket** - Persistent bidirectional communication
  - **Secure WebSocket (WSS)** - Encrypted persistent connections
  - **HTTP** - Stateless request/response for web applications
  - **TCP** - Direct socket communication
  - **Shared Memory** - Zero-copy IPC with 8x memory efficiency
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
| **HTTP** | Web apps, stateless APIs | Browser compatible, stateless, simple | Higher overhead per request |
| **TCP** | High-performance IPC | Low overhead, reliable | No browser support |
| **Shared Memory** | Same-machine IPC | Zero-copy, 8x memory efficient | Local only |

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
namespace nscalc;

exception CalculationError {
  string message;
  i32 code;
}

interface Calculator {
  f64 Add(f64 a, f64 b);
  f64 Subtract(f64 a, f64 b);
  f64 Multiply(f64 a, f64 b);
  f64 Divide(f64 a, f64 b) throws CalculationError;
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

class CalculatorImpl : public nscalc::ICalculator_Servant {
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
      throw nscalc::CalculationError{"Division by zero", 1};
    }
    return a / b;
  }
};

int main() {
  boost::asio::io_context ioc;

  // Create RPC with multiple transports
  auto rpc = nprpc::RpcBuilder()
    .set_debug_level(nprpc::DebugLevel::DebugLevel_Critical)
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
import * as nscalc from './gen/calculator';

// Initialize RPC with WebSocket
const rpc = await NPRPC.init();

// Get object from nameserver
const nameserver = NPRPC.get_nameserver('localhost:15001');
const objRef = NPRPC.make_ref<NPRPC.ObjectProxy>();
await nameserver.Resolve('calculator', objRef);

// Narrow to specific type
const calculator = NPRPC.narrow(objRef.value, nscalc.Calculator);

// Call methods - uses WebSocket
const sum = await calculator.Add(10, 20);        // 30
const diff = await calculator.Subtract(20, 10);  // 10
const product = await calculator.Multiply(5, 6); // 30

try {
  await calculator.Divide(10, 0);
} catch (err) {
  if (err instanceof nscalc.CalculationError) {
    console.error(`Error ${err.code}: ${err.message}`);
  }
}
```

#### HTTP Client (Stateless)

```typescript
import * as NPRPC from 'nprpc';
import * as nscalc from './gen/calculator';

// Get calculator proxy (from host.json or nameserver)
const calculator = NPRPC.narrow(host_info.objects.calculator, nscalc.Calculator);

// Call via HTTP - returns values directly, no out parameters
const sum = await calculator.http.Add(10, 20);        // 30
const diff = await calculator.http.Subtract(20, 10);  // 10
const product = await calculator.http.Multiply(5, 6); // 30

try {
  await calculator.http.Divide(10, 0);
} catch (err) {
  if (err instanceof nscalc.CalculationError) {
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

```cpp
struct UserProfile {
  string username;
  string email;
  optional<string> avatar_url;
  vector<string> roles;
  map<string, string> metadata;
}

struct NestedData {
  i32 id;
  vector<UserProfile> users;
  map<string, vector<f64>> timeseries;
}

interface DataService {
  UserProfile GetUser(string username);
  vector<UserProfile> SearchUsers(string query);
  void UpdateProfile(UserProfile profile);
  NestedData GetComplexData() throws DataError;
}
```

### Object References

Pass objects as parameters:

```cpp
interface ObjectManager {
  void ProcessData(IDataProcessor processor, vector<u8> data);
  IDataProcessor CreateProcessor(string type);
}
```

```typescript
// Implement servant locally
class MyProcessor extends nscalc.IDataProcessor_Servant {
  ProcessData(data: Uint8Array): void {
    // Process data...
  }
}

const processor = new MyProcessor();
const oid = poa.activate_object(processor);

// Pass to remote object
await objectManager.ProcessData(oid, data);
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

*Benchmarks built with -O3 optimization.*

### Benchmark Results

#### Empty Call Latency (no payload)
| Framework | Time | Calls/sec |
|-----------|------|-----------|
| **NPRPC TCP** | 116 μs | 56.874k/s |
| **NPRPC WebSocket** | 126 μs | 48.913k/s |
| **NPRPC SharedMemory** | 128 μs | 60.479k/s |
| gRPC | 325 μs | 16.884k/s |
| Cap'n Proto | 10,216 μs | 14.213k/s |

#### Large Data Transfer (1 MB payload)
| Framework | Time | Throughput |
|-----------|------|------------|
| **NPRPC SharedMemory** | **0.922 ms** | **4.28 GiB/s** |
| gRPC | 2.64 ms | 2.34 GiB/s |
| **NPRPC TCP** | 7.73 ms | 850.59 MiB/s |
| Cap'n Proto | 11.7 ms | 1.78 GiB/s |
| **NPRPC WebSocket** | 80.3 ms | 2.67 GiB/s |

#### Large Data Transfer (10 MB payload)
| Framework | Time | Throughput |
|-----------|------|------------|
| gRPC | 13.0 ms | 2.45 GiB/s |
| **NPRPC SharedMemory** | 17.2 ms | 2.77 GiB/s |
| Cap'n Proto | 23.6 ms | 1.23 GiB/s |
| **NPRPC TCP** | 27.1 ms | 1.82 GiB/s |
| **NPRPC WebSocket** | 42.8 ms | 2.34 GiB/s |

**Key Takeaways:**
- NPRPC SharedMemory with zero-copy is **8x faster** than TCP for large payloads
- NPRPC is **2-4x faster** than gRPC for empty calls
- NPRPC SharedMemory achieves **4.28 GiB/s** throughput for 1MB payloads

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
- `bool`, `i8`, `i16`, `i32`, `i64`
- `u8`, `u16`, `u32`, `u64`
- `f32`, `f64`
- `string`

### Collections
- `vector<T>` - dynamic arrays
- `map<K, V>` - key-value maps

### Modifiers
- `optional<T>` - nullable values
- `out` - output parameters
- `throws ExceptionType` - exception specification

### Example:
```cpp
struct Point { f64 x; f64 y; }

exception OutOfBounds { string message; }

interface Geometry {
  f64 Distance(Point a, Point b);
  void Transform(Point in, out Point result);
  optional<Point> FindCenter(vector<Point> points);
  vector<Point> GeneratePoints(u32 count) throws OutOfBounds;
}
```

## 🤝 Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

## 📄 License

See [LICENSE](../LICENSE) file in the topmost directory.

## 🙏 Acknowledgments

NPRPC is built on top of excellent open-source libraries:
- [Boost.Asio](https://www.boost.org/doc/libs/release/libs/asio/) - Async I/O
- [Boost.Beast](https://www.boost.org/doc/libs/release/libs/beast/) - HTTP/WebSocket

## 📚 More Examples

Check out the complete examples:
- [Nameserver](npnameserver/npnameserver.cpp) - Service discovery server
- [Calculator Service](../nscalc/server/src/main.cpp) - Full-featured web service
- [TypeScript Client](../nscalc/client/src/rpc/rpc.ts) - Browser client
- [Test Suite](test/js/test/nprpc-integration.test.ts) - Comprehensive tests

---

**NPRPC** - 2-4x faster than gRPC, with zero-copy shared memory reaching 4.28 GiB/s! 🚀

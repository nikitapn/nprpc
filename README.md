# NPRPC - Multi-Transport RPC Framework

NPRPC is a high-performance, multi-transport RPC framework for distributed systems. It features a compact binary protocol with flat-buffer serialization, a type-safe IDL with code generation for C++, TypeScript, and Swift, and first-class streaming support (server, client, and bidirectional streams) over every transport.

## Key Features

- **Multiple transports** — WebSocket (WS/WSS), HTTP/HTTPS, HTTP/3, TCP, Shared Memory, QUIC, WebTransport
- **Streaming RPC** — server (`stream<T>`), client (`client_stream<T>`), and bidi (`bidi_stream<In,Out>`) streams with C++20 coroutines
- **Type-safe IDL** — `.npidl` → C++/TypeScript/Swift stubs with `npidl`
- **Cross-language** — seamless C++ ↔ TypeScript/JavaScript ↔ Swift interop
- **Browser-first** — WebSocket, HTTP, and WebTransport endpoints; `host.json` bootstrap for static deployments
- **SSR support** — built-in SvelteKit SSR via shared memory IPC (see [docs/SSR_ARCHITECTURE.md](docs/SSR_ARCHITECTURE.md))
- **Cookie auth** — httpOnly cookie-based auth for HTTP/WebSocket (see [docs/HTTP_AUTH.md](docs/HTTP_AUTH.md))
- **POA** — Portable Object Adapter for lifecycle management and session-scoped activation
- **Nameserver** — service discovery and named object binding

## Transport Overview

| Transport | Use Case | Notes |
|-----------|----------|-------|
| **TCP** | Native IPC, microservices | Lowest overhead, no browser support; optional io_uring backend (experimental) |
| **WebSocket** | Real-time, bidirectional | Persistent connection, streams supported; TLS via WSS |
| **HTTP** | Stateless web APIs | Browser-compatible, SSR-capable; TLS via HTTPS |
| **HTTP/3** | Modern web | QUIC-based, SSR-capable; requires `-DNPRPC_ENABLE_HTTP3=ON` |
| **WebTransport** | Browser streaming | Multiplexed streams over HTTP/3; native stream mapping for `stream<T>` |
| **QUIC** | Native next-gen | Multiplexed, encrypted; requires `-DNPRPC_ENABLE_QUIC=ON` |
| **Shared Memory** | Same-machine IPC | Zero-copy in some cases; extremely low latency |

## Quick Start

### 1. Define Your Interface (IDL)

```
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
npidl calculator.npidl --cpp --ts        # C++ + TypeScript
npidl calculator.npidl --cpp --ts --swift # add Swift
```

Generates `calculator.hpp` / `calculator.cpp` (C++) and `calculator.ts` (TypeScript).

### 3. Implement the Server (C++)

```cpp
#include <nprpc/nprpc.hpp>
#include "calculator.hpp"

class CalculatorImpl : public example::ICalculator_Servant {
public:
  double Add(double a, double b) override { return a + b; }
  double Subtract(double a, double b) override { return a - b; }
  double Multiply(double a, double b) override { return a * b; }
  double Divide(double a, double b) override {
    if (b == 0.0) throw example::CalculationError{"Division by zero", 1};
    return a / b;
  }
};

int main() {
  // Build RPC — chain transport builders, then call build()
  auto* rpc = nprpc::RpcBuilder()
    .set_log_level(nprpc::LogLevel::info)
    .with_hostname("localhost")
    .with_tcp(15000)
    .with_http(8080)
      .root_dir("./public")
    .build();

  // Create a POA
  auto* poa = nprpc::PoaBuilder(rpc)
    .with_max_objects(10)
    .with_lifespan(nprpc::PoaPolicy::Lifespan::Persistent)
    .build();

  // Activate object — specify which transports it accepts
  CalculatorImpl calc;
  auto oid = poa->activate_object(
    &calc,
    nprpc::ObjectActivationFlags::tcp  |
    nprpc::ObjectActivationFlags::ws   |
    nprpc::ObjectActivationFlags::http
  );

  // Publish for browser clients (writes <root_dir>/host.json)
  rpc->add_to_host_json("calculator", oid);
  rpc->produce_host_json();

  // Or register with the nameserver
  auto ns = rpc->get_nameserver("localhost:15001");
  ns->Bind(oid, "calculator");

  rpc->run(); // blocks; use rpc->start_thread_pool(n) for async
  return 0;
}
```

### 4. Use the Client (TypeScript)

#### Via WebSocket (persistent connection)

```typescript
import * as NPRPC from 'nprpc';
import * as example from './gen/calculator';

const rpc = await NPRPC.init();
const ns = NPRPC.get_nameserver('localhost:15001');
const ref = NPRPC.make_ref<NPRPC.ObjectProxy>();
await ns.Resolve('calculator', ref);

const calc = NPRPC.narrow(ref.value, example.Calculator);
console.log(await calc.Add(10, 20));   // 30

try {
  await calc.Divide(10, 0);
} catch (e) {
  if (e instanceof example.CalculationError)
    console.error(`${e.code}: ${e.message}`);
}
```

#### Via HTTP (stateless, from `host.json`)

```typescript
// host.json is served by the C++ server at /host.json
const host = await fetch('/host.json').then(r => r.json());
const calc = NPRPC.narrow(host.objects.calculator, example.Calculator);

// .http sub-proxy returns values directly
console.log(await calc.http.Add(10, 20));  // 30
```

## Streaming RPC

All three stream directions are supported at the IDL level and map to C++20 coroutines on the server side and range-based iteration / `AsyncThrowingStream` on clients.

### IDL Syntax

```
interface FileServer {
  // Server → Client  (stream<T> is an alias for server_stream<T>)
  stream<vector<u8>>       DownloadFile(filename: in string);

  // Client → Server  (void reply after stream closes)
  void UploadFile(filename: in string, data: client_stream<vector<u8>>);

  // Bidirectional
  bidi_stream<string, string> Chat(room: in string);
}
```

Optional `in` parameters before the stream keyword are sent in the handshake phase; the server can raise exceptions there before any data flows.

### C++ Server (coroutine)

```cpp
// server_stream — return a StreamWriter<T> coroutine
nprpc::StreamWriter<uint8_t>
FileServerImpl::DownloadFile(std::string_view filename) {
  auto data = read_file(filename);
  for (uint8_t byte : data)
    co_yield byte;
}

// client_stream — StreamReader<T> delivered as a parameter
void FileServerImpl::UploadFile(
    std::string_view filename,
    nprpc::StreamReader<std::vector<uint8_t>>& data) {
  std::ofstream out(std::string(filename), std::ios::binary);
  for (auto& chunk : data)   // blocking range-based for
    out.write((char*)chunk.data(), chunk.size());
}
```

### C++ Client

```cpp
// Server stream — range-based for loop
auto reader = file_server->DownloadFile("large.bin");
for (auto& chunk : reader)
  process(chunk);

// Client stream — write chunks then close
auto writer = file_server->UploadFile("upload.bin");
while (has_data())
  writer.send(next_chunk());
writer.close();
```

### TypeScript Client

```typescript
// Server stream
const stream = await fileServer.DownloadFile('large.bin');
for await (const chunk of stream) {
  process(chunk);
}

// Bidirectional
const chat = await chatService.Chat('lobby');
chat.send('Hello!');
for await (const msg of chat) {
  console.log(msg);
}
```

### Swift Client

```swift
// Server stream — AsyncThrowingStream
let stream = try client.downloadFile(filename: "large.bin")
for try await chunk in stream {
    process(chunk)
}
```

## WebTransport

WebTransport is the browser-native streaming transport built on HTTP/3. When an object is advertised with a secured HTTPS endpoint and the server has `enable_http3()` active, browsers can open a WebTransport session to `https://host:port/wt` and use it as the NPRPC transport.

- Unary RPC uses a single reliable bidirectional control stream.
- `stream<T>` methods map to server-opened unidirectional WebTransport streams.
- `client_stream<T>` maps to client-opened unidirectional streams.
- `bidi_stream<In,Out>` maps to a dedicated bidirectional stream.

Enable it server-side by activating objects with `ObjectActivationFlags::https` and calling `enable_http3()` on the HTTP builder:

```cpp
auto* rpc = nprpc::RpcBuilder()
  .with_hostname("example.com")
  .with_http(443)
    .ssl("cert.crt", "key.key")
    .enable_http3()
  .build();

auto oid = poa->activate_object(&servant,
    nprpc::ObjectActivationFlags::https);
```

The TypeScript runtime automatically prefers WebTransport when available (`globalThis.WebTransport` present) and the object carries a secured endpoint.

## Advanced Features

### SSL / TLS

Chain `.ssl()` on the HTTP or QUIC builder. The `dhparams` file is optional.

```cpp
auto* rpc = nprpc::RpcBuilder()
  .with_hostname("example.com")
  .with_http(443)
    .ssl("cert.crt", "key.key", "dhparam.pem")
    .enable_http3()
  .with_tcp(15000)
  .build();

// Activate for secure WebSocket only
poa->activate_object(&obj, nprpc::ObjectActivationFlags::wss);
```

```typescript
// TypeScript client automatically uses wss:// when served over HTTPS
const rpc = await NPRPC.init();
```

### HTTP/3 Launch Requirements on Linux

When HTTP/3 is enabled with multiple workers, NPRPC uses an eBPF `SO_REUSEPORT`
selector with a reuseport sockarray to keep QUIC packets pinned to the correct
worker. On Linux this is not an unprivileged operation.

If your application links against `libnprpc` and starts an HTTP/3 server with
more than one worker, grant capabilities to your application executable after
each build:

```bash
sudo setcap cap_net_admin,cap_bpf+ep /path/to/your_server_binary
getcap /path/to/your_server_binary
```

Capabilities must be applied to the final executable that starts the NPRPC
runtime, not to `libnprpc.so`.

Notes:

- Rebuilding the binaries may clear file capabilities, so scripts that launch
  HTTP/3 servers should re-apply them after each rebuild.
- If capabilities are unavailable, the safe fallback is to run HTTP/3 with a
  single worker; multi-worker HTTP/3 requires the reuseport BPF path.
- In Docker, grant the container the matching capabilities, for example
  `--cap-add=NET_ADMIN --cap-add=BPF`.

### Nameserver

```cpp
// Server: bind by name
auto ns = rpc->get_nameserver("localhost:15001");
ns->Bind(calc_oid,  "calculator");
ns->Bind(auth_oid,  "authorizator");
```

```typescript
// Client: resolve by name
const ref = NPRPC.make_ref<NPRPC.ObjectProxy>();
if (await nameserver.Resolve('calculator', ref))
  const calc = NPRPC.narrow(ref.value, example.Calculator);
```

### Deterministic Object IDs

Use `ObjectIdPolicy::UserSupplied` when you need stable IDs baked into a web bundle:

```cpp
auto* poa = nprpc::PoaBuilder(rpc)
  .with_lifespan(nprpc::PoaPolicy::Lifespan::Persistent)
  .with_object_id_policy(nprpc::PoaPolicy::ObjectIdPolicy::UserSupplied)
  .with_max_objects(16)
  .build();

constexpr nprpc::oid_t kCalcId = 0;
poa->activate_object_with_id(kCalcId, &calc,
    nprpc::ObjectActivationFlags::tcp | nprpc::ObjectActivationFlags::http);
```

IDs must be in `[0, max_objects)`. With `UserSupplied`, `activate_object` (auto-ID) is disabled to prevent mismatches.

### Shared Memory Transport

```cpp
// Server
auto* rpc = nprpc::RpcBuilder()
  .with_hostname("localhost")
  .build();

poa->activate_object(&obj, nprpc::ObjectActivationFlags::shm);
rpc->run();
```

```cpp
// Client (same machine)
auto* rpc = nprpc::RpcBuilder().build();
auto ns = rpc->get_nameserver("localhost:15001");
Object* obj;
ns->Resolve("my_object", obj);
auto* svc = nprpc::narrow<MyInterface>(obj);
svc->MyMethod(data);
```

### Session-Scoped Activation

Pass the current session context to restrict an object to the caller's connection:

```cpp
nprpc::ObjectId CreateProcessor() override {
  auto proc = std::make_unique<MyProcessor>();
  // Only the calling client can reach this object
  return poa_->activate_object(proc.release(),
      nprpc::ObjectActivationFlags::all,
      &nprpc::get_context());
}
```

### Object References as Parameters

```
// IDL — use `object` for interface-typed parameters
interface ObjectManager {
  object CreateProcessor(type: in string);
  void   RegisterProcessor(proc: in object);
}
```

```cpp
void RegisterProcessor(nprpc::Object* proc) override {
  auto* typed = nprpc::narrow<IDataProcessor>(proc);
  if (!typed) throw nprpc::Exception("wrong type");
  processors_.emplace_back(typed);
}
```

TypeScript servants can be passed as parameters too — the server can call back on them over a bidirectional transport (WebSocket/WebTransport):

```typescript
class MyProcessor extends example.IDataProcessor_Servant {
  ProcessData(data: Uint8Array): void { /* ... */ }
}
const proc = new MyProcessor();
await manager.RegisterProcessor(proc);
```

### Server-Side Rendering (SSR)

NPRPC can serve SvelteKit apps with full SSR over HTTP/3. See [docs/SSR_ARCHITECTURE.md](docs/SSR_ARCHITECTURE.md) for setup and architecture details.

```cpp
auto* rpc = nprpc::RpcBuilder()
  .with_hostname("mysite.com")
  .with_http(443)
    .ssl("cert.crt", "key.key")
    .root_dir("/srv/www")
    .enable_http3()
    .enable_ssr("/srv/www") // path to SvelteKit build containing index.js
  .build();
rpc->run();
```

### Cookie-Based Authentication

See [docs/HTTP_AUTH.md](docs/HTTP_AUTH.md) for the full API reference. Quick example:

```cpp
// Inside any servant method — read / write httpOnly cookies
auto token = nprpc::get_cookie("session");
nprpc::set_cookie("session", new_token, {
    .http_only = true, .secure = true, .same_site = "Strict", .max_age = 86400
});
```

## IDL Reference

### Types

| Category | Tokens |
|----------|--------|
| Booleans | `boolean` |
| Integers | `i8` `i16` `i32` `i64` `u8` `u16` `u32` `u64` |
| Floats   | `f32` `f64` |
| String   | `string` |
| Object ref | `object` |
| Dynamic array | `T[]` or `vector<T>` |
| Fixed array | `T[N]` |
| Alias | `alias Foo = vector<Bar>` |
| Discriminated union | `alias Foo = one of { arm1: T1; arm2: T2; }` |

### Discriminated Unions (`one of`)

The `one of` construct defines a tagged union of named struct arms. It generates a `std::variant`-backed C++ type with a `Kind` enum, a TypeScript discriminated union, and a Swift enum with associated values.

```
// IDL
message MsgA { id: u32; label: string; }
message MsgB { code: u32; detail: string; }

alias MyVariant = one of {
  msgA: MsgA;
  msgB: MsgB;
};

message Envelope {
  seq: u32;
  payload: MyVariant;
}

interface EventService {
  void Send(event: MyVariant, echo: out MyVariant);
}
```

**C++ usage:**

```cpp
// Construct
MyVariant v{ MyVariant::Kind::msgA, MsgA{42u, "hello"} };

// Dispatch
std::visit([](auto&& arm) {
  using T = std::decay_t<decltype(arm)>;
  if constexpr (std::is_same_v<T, MsgA>)
    std::cout << "A: " << arm.label;
  else if constexpr (std::is_same_v<T, MsgB>)
    std::cout << "B: " << arm.code;
}, v.value);
```

**TypeScript usage:**

```typescript
// Construct
const v: MyVariant = { kind: 'msgA', value: { id: 42, label: 'hello' } };

// Narrow
if (v.kind === 'msgA') {
  console.log(v.value.label);
}
```

**Swift usage:**

```swift
// Construct
let v = MyVariant.msgA(MsgA(id: 42, label: "hello"))

// Switch
switch v {
case .msgA(let a): print("A: \(a.label)")
case .msgB(let b): print("B: \(b.code)")
}
```

### Qualifiers

- `?` — nullable/optional field or parameter
- `in` — input parameter (by value)
- `out` — output parameter
- `raises(E1, E2)` — exception specification
- `async` — fire-and-forget (no reply)
- `[unreliable]` — best-effort delivery (QUIC/WebTransport only, others ignore)
- `[force_helpers=1]` — emit helper `from_flat` / `to_flat` functions for a `message`
- `[trusted=true]` — disable strict bounds checking for untrusted input

### Streaming IDL

```
interface DataService {
  // Server → Client
  stream<vector<u8>>       Download(id: in u32) raises(NotFound);
  // server_stream<T> is the canonical spelling; stream<T> is an alias

  // Client → Server
  void Upload(name: in string, data: client_stream<vector<u8>>);

  // Bidirectional
  bidi_stream<string, string> Chat(room: in string);
  bidi_stream<AAA, CCC>       Transform(suffix: in string);
}
```

### Full Example

```
module blog;

exception NotFound { id: u32; }

message Post { id: u32; title: string; body: string; }

interface BlogService {
  Post        GetPost(id: in u32) raises(NotFound);
  vector<Post> ListPosts(page: in u32, size: in u32);
  async       DeletePost(id: in u32);

  // Stream all posts matching a query
  stream<Post> Search(query: in string) raises(NotFound);

  // Live feed — bidi (client sends ack, server sends posts)
  bidi_stream<u32, Post> LiveFeed(channel: in string);
}
```

## Swift Bindings

NPRPC provides native Swift bindings via Swift 6.2+ C++ interop. The full feature set is supported: servants, client proxies, exceptions, object references, async methods, and all three stream directions.

### Building (Docker workflow)

Swift must be built inside a dedicated Docker container because it requires NPRPC and Boost to be compiled with Swift's bundled Clang toolchain.

```bash
cd nprpc_swift

# Step 1 — build the Docker image (once)
cd ..
./build-dev-image.sh          # builds nprpc-dev:latest used by CMake examples
cd nprpc_swift

# OR build the Swift-specific image directly
docker build -f Dockerfile \
  --build-arg USER_ID=$(id -u) \
  --build-arg GROUP_ID=$(id -g) \
  --build-arg USERNAME=$(id -un) \
  -t nprpc-swift-ubuntu ..

# Step 2 — build Boost + OpenSSL inside the container (first time only, ~10 min)
./docker-build-boost.sh

# Step 3 — build libnprpc.so with Swift's Clang
./docker-build-nprpc.sh

# Step 4 — generate Swift stubs from IDL (requires npidl built in Step 3)
./gen_stubs.sh

# Step 5 — build and optionally test the Swift package
./docker-build-swift.sh          # build only
./docker-build-swift.sh --test   # build + run tests (timeout 15 s)
```

To rebuild the Docker image (after Dockerfile changes):

```bash
./docker-build-nprpc.sh --rebuild
```

### Generate Swift Stubs

```bash
# Run from repo root; npidl must be built first
npidl myservice.npidl --swift --output-dir nprpc_swift/Sources/NPRPC/Generated
```

### Implement a Servant

```swift
import NPRPC

class CalculatorImpl: CalculatorServant, @unchecked Sendable {
    override func add(a: Float64, b: Float64) throws -> Float64 { a + b }
    override func divide(a: Float64, b: Float64) throws -> Float64 {
        guard b != 0 else { throw CalculationError(message: "div/0", code: 1) }
        return a / b
    }
}
```

### Activate and Call

```swift
let rpc = try RpcBuilder()
    .setLogLevel(.info)
    .setHostname("localhost")
    .withTcp(15000)
    .withHttp(15001)
        .ssl(certFile: "cert.crt", keyFile: "key.key")
    .build()

let poa = try rpc.createPoa(maxObjects: 100)

let servant = CalculatorImpl()
let oid = try poa.activateObject(servant, flags: [.tcp, .ws])

let obj = NPRPCObject.fromObjectId(oid)!
let client = narrow(obj, to: Calculator.self)!
let result = try client.add(a: 10, b: 20)   // 30.0
```

### Streaming (Swift)

```swift
// Server stream
let stream = try client.downloadFile(filename: "data.bin")
for try await chunk in stream {
    process(chunk)
}

// Async fire-and-forget
await client.playerMoved(x: 1.0, y: 2.0, z: 0.0)

// Async with out value
let reply = try await client.method2(arg1: 42)
```

See [nprpc_swift/README.md](nprpc_swift/README.md) and [nprpc_swift/EXAMPLES.md](nprpc_swift/EXAMPLES.md) for more.

## Building

```bash
# Standard dev build (Ninja, RelWithDebInfo, all features)
./configure.sh
cmake --build .build_release -j$(nproc)

# Minimal build (library only)
cmake -S . -B build
cmake --build build -j$(nproc)

# With QUIC + HTTP/3
cmake -S . -B build -DNPRPC_ENABLE_QUIC=ON -DNPRPC_ENABLE_HTTP3=ON
cmake --build build -j$(nproc)

# Run C++ tests
./run_cpp_test.sh        # or: ctest --output-on-failure --test-dir .build_release
```

See [docs/BUILD.md](docs/BUILD.md) for all CMake options, the JS/TS build, and install instructions.

## Performance

NPRPC is benchmarked against gRPC and Cap'n Proto RPC.

```bash
./run_benchmark.sh                                    # all suites
./run_benchmark.sh --benchmark_filter=EmptyCall       # latency
./run_benchmark.sh --benchmark_filter=LargeData1MB    # throughput
```

See [benchmark/README.md](benchmark/README.md) for methodology and results.

## More Resources

| Topic | Document |
|-------|----------|
| Full build options | [docs/BUILD.md](docs/BUILD.md) |
| SSR architecture | [docs/SSR_ARCHITECTURE.md](docs/SSR_ARCHITECTURE.md) |
| Cookie auth API | [docs/HTTP_AUTH.md](docs/HTTP_AUTH.md) |
| HTTP/3 + WebTransport debugging | [.github/skills/http3-webtransport-debugging/SKILL.md](.github/skills/http3-webtransport-debugging/SKILL.md) |
| Nameserver source | [npnameserver/npnameserver.cpp](npnameserver/npnameserver.cpp) |
| Swift integration tests | [nprpc_swift/Tests/NPRPCTests/IntegrationTest.swift](nprpc_swift/Tests/NPRPCTests/IntegrationTest.swift) |
| C++ test suite | [test/src/](test/src/) |
| JS/TS test suite | [test/js/](test/js/) |
| Live Blog example | [examples/live-blog/README.md](examples/live-blog/README.md) |

## License

See [LICENSE](LICENSE).

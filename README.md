# NPRPC - Multi-Transport RPC Framework

NPRPC is a high-performance, multi-transport RPC framework for distributed systems. It features a compact binary protocol with flat-buffer serialization, a type-safe IDL with code generation for C++, TypeScript, and Swift, and first-class streaming support (server, client, and bidirectional streams) over every transport.

## Key Features

- **Multiple transports** — WebSocket (WS/WSS), HTTP/HTTPS, HTTP/3, TCP, Shared Memory, QUIC, WebTransport
- **Streaming RPC** — server (`stream<T>`), client (`client_stream<T>`), and bidi (`bidi_stream<In,Out>`) streams with C++20 coroutines
- **Type-safe IDL** — `.npidl` → C++/TypeScript/Swift stubs with `npidl`
- **Cross-language** — seamless C++ ↔ TypeScript/JavaScript ↔ Swift interop
- **Browser-first** — WebSocket, HTTP, and WebTransport endpoints; `host.json` bootstrap for static deployments
- **Server-rendered pages** — render HTML in the process that owns the services (see [docs/PAGE_RENDERING.md](docs/PAGE_RENDERING.md))
- **Cookie auth** — httpOnly cookie-based auth for HTTP/WebSocket (see [docs/HTTP_AUTH.md](docs/HTTP_AUTH.md))
- **POA** — Portable Object Adapter for lifecycle, transports, and dispatch placement (see [docs/POA.md](docs/POA.md))
- **Nameserver** — service discovery and named object binding

## Transport Overview

| Transport | Use Case | Notes |
|-----------|----------|-------|
| **TCP** | Native IPC, microservices | Lowest overhead, no browser support; optional io_uring backend (experimental); `-DNPRPC_ENABLE_TCP=OFF` to disable |
| **WebSocket** | Real-time, bidirectional | Persistent connection, streams supported; TLS via WSS; `-DNPRPC_ENABLE_WEBSOCKET=OFF` to disable |
| **HTTP** | Stateless web APIs | Browser-compatible, page rendering; TLS via HTTPS; `-DNPRPC_ENABLE_HTTP=OFF` to disable |
| **HTTP/3** | Modern web | QUIC-based, page rendering; requires `-DNPRPC_ENABLE_HTTP3=ON` |
| **WebTransport** | Browser streaming | Multiplexed streams over HTTP/3; native stream mapping for `stream<T>` |
| **QUIC** | Native next-gen | Multiplexed, encrypted; requires `-DNPRPC_ENABLE_QUIC=ON` |
| **Shared Memory** | Same-machine IPC | Zero-copy in some cases; extremely low latency. Always compiled in. |

TLS for HTTPS/WSS is optional (`-DNPRPC_ENABLE_SSL=OFF`). A shared-memory-only build (TCP/HTTP/WebSocket/SSL all off) does not need OpenSSL or liburing. See [docs/BUILD.md](docs/BUILD.md).

## Quick Start

[docs/GETTING_STARTED.md](docs/GETTING_STARTED.md) walks through this example
step by step, including the CMake setup.

### 1. Define the interface

```npidl
// idl/calculator.npidl
module example;

/// Raised by Divide for a zero divisor.
exception CalculationError {
  reason: string;
}

interface Calculator {
  f64 Add(a: f64, b: f64);
  f64 Divide(a: f64, b: f64) raises(CalculationError);
}
```

`npidl` generates the C++ (`--cpp`), TypeScript (`--ts`) and Swift (`--swift`)
code; from CMake, `npidl_generate_idl_files` does it at build time.

### 2. Implement the server (C++)

```cpp
#include <fstream>
#include <nprpc/nprpc.hpp>
#include "calculator.hpp"

class CalculatorImpl : public example::ICalculator_Servant {
public:
  double Add(double a, double b) override { return a + b; }
  double Divide(double a, double b) override {
    if (b == 0)
      throw example::CalculationError("division by zero");
    return a / b;
  }
};

int main() {
  auto* rpc = nprpc::RpcBuilder()
                  .with_hostname("localhost")
                  .with_tcp(15100)
                  .build();

  // Persistent: objects published at startup live until the server stops.
  auto* poa = rpc->create_poa()
                  .with_lifespan(nprpc::PoaPolicy::Lifespan::Persistent)
                  .build();
  auto oid = poa->activate_object(new CalculatorImpl(),
                                  nprpc::ObjectActivationFlags::tcp);

  std::ofstream("calculator.ref") << oid.to_string();
  rpc->run();
}
```

### 3. Call it (C++)

```cpp
auto* rpc = nprpc::RpcBuilder().build();
rpc->start_thread_pool(1);

nprpc::ObjectPtr<example::Calculator> calc(
    nprpc::narrow<example::Calculator>(nprpc::Object::from_string(ref)));

calc->Add(2, 3);                 // 5
try {
  calc->Divide(1, 0);
} catch (const example::CalculationError& e) {
  // e.reason == "division by zero"
}
```

### 4. Call it from a browser (TypeScript)

Add `.with_http(8080).root_dir("www")` to the server's builder, activate with
the `ws | http` flags, and publish the object in `host.json`:

```cpp
rpc->add_to_host_json("calculator", oid);
rpc->produce_host_json();
```

```typescript
import * as NPRPC from 'nprpc';
import { Calculator } from './gen/calculator';

const rpc = await NPRPC.init();   // loads host.json
const calc = NPRPC.narrow(rpc.host_info.objects.calculator, Calculator);
console.log(await calc.Add(2, 3));   // 5
```

## Streaming RPC

A method can send a sequence of values: server streams, client streams, and
bidirectional streams, with per-stream flow control on every transport. See
[docs/STREAMS.md](docs/STREAMS.md).

```npidl
interface Feed {
  stream<Item> Watch(limit: u32);                    // server -> client
  void Upload(tag: string, items: client_stream<Item>); // client -> server
  bidi_stream<Item, Reply> Echo(prefix: string);     // both ways
}
```

```cpp
// Caller
for (auto& item : feed->Watch(10))
  show(item);

auto upload = feed->Upload("batch-1");   // the stream parameter is returned
upload.write(item);
upload.close();

// Servant: stream methods are coroutines
nprpc::StreamWriter<Item> Watch(uint32_t limit) override {
  for (uint32_t i = 0; i < limit; ++i)
    co_yield Item{i, "item"};
}

nprpc::Task<> Upload(std::string tag, nprpc::StreamReader<Item> items) override {
  while (auto item = co_await items)
    store(tag, *item);
}
```

```typescript
for await (const item of await feed.Watch(10)) show(item);
```

```swift
for try await item in try feed.watch(limit: 10) { show(item) }
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

auto oid = poa->activate_object(new MyServant(),
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
poa->activate_object(new MyServant(), nprpc::ObjectActivationFlags::wss);
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

`npnameserver` keeps a directory of objects by name. It listens on TCP port
15000 and HTTP/WebSocket port 15001; `npnameserver --help` lists its options
(hostname, TLS certificate, allowed browser origins).

```cpp
// Server: bind by name. get_nameserver takes the host; the ports are fixed.
auto ns = rpc->get_nameserver("127.0.0.1");   // ObjectPtr<common::Nameserver>
ns->Bind(calc_oid, "calculator");

// Client: resolve by name
nprpc::Object* obj = nullptr;
if (ns->Resolve("calculator", obj)) {
  nprpc::ObjectPtr<example::Calculator> calc(nprpc::narrow<example::Calculator>(obj));
}
```

```typescript
const ns = NPRPC.get_nameserver('localhost');
const ref = NPRPC.make_ref<NPRPC.ObjectProxy>();
if (await ns.Resolve('calculator', ref)) {
  const calc = NPRPC.narrow(ref.value, example.Calculator);
}
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
poa->activate_object_with_id(kCalcId, new CalculatorImpl(),
    nprpc::ObjectActivationFlags::tcp | nprpc::ObjectActivationFlags::http);
```

IDs must be in `[0, max_objects)`. With `UserSupplied`, `activate_object` (auto-ID) is disabled to prevent mismatches.

### Shared Memory Transport

```cpp
// Server
auto* rpc = nprpc::RpcBuilder()
  .with_hostname("localhost")
  .build();

poa->activate_object(new MyServant(), nprpc::ObjectActivationFlags::shm);
rpc->run();
```

```cpp
// Client (same machine)
auto* rpc = nprpc::RpcBuilder().build();
rpc->start_thread_pool(1);
auto ns = rpc->get_nameserver("127.0.0.1");   // ObjectPtr<common::Nameserver>
nprpc::Object* obj = nullptr;
ns->Resolve("my_object", obj);
nprpc::ObjectPtr<MyInterface> svc(nprpc::narrow<MyInterface>(obj));
svc->MyMethod(data);   // picks shared memory when the server is local
```

#### What a client costs

Every accepted client gets two rings — one each way — created by the server
and **resident from the moment they exist**, not faulted in as they fill. The
default is 1 MiB each, so 2 MiB per connected client whether it is busy or
idle, and the largest single message is half a ring:

```cpp
auto* rpc = nprpc::RpcBuilder()
  .shm_channel_sizes(4 * 1024 * 1024, 3 * 1024 * 1024) // ring, max message
  .build();
```

Only the server says this. It writes both numbers into the ring header at
creation and whoever opens the ring adopts them, so clients need no matching
configuration and two differently-configured builds cannot disagree about the
same ring.

Raise it when replies are large — a message the ring cannot hold is refused
rather than queued, with an error naming this call. But before raising it for
everybody, consider that one big payload is usually better carried in a
segment of its own than paid for on every connection, idle ones included.

#### What happens when a process dies

The server removes a client's rings when the channel closes, which covers
every orderly ending and none of the others: killed, crashed, or `exit()`
without unwinding, it runs no destructor and its rings stay in `/dev/shm`
holding real memory until the machine reboots. A day of restarts during
development is measured in gigabytes.

Nothing in the dying process can fix that, so the next server to start does
it: `SharedMemoryListener` sweeps stale segments before creating its own. A
segment goes only when a process named in it is provably gone — by pid *and*
start time, so a recycled pid cannot condemn a live channel — and no other
end of the same channel is still alive. Anything unreadable, or not yet
claimed by a writer, is left where it is.

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

### Server-Rendered Pages

A page handler renders HTML in the process that owns the services, so a page is
built from data the server already holds — no second process and no hydration
round-trip. Returning `std::nullopt` declines a request, which falls through to
the zero-copy static file cache. See [docs/PAGE_RENDERING.md](docs/PAGE_RENDERING.md).

```cpp
auto* rpc = nprpc::RpcBuilder()
  .with_hostname("mysite.com")
  .with_http(443)
    .ssl("cert.crt", "key.key")
    .root_dir("/srv/www")
    .enable_http3()
    .with_page_handler([&](const nprpc::PageRequest& req)
                           -> std::optional<nprpc::PageResponse> {
      if (req.path != "/blog") return std::nullopt;  // -> static files
      nprpc::PageResponse res;
      res.body = render_blog(repository.list_posts(1, 10));
      return res;
    })
  .build();
rpc->run();
```

### Cookie-Based Authentication

See [docs/HTTP_AUTH.md](docs/HTTP_AUTH.md) for the full API reference. Quick example:

```cpp
// Inside any servant method — read / write httpOnly cookies
auto token = nprpc::http::get_cookie("session");
nprpc::http::set_cookie("session", new_token, {
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
- `[unreliable]` — fire-and-forget RPC (must be `void`, only `in` args, no `raises`) or best-effort streams (QUIC DATAGRAM when available; other transports may still deliver the bytes)
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

NPRPC provides native Swift bindings via Swift 6.3+ C++ interop. The full feature set is supported: servants, client proxies, exceptions, object references, async methods, and all three stream directions.

### Building (Docker workflow)

Swift must be built inside a dedicated Docker container because it requires NPRPC and Boost to be compiled with Swift's bundled Clang toolchain.

```bash
# Step 1 — build the Docker image (once)
just build-dev-image          # builds nprpc-dev:latest used by CMake examples

# OR build the Swift-specific image directly
cd nprpc_swift
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
just gen-swift-stubs          # from repo root

# Step 5 — build and optionally test the Swift package
./docker-build-swift.sh          # build only
./docker-build-swift.sh --test   # build + run tests (timeout 15 s)

# Or all-in-one (stubs + docker build + test):
# just run-swift-tests
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

For the calculator IDL from the Quick Start:

```swift
import NPRPC

final class CalculatorImpl: CalculatorServant, @unchecked Sendable {
    override func add(a: Double, b: Double) -> Double { a + b }
    override func divide(a: Double, b: Double) throws -> Double {
        guard b != 0 else { throw CalculationError(reason: "division by zero") }
        return a / b
    }
}
```

A servant method is `throws` only when its IDL declares `raises`.

### Activate and Call

```swift
let rpc = try RpcBuilder()
    .withHostname("localhost")
    .withTcp(15000)
    .build()
try rpc.startThreadPool(2)

let poa = try rpc.createPoa(maxObjects: 100)   // persistent by default
let oid = try poa.activateObject(CalculatorImpl(), flags: [.tcp, .shm])

// Proxy methods are async.
let calc = narrow(NPRPCObject.fromObjectId(oid)!, to: Calculator.self)!
let sum = try await calc.add(a: 10, b: 20)   // 30.0
```

### Streaming (Swift)

```swift
for try await item in try feed.watch(limit: 10) {
    show(item)
}
```

See [docs/STREAMS.md](docs/STREAMS.md) for client and bidi streams.

See [nprpc_swift/README.md](nprpc_swift/README.md) and [nprpc_swift/EXAMPLES.md](nprpc_swift/EXAMPLES.md) for more.

## Building

```bash
# Standard dev build (Ninja, RelWithDebInfo, all features) — see `.env` for BUILD_DIR
just configure
just build

# Or a single target
just bt nprpc_test

# Run tests
just run-cpp-tests                    # C++ (ctest)
just run-js-tests                     # TypeScript / Mocha
just run-swift-tests                  # Swift in Docker (rebuilds nprpc)
just run-swift-tests-host             # Swift on host (reuses CMake build; sudo setcap)
just test-all --swift-host            # C++ + JS + host Swift  ← pre-merge gate
just test-all                         # C++ + JS + Docker Swift
just run-cpp-tests -R NprpcTest.TestBasic   # filtered

# Pre-merge: always run the full suite (C++ / JS / Swift) before merging to main.

# Minimal build (library only)
cmake -S . -B build
cmake --build build -j$(nproc)
```

See [docs/BUILD.md](docs/BUILD.md) for all CMake options, the JS/TS build, and install instructions. Run `just` for the full task list.

## Performance

NPRPC is benchmarked against gRPC and Cap'n Proto RPC.

```bash
just run-benchmarks                                    # all suites
just run-benchmarks --benchmark_filter=EmptyCall       # latency
just run-benchmarks --benchmark_filter=LargeData1MB    # throughput
```

See [benchmark/README.md](benchmark/README.md) for methodology and results.

## More Resources

| Topic | Document |
|-------|----------|
| First project, step by step | [docs/GETTING_STARTED.md](docs/GETTING_STARTED.md) |
| Full build options | [docs/BUILD.md](docs/BUILD.md) |
| Object lifetimes and dispatch | [docs/POA.md](docs/POA.md) |
| Streams | [docs/STREAMS.md](docs/STREAMS.md) |
| Server-rendered pages | [docs/PAGE_RENDERING.md](docs/PAGE_RENDERING.md) |
| Cookie auth API | [docs/HTTP_AUTH.md](docs/HTTP_AUTH.md) |
| API reference site | `just docs-api && just docs-serve` ([docs/site/README.md](docs/site/README.md)) |
| HTTP/3 + WebTransport debugging | [.github/skills/http3-webtransport-debugging/SKILL.md](.github/skills/http3-webtransport-debugging/SKILL.md) |
| Nameserver source | [npnameserver/npnameserver.cpp](npnameserver/npnameserver.cpp) |
| Swift integration tests | [nprpc_swift/Tests/NPRPCTests/IntegrationTest.swift](nprpc_swift/Tests/NPRPCTests/IntegrationTest.swift) |
| C++ test suite | [test/src/](test/src/) |
| JS/TS test suite | [test/js/](test/js/) |
| Live Blog example | [examples/live-blog/README.md](examples/live-blog/README.md) |

## License

See [LICENSE](LICENSE).

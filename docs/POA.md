# Portable Object Adapter (POA)

This document describes the NPRPC **POA** API: how servants are activated, which transports they accept, and **where their methods run** (threading / hop trees). Examples are given in **C++** and **Swift**.

Related tests:

- C++: `NprpcTest.PoaDispatchExecutor` in `test/src/basic.cpp`
- Swift: `PoaDispatchExecutorTests` in `nprpc_swift/Tests/NPRPCTests/PoaDispatchExecutorTests.swift`

---

## What is a POA?

A **Portable Object Adapter** is a container for **servants** (object implementations):

| Responsibility | Notes |
|----------------|--------|
| Object IDs | Assign or accept IDs; map `oid → servant` |
| Activation | Bind a servant to transports (TCP, SHM, WS, …) |
| Dispatch routing | Incoming `FunctionCall` / `StreamInit` look up POA + object |
| Lifecycle | Persistent vs transient policies |
| **Dispatch placement** | Optional executor + transport affinity (this doc’s focus) |

An application may use **several POAs**:

- A **hot-path POA** for cheap, latency-critical servants (default: run on the transport thread).
- A **UI POA** whose servants always run on the main queue (Swift) or another serial executor.

---

## Creating a POA

### C++

```cpp
#include <nprpc/nprpc.hpp>

auto* rpc = nprpc::RpcBuilder()
  .with_hostname("localhost")
  .with_tcp(15000)
  .build();
rpc->start_thread_pool(4);

// Default: persistent objects, system-generated IDs, dispatch on transport thread.
auto* poa = rpc->create_poa()
  .with_max_objects(128)
  .with_lifespan(nprpc::PoaPolicy::Lifespan::Persistent)
  .with_object_id_policy(nprpc::PoaPolicy::ObjectIdPolicy::SystemGenerated)
  .build();
```

Builder options:

| Method | Purpose |
|--------|---------|
| `with_max_objects(n)` | Capacity of the object table |
| `with_lifespan(Transient \| Persistent)` | Object lifetime policy |
| `with_object_id_policy(SystemGenerated \| UserSupplied)` | Who assigns OIDs |
| `with_dispatch_executor(DispatchExecutor)` | Hop target for servant work (optional) |
| `with_transport_affinity(TransportAffinity)` | Whether transport may run dispatch inline |

### Swift

```swift
import NPRPC

let rpc = try RpcBuilder()
  .setLogLevel(.info)
  .withHostname("localhost")
  .withTcp(15000)
  .build()
try rpc.startThreadPool(4)

// Default: inline on transport / SHM ring thread
let poa = try rpc.createPoa(maxObjects: 128)

// UI servants — whole request+reply hops to main (SHM path)
let uiPoa = try rpc.createPoa(
  maxObjects: 32,
  dispatch: .main
)

// Dedicated serial queue (must be serial for SHM FIFO replies)
let workQ = DispatchQueue(label: "app.nprpc.servants")
let workPoa = try rpc.createPoa(
  maxObjects: 64,
  dispatch: .queue(workQ)
)
```

`PoaDispatchExecutor`:

| Case | Meaning |
|------|---------|
| `.inlineOnTransportThread` | Default — lowest latency |
| `.main` | Fire-and-forget hop to `DispatchQueue.main` |
| `.queue(DispatchQueue)` | Hop to a **serial** custom queue |

---

## Activating objects

### C++

```cpp
class CalcImpl : public example::ICalculator_Servant {
public:
  double Add(double a, double b) override { return a + b; }
  // ...
};

CalcImpl calc;
auto oid = poa->activate_object(
  &calc,
  nprpc::ObjectActivationFlags::tcp |
  nprpc::ObjectActivationFlags::shm |
  nprpc::ObjectActivationFlags::ws
);

// Session-scoped (ephemeral) activation uses a SessionContext* when available.
// User-supplied IDs (UserSupplied policy):
//   poa->activate_object_with_id(manual_id, &calc, flags);
```

### Swift

```swift
let servant = CalculatorImpl()
let oid = try poa.activateObject(
  servant,
  flags: [.tcp, .shm, .ws]
)
// oid.urls contains transport URLs, e.g. "tcp://…;mem://…;web://…"
```

Common flags (Swift `ObjectActivationFlags` / C++ `ObjectActivationFlags`):

| Flag | Transport |
|------|-----------|
| `tcp` | TCP |
| `shm` | Shared memory |
| `ws` / `wss` | WebSocket |
| `http` / `https` | HTTP(S) |
| `quic` | QUIC |
| `wt` | WebTransport |
| `allowAll` | All of the above |
| `privateSession` | Session-scoped (ephemeral) |

---

## Dispatch placement policies

### `TransportAffinity`

```cpp
namespace nprpc::PoaPolicy {
enum class TransportAffinity {
  /// Prefer transport/ring thread when there is no DispatchExecutor (default).
  AllowBlockTransport = 0,
  /// Never run servant dispatch on the transport thread.
  NeverBlockTransport = 1,
};
}
```

```cpp
// Cheap, latency-critical (default affinity)
auto* hot = rpc->create_poa()
  .with_max_objects(64)
  .build();

// Keep SHM ring free even without a custom executor
auto* offload = rpc->create_poa()
  .with_max_objects(64)
  .with_transport_affinity(
      nprpc::PoaPolicy::TransportAffinity::NeverBlockTransport)
  .build();
```

### `DispatchExecutor` (C++)

Opaque C-callable hop, typically wired from Swift to GCD:

```cpp
struct DispatchExecutor {
  using WorkFn = void (*)(void* arg);
  using PostFn = void (*)(void* ctx, WorkFn fn, void* arg);
  using IsRunningOnFn = bool (*)(void* ctx);

  PostFn post = nullptr;              // schedule work (fire-and-forget OK)
  IsRunningOnFn is_running_on = nullptr; // avoid nested post / deadlock
  void* ctx = nullptr;
};
```

**Uses:**

1. **Fire-and-forget** (SHM offload): ring calls `post(ctx, drain_fn, job)` and returns immediately so the ring can free the slot.
2. **`invoke_sync`**: post+wait when code is not already on the target queue; if `is_running_on` is true, runs **inline**.

```cpp
nprpc::DispatchExecutor ex;
ex.post = my_post;                 // e.g. wrap a worker queue
ex.is_running_on = my_is_on_queue;
ex.ctx = my_ctx;

auto* uiPoa = rpc->create_poa()
  .with_max_objects(32)
  .with_dispatch_executor(ex)      // implies off-transport for SHM
  .build();
```

Swift does not require you to fill `DispatchExecutor` by hand — `createPoa(dispatch:)` installs the GCD trampolines.

### `requires_off_transport_dispatch()`

True when either:

- a non-empty `DispatchExecutor` is set, or  
- affinity is `NeverBlockTransport`.

SHM uses this to choose **inline** vs **offload**.

---

## Shared-memory receive path (hybrid)

1. **Always** copy the message out of the ring (owned buffer).
2. **Inline** (default POA, affinity allows, no in-flight offload drain):  
   `handle_request` on the **ring thread**.
3. **Offload** (executor / NeverBlock / drain already busy for FIFO):  
   enqueue; kick via **`DispatchExecutor.post`** if present, else **`asio::post(ioc)`**.

Replies stay **FIFO** on a single SHM session when offloads share one serial queue (e.g. main) or when the session serializes the drain. The client matches replies by ring slot order, not `request_id`.

---

## Threading hop trees

Trees below describe **server-side** placement of servant methods for a **Shared Memory** client call. TCP/WS/QUIC use similar policy for offload intent; their I/O threads differ (strand / epoll / etc.) but default remains “dispatch on the transport thread.”

### 1. Default POA — inline on the ring (lowest latency)

Typical for benchmarks and thin servants (~µs path).

```text
Client thread
  └─ send_receive (c2s ring)
       │
SHM ring consumer thread
  ├─ try_read_view / copy → owned rx
  ├─ commit_read (slot free)          [after callback returns]
  ├─ handle_request(rx, tx)           ← servant runs HERE
  │    └─ YourServant::Method(...)
  └─ commit_write (s2c reply)
       │
Client thread
  └─ wakes with reply
```

**Hops to servant:** 0 (same as ring consumer).

```swift
let poa = try rpc.createPoa(maxObjects: 64)  // .inlineOnTransportThread
```

```cpp
auto* poa = rpc->create_poa().with_max_objects(64).build();
```

---

### 2. UI POA — `dispatch: .main` (Swift)

Ring does **not** wait. One hop to main; no Asio in the path.

```text
Client thread
  └─ send_receive_async / await proxy method
       │
SHM ring consumer thread
  ├─ copy rx, commit_read
  ├─ DispatchExecutor.post(...)       ← fire-and-forget (GCD / main)
  └─ return (ring free for next msg)
       │
DispatchQueue.main
  ├─ drain_pending (serial)
  ├─ handle_request
  │    └─ invoke_sync → is_running_on(main)? yes → inline
  │         └─ YourServant.method()   ← MainActor-safe UI work
  └─ commit_write (s2c reply)
       │
Client thread
  └─ continuation resumes
```

**Hops to servant:** 1 (`ring → main`).

```swift
let uiPoa = try rpc.createPoa(maxObjects: 32, dispatch: .main)

final class DashboardServant: MyIfaceServant, @unchecked Sendable {
  override func updateTitle(title: String) throws {
    // Already on main — update UI directly
    self.label.stringValue = title
  }
}

let oid = try uiPoa.activateObject(servant, flags: .shm)
```

---

### 3. Custom serial queue

Same as main, but the hop target is your queue (must be **serial** for SHM reply order).

```text
SHM ring
  └─ post(custom serial queue)
       │
app.nprpc.servants (serial)
  └─ servant method + reply
```

```swift
let q = DispatchQueue(label: "app.nprpc.servants")  // serial by default
let poa = try rpc.createPoa(maxObjects: 64, dispatch: .queue(q))
```

---

### 4. `NeverBlockTransport` without a custom executor

Offload uses the process **Asio `io_context`** thread pool (generic C++ fallback).

```text
SHM ring
  └─ asio::post(ioc)
       │
rpc_worker_N (Asio)
  └─ handle_request + reply
```

**Hops to servant:** 1 (`ring → ioc`).

```cpp
auto* poa = rpc->create_poa()
  .with_max_objects(64)
  .with_transport_affinity(
      nprpc::PoaPolicy::TransportAffinity::NeverBlockTransport)
  .build();
```

---

### 5. Anti-pattern (avoided by design)

```text
// BAD historical shape — double hop + blocked ring
ring ──wait──► asio ──post+wait──► main ──servant──► …
```

Current offload with executor:

```text
// GOOD
ring ──post(f&f)──► main ──servant + reply──► …
```

`invoke_sync` is only a post+wait **when not already** on the target queue (`is_running_on`).

---

### Comparison

| POA setup | Servant thread | Ring blocked on servant? | Extra hops |
|-----------|----------------|---------------------------|------------|
| Default / AllowBlock | Ring consumer | Yes (cheap work only) | 0 |
| `.main` / GCD executor | Main (or custom serial) | No | 1 |
| NeverBlock, no executor | Asio worker | No | 1 |
| Executor **and** nested wait without `is_running_on` | — | Can deadlock | — |

---

## Choosing a policy

```text
                    ┌─────────────────────────────┐
                    │ Servant does UI / heavy I/O? │
                    └─────────────┬───────────────┘
                         yes      │      no
                          ▼       │       ▼
              ┌────────────────┐  │  ┌──────────────────────┐
              │ .main / .queue │  │  │ Default POA (inline) │
              │ or NeverBlock  │  │  │ lowest SHM latency   │
              └────────────────┘  │  └──────────────────────┘
                                  │
                    ┌─────────────┴──────────────┐
                    │ May block briefly but no UI?│
                    └─────────────┬──────────────┘
                           yes    │
                            ▼
                 NeverBlockTransport
                 (or private serial queue)
```

Guidelines:

- **Benchmark / Ping / pure compute on SHM** → default POA.
- **Swift UI** → `dispatch: .main` (or a dedicated serial queue if you must leave main free for rendering only).
- **Do not** block main on NPRPC that waits on main again.
- **Do not** use concurrent (global) queues for SHM offload if multiple replies can interleave on one connection—use **serial** queues.

---

## End-to-end examples

### C++: two POAs (hot + offload)

```cpp
auto* hot = rpc->create_poa()
  .with_max_objects(128)
  .build();

auto* slow = rpc->create_poa()
  .with_max_objects(32)
  .with_transport_affinity(
      nprpc::PoaPolicy::TransportAffinity::NeverBlockTransport)
  .build();

PingImpl ping;
HeavyImpl heavy;
hot->activate_object(&ping, nprpc::ObjectActivationFlags::shm);
slow->activate_object(&heavy, nprpc::ObjectActivationFlags::shm);
```

### Swift: UI POA + hot POA

```swift
let hotPoa = try rpc.createPoa(maxObjects: 128)
let uiPoa  = try rpc.createPoa(maxObjects: 32, dispatch: .main)

let metrics = MetricsServant()           // cheap
let dashboard = DashboardServant()       // touches AppKit/UIKit

_ = try hotPoa.activateObject(metrics, flags: .shm)
_ = try uiPoa.activateObject(dashboard, flags: [.shm, .ws])
```

### C++: custom executor sketch (worker thread)

```cpp
// Pseudocode — post to your own serial queue
struct MyCtx { /* queue + mutex + condvar or eventfd */ };

void my_post(void* ctx, void (*fn)(void*), void* arg) {
  static_cast<MyCtx*>(ctx)->enqueue([=]{ fn(arg); });
}
bool my_on_queue(void* ctx) {
  return static_cast<MyCtx*>(ctx)->is_this_thread();
}

nprpc::DispatchExecutor ex{my_post, my_on_queue, &g_ctx};
auto* poa = rpc->create_poa()
  .with_dispatch_executor(ex)
  .build();
```

---

## Related APIs

| API | Role |
|-----|------|
| `Session::handle_request` | Sync dispatch (TCP/QUIC/epoll default entry) |
| `Session::handle_request_async` | Awaitable form (body still sync today; SHM offload target) |
| `RpcImpl::get_poa` | `std::expected<PoaImpl*, PoaLookupError>` — no throw on bad index |
| `ObjectServant::dispatch` | Generated entry for one RPC |
| Nameserver `Bind` / `Resolve` | Publish and look up activated objects |

---

## Summary

- **POA** = servant container + activation flags + optional **where** dispatch runs.
- **Default** = transport/ring inline for minimum latency.
- **Swift UI** = `createPoa(dispatch: .main)` → ring posts once to main; servant and reply run there.
- **Affinity** = force offload without a custom queue (`NeverBlockTransport`).
- Prefer **serial** hop targets so SHM reply order stays correct.

# Portable Object Adapter (POA)

A POA holds your servants (the objects that implement IDL interfaces) and
routes incoming calls to them. You create one or more POAs, activate servants
in them, and hand the resulting object references to clients.

A POA decides three things about the objects in it:

- **Lifespan:** reference counted, or alive until deactivated.
- **Object ids:** assigned by the POA, or chosen by you.
- **Where servant methods run:** on the transport thread, or on a thread or
  queue you choose.

An application often uses several POAs, for example one for cheap,
latency-critical servants and one whose servants must run on the UI thread.

## Creating a POA

### C++

```cpp
#include <nprpc/nprpc.hpp>

auto* rpc = nprpc::RpcBuilder()
                .with_hostname("localhost")
                .with_tcp(15000)
                .build();
rpc->start_thread_pool(4);

auto* poa = rpc->create_poa()
                .with_max_objects(128)
                .with_lifespan(nprpc::PoaPolicy::Lifespan::Persistent)
                .build();
```

| Builder method | Default | Purpose |
|---|---|---|
| `with_max_objects(n)` | 32 | Objects active at once |
| `with_lifespan(...)` | `Transient` | `Transient`: owned by the clients that hold it; deleted when the last one releases it. `Persistent`: lives until deactivated. |
| `with_object_id_policy(...)` | `SystemGenerated` | Whether the POA or you assign object ids |
| `with_transport_affinity(...)` | `AllowBlockTransport` | Whether methods may run on the transport thread |
| `with_dispatch_executor(...)` | none | Run methods on your own executor |

A transient object belongs to the client it was created for, so it can only
be activated inside a call, with that client's session:
`activate_object(servant, flags, &nprpc::get_context())`. Objects you publish
at startup belong in a persistent POA.

The runtime owns the POA; remove it with `rpc->destroy_poa(poa)`, never
`delete`.

### Swift

```swift
import NPRPC

let rpc = try RpcBuilder()
    .withHostname("localhost")
    .withTcp(15000)
    .build()
try rpc.startThreadPool(4)

let poa = try rpc.createPoa(maxObjects: 128)
```

`createPoa` takes `lifetime:` (default `.Persistent`), `idPolicy:` (default
`.systemGenerated`) and `dispatch:` (default `.inlineOnTransportThread`; see
below).

## Activating objects

Activation registers a servant and returns its `ObjectId`: the reference
clients use to reach it. The flags choose which transports may call it.

### C++

```cpp
class CalculatorImpl : public example::ICalculator_Servant {
public:
  double Add(double a, double b) override { return a + b; }
};

auto oid = poa->activate_object(
    new CalculatorImpl(),
    nprpc::ObjectActivationFlags::tcp | nprpc::ObjectActivationFlags::shm |
        nprpc::ObjectActivationFlags::ws);
```

The runtime calls the servant's `destroy()` when it is done with it, which
deletes it by default, so allocate servants with `new`. With the
`UserSupplied` id policy, use `activate_object_with_id(id, servant, flags)`.

### Swift

```swift
let oid = try poa.activateObject(CalculatorImpl(), flags: [.tcp, .shm, .ws])
// oid.urls lists where it can be reached, e.g. "tcp://…;mem://…;web://…"
```

### Activation flags

| Flag | Transport |
|---|---|
| `tcp` | TCP |
| `shm` | Shared memory (same machine) |
| `ws` / `wss` | WebSocket |
| `http` / `https` | RPC over HTTP |
| `quic` | Native QUIC |
| `wt` | WebTransport |
| `privateSession` | Only the session that activated the object may call it |

Swift also has `.allowAll` and `.networkOnly`; C++ has `all`.

To publish an object, give its `ObjectId` to clients: through the nameserver
(`Bind`/`Resolve`), host.json for browsers (`Rpc::add_to_host_json`), or as a
string (`ObjectId::to_string()`).

## Choosing where servant methods run

By default a servant method runs on the thread that received the call: the
transport's I/O thread, or for shared memory the thread reading the ring. That
is the lowest-latency option, and right for methods that finish in
microseconds. A method that blocks, allocates heavily or touches UI state
should run elsewhere, so the transport keeps serving other calls.

| Setup | Methods run on | Use for |
|---|---|---|
| Default | The transport thread | Cheap, latency-critical methods |
| `NeverBlockTransport` affinity | The runtime's thread pool | Methods that may block, with no thread requirement |
| Swift `dispatch: .main` | The main queue | UI servants |
| Swift `dispatch: .queue(q)` | Your serial queue | State owned by one queue |
| Swift `dispatch: .loop(executor)` | Your event-loop thread | A render loop, GLFW, epoll |
| C++ `with_dispatch_executor(ex)` | Wherever `ex.post` schedules work | Your own executor |

With any of the non-default options, the transport hands the call off and
immediately moves on. The method and its reply both run on the target.

### C++

```cpp
// Keep the transport free, run on the runtime's thread pool.
auto* offload = rpc->create_poa()
                    .with_transport_affinity(
                        nprpc::PoaPolicy::TransportAffinity::NeverBlockTransport)
                    .build();

// Or run on your own executor.
nprpc::DispatchExecutor ex;
ex.post = [](void* ctx, nprpc::DispatchExecutor::WorkFn fn, void* arg) {
  static_cast<MyQueue*>(ctx)->push([fn, arg] { fn(arg); });
};
ex.is_running_on = [](void* ctx) {
  return static_cast<MyQueue*>(ctx)->is_current_thread();
};
ex.ctx = &my_queue;

auto* poa = rpc->create_poa().with_dispatch_executor(ex).build();
```

`post` must not wait for the work to run. `is_running_on` lets the runtime
run work inline when it is already on your executor, instead of posting and
waiting for itself.

### Swift

```swift
let uiPoa = try rpc.createPoa(maxObjects: 32, dispatch: .main)

final class DashboardImpl: DashboardServant, @unchecked Sendable {
    override func updateTitle(title: String) throws {
        label.stringValue = title   // already on main
    }
}
```

`.queue` must be a **serial** queue. A shared-memory client matches replies
to requests by order, so a concurrent queue could send them back out of order.

### A thread with its own event loop (Swift)

`.main` and `.queue` cover everything GCD owns. A thread that runs its own
loop, blocking in `glfwWaitEvents` or `epoll_wait` and owning state only it
may touch, needs `.loop` with a `PoaExecutor`:

```swift
final class RenderLoop: PoaExecutor {
    func post(_ work: @escaping @Sendable () -> Void) {
        lock.lock(); pending.append(work); lock.unlock()
        wakeTheLoop()                      // e.g. glfwPostEmptyEvent()
    }
    var isRunningOnExecutor: Bool { Thread.current === loopThread }

    func drain() {                         // call once per loop iteration
        lock.lock(); let work = pending; pending.removeAll(); lock.unlock()
        for item in work { item() }
    }
}

let poa = try rpc.createPoa(maxObjects: 8, dispatch: .loop(renderLoop))
```

Three rules, in order of how badly they fail when broken:

1. **`post` must wake the loop.** Nothing else will tell it a call is waiting.
   An idle loop would otherwise never run the servant.
2. **Drain in FIFO order**, for the same reason `.queue` must be serial.
3. **`isRunningOnExecutor` must be accurate.** It stops the runtime from
   posting to the loop it is already on and then waiting for a drain that
   cannot start until it returns.

## Avoiding deadlocks

Do not make a blocking NPRPC call from a servant running on a queue if that
call needs the same queue to complete. For example, a `.main` servant must not
block main waiting for a reply that is also delivered on main. Use the `async`
form of the call instead.

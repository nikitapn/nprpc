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
immediately moves on. The method and its reply both run on the target. This
holds for TCP, WebSocket, WebTransport, QUIC and shared memory, with two
exceptions: HTTP RPC, and the raw `use_epoll_tcp` and `use_uring_tcp` servers.
Those keep their thread until the method returns, and run a
`NeverBlockTransport` method on it.

One session still runs one message at a time. While a handed-off call is
running, later messages from that client wait behind it, whichever POA they are
for, so a client sees its calls complete in order. Other clients aren't affected.

A handed-off method can notice that its client stopped caring, and stop early. The
token stops when the client cancels the call or disconnects. A call on the default
POA can't be told, because its transport isn't reading.

```cpp
std::vector<Row> Report::Build() {
  const auto stop = nprpc::get_call_context().stop_token;
  std::vector<Row> rows;
  while (!stop.stop_requested() && more_rows())
    rows.push_back(next_row());
  if (stop.stop_requested()) throw nprpc::OperationCancelled{};
  return rows;
}
```

```swift
override func build() throws -> [Row] {
    var rows: [Row] = []
    while !currentCallIsCancelled && moreRows() { rows.append(nextRow()) }
    return rows
}
```

A cancelled call that hasn't started yet is skipped. A client that disconnects
stops the running call, but its queued calls still run, with their tokens already
stopped, because some may be one-way.

### Methods that wait on something else

A method marked `[coroutine]` in the IDL doesn't hold anything up while it waits.
Its connection goes on to the next call, and its reply goes out whenever it's ready,
so one client's calls can finish in any order. Clients don't change.

```
interface Report {
  [coroutine] string Build(id: u32) raises (NotFound);
}
```

```cpp
nprpc::Task<std::string> ReportImpl::Build(std::stop_token stop, uint32_t id) {
  auto rows = co_await database->QueryAsync(id, stop);
  co_return render(rows);
}
```

```swift
override func build(id: UInt32) async throws -> String {
    render(try await database.query(id: id))
}
```

The method keeps the POA's choice of thread: after each `co_await` it's back on its
executor, or on the thread pool or the session's executor if the POA has none. In
Swift that's only for a POA with a `dispatch:`; otherwise a method runs on the
concurrency pool. Other calls can run on that thread while it waits, so state read
before an await may have changed after it.

`stop` fires if the client cancels or disconnects, on any POA. In Swift, the task is
cancelled: use `Task.isCancelled`. Arguments stay valid for the whole call.
`get_context()` and `get_call_context()` only work until the first `co_await`.

`[coroutine]` methods can't have out parameters or file descriptors. A session allows
1024 at once; beyond that, a call fails with `ExceptionCommFailure`. HTTP RPC and the
raw epoll and io_uring servers wait for the method to finish before replying.

The runtime's thread pool also runs the transports' I/O, so size
`start_thread_pool` for the number of methods you expect to block at once, plus
some to spare.

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

`.queue` must be a **serial** queue. A session only runs one call at a time, but
on a concurrent queue, calls from different clients would run at once.

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
2. **Drain in FIFO order**, so each client's calls run in the order it sent them.
3. **`isRunningOnExecutor` must be accurate.** It stops the runtime from
   posting to the loop it is already on and then waiting for a drain that
   cannot start until it returns.

## Avoiding deadlocks

Do not make a blocking NPRPC call from a servant running on a queue if that
call needs the same queue to complete. For example, a `.main` servant must not
block main waiting for a reply that is also delivered on main. Use the `async`
form of the call instead.

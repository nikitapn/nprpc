# Calls and cancellation

Every call involves two separate choices: how the client waits for the reply, and
how the servant runs. The two sides don't know what the other picked, and the wire
format is the same for all of them, so any client style works with any servant style.

The examples below use this interface:

```
interface Report {
  string Summary(id: u32) raises (NotFound);
  string Build(id: u32) raises (NotFound);
  [coroutine] string Fetch(id: u32) raises (NotFound);
}
```

## Quick guide

| The method... | Declare it | Run it on |
|---|---|---|
| Finishes in microseconds | Plain | The default POA |
| Does long CPU work, or needs a particular thread | Plain | A POA that hands off, checking the token |
| Waits on I/O or another service | `[coroutine]` | Any POA |

Cancelling always frees the client. Only the servant style decides whether the
server stops working as well.

## Client: how to wait for the reply

| Style | Languages | Use for | Can cancel |
|---|---|---|---|
| Blocking | C++ | Simple code, lowest overhead | No |
| Suspending | C++, Swift, TypeScript | Many calls at once without a thread for each | C++ and Swift |

### Blocking

Only C++ has this style. The thread waits until the reply arrives or the call times
out.

```cpp
std::string text = report->Summary(7);
```

### Suspending

C++ generates an `Async` version of each method, except stream and `[unreliable]`
ones. Swift and TypeScript client methods are always `async`.

```cpp
nprpc::Task<std::string> both(nprpc::ObjectPtr<Report>& report) {
  auto first = co_await report->BuildAsync(7);
  auto second = co_await report->FetchAsync(8);
  co_return first + second;
}
```

```swift
async let first = report.build(id: 7)
async let second = report.fetch(id: 8)
let text = try await first + second
```

### Cancelling a call

In C++, pass a `std::stop_token` to the `Async` method. The call ends with
`nprpc::OperationCancelled` as soon as you request a stop.

```cpp
std::stop_source stop;
auto task = report->FetchAsync(7, stop.get_token());

stop.request_stop();

try {
  auto text = task.get();
} catch (const nprpc::OperationCancelled&) {
  // The call was cancelled.
}
```

In Swift, cancel the task. The call throws `CancellationError`.

```swift
let task = Task { try await report.fetch(id: 7) }

task.cancel()

do {
  _ = try await task.value
} catch is CancellationError {
  // The call was cancelled.
}
```

A TypeScript client can't send a cancel yet. A call that times out doesn't send one
either, so the servant isn't told.

## Servant: how the method runs

| Style | Lets you | Told about a cancel | Holds up that client's other calls |
|---|---|---|---|
| Plain, on the default POA | Get the lowest latency | No | Yes |
| Plain, on a POA that hands off | Do long work without blocking the transport, or stay on one thread | Yes | Yes |
| `[coroutine]` | Wait without holding anything up | Yes | No |

[POA.md](POA.md#choosing-where-servant-methods-run) lists the POA setups that hand off.

### Plain, on the default POA

The method runs on the transport thread, which can't read anything else until the
method returns. A cancel therefore arrives too late to matter, and there's no token
worth checking. Keep these methods short.

```cpp
std::string ReportImpl::Summary(uint32_t id) {
  return summaries.at(id);
}
```

### Plain, on a POA that hands off

The transport keeps reading while the method runs, so it sees the cancel and stops
the token. The method has to check the token itself.

```cpp
std::string ReportImpl::Build(uint32_t id) {
  const auto stop = nprpc::get_call_context().stop_token;
  std::string out;
  for (auto& part : parts(id)) {
    if (stop.stop_requested()) throw nprpc::OperationCancelled{};
    out += render(part);
  }
  return out;
}
```

```swift
override func build(id: UInt32) throws -> String {
    var out = ""
    for part in parts(id) {
        if currentCallIsCancelled { break }
        out += render(part)
    }
    return out
}
```

Throwing `OperationCancelled` is how a C++ method skips sending its result. A Swift
method can't do that, so it returns early, and the client ignores whatever it sends.

Later calls from the same client wait until this method returns, even after the
client has cancelled it. A client that cancels and retries still waits for the
original call to end, so a method that doesn't check the token delays the retry.

### `[coroutine]`

The method suspends while it waits. The session goes on to the next call, and the
reply goes out when it's ready, so one client's calls can finish in any order.

```cpp
nprpc::Task<std::string> ReportImpl::Fetch(std::stop_token stop, uint32_t id) {
  auto rows = co_await backend->BuildAsync(id, stop);
  co_return render(rows);
}
```

```swift
override func fetch(id: UInt32) async throws -> String {
    render(try await backend.build(id: id))
}
```

In C++, passing `stop` along cancels the nested call too. In Swift, the nested call
is cancelled with the task. Don't use blocking calls inside these methods: they
hold up the thread that other calls need.

[POA.md](POA.md#methods-that-wait-on-something-else) covers threads, restrictions and
limits.

## What a cancel does on the server

The client has already moved on in every case here. It ignores any reply that
arrives later.

| Where the call is | What happens | What goes back |
|---|---|---|
| Queued, not started | The servant never runs | `Error_Cancelled` |
| Handed off, throws `OperationCancelled` | The token stops, and the method ends early | `Error_Cancelled` |
| Handed off, ignores the token | The token stops, and the method runs to the end | The full reply |
| Running on the default POA | Nothing, because the method returns before the transport reads the cancel | The full reply |
| `[coroutine]` | `stop` fires, or the Swift task is cancelled | Whatever the method returns or throws |

The full reply still crosses the wire, so a large one costs the same as if the call
hadn't been cancelled.

## When the client disconnects

A disconnect stops the running call, and any `[coroutine]` calls, the same way a
cancel does. Calls still queued do run, with their tokens already stopped, because
some may be one-way. A method on the default POA runs to the end.

## Transports

Handoff and `[coroutine]` work over TCP, WebSocket, WebTransport, QUIC and shared
memory. HTTP RPC and the raw `use_epoll_tcp` and `use_uring_tcp` servers keep their
thread until the method finishes, so a call on them can't be cancelled while it runs.

# Streams

A stream method sends a sequence of values instead of a single reply, for
example a live feed, a file upload, or a chat. Declare it in IDL, and npidl
generates a reader or writer on each side.

## Declaring streams

```npidl
interface Feed {
  /// Server to client: the server produces, the caller iterates.
  stream<Item> Watch(limit: u32);

  /// Client to server: the caller produces, the servant iterates.
  void Upload(tag: string, items: client_stream<Item>);

  /// Both ways: the client sends Item, the server sends Reply.
  bidi_stream<Item, Reply> Echo(prefix: string);

  /// Best effort: over native QUIC, chunks may be dropped, not retransmitted.
  [unreliable] stream<u8> Samples(rate: u32);
}
```

| Kind | IDL | Caller gets | Servant gets |
|---|---|---|---|
| Server | `stream<T> M(...)` (also `server_stream<T>`) | a reader | a writer |
| Client | `void M(..., name: client_stream<T>)` | a writer | a reader |
| Bidi | `bidi_stream<ClientToServer, ServerToClient> M(...)` | a writer and a reader | a reader and a writer |

Element types can be any IDL type: plain values, strings, vectors, arrays and
messages. `stream<direct T>` hands C++ callers the received buffer instead of
a decoded copy.

## Using them

### C++

The caller gets `nprpc::StreamReader<T>` and `nprpc::StreamWriter<T>`:

```cpp
// Server stream: range-for blocks for each item.
for (auto& item : feed->Watch(10))
  show(item);

// Client stream: the stream parameter becomes the return value.
auto upload = feed->Upload("batch-1");
for (auto& item : items)
  upload.write(item);
upload.close();

// Bidi stream.
auto [writer, reader] = feed->Echo(">");
writer.write(Item{1, "a"});
writer.close();
for (auto& reply : reader)
  show(reply);
```

Inside a coroutine, `while (auto item = co_await reader)` waits without
blocking a thread.

A servant implements each stream method as a coroutine. A server stream
`co_yield`s its items, and the runtime only resumes it once the reader has
credit, so a slow client slows the producer down:

```cpp
nprpc::StreamWriter<Item> Watch(uint32_t limit) override {
  for (uint32_t i = 0; i < limit; ++i)
    co_yield Item{i, "item"};
}

nprpc::Task<> Upload(std::string tag, nprpc::StreamReader<Item> items) override {
  while (auto item = co_await items)
    store(tag, *item);
}

nprpc::Task<> Echo(std::string prefix,
                   nprpc::BidiStream<Item, Reply> stream) override {
  while (auto item = co_await stream.reader)
    stream.writer.write(Reply{prefix + item->name});
  stream.writer.close();
}
```

### Swift

```swift
// Server stream.
for try await item in try feed.watch(limit: 10) {
    show(item)
}

// Client stream.
let upload = try feed.upload(tag: "batch-1")
for item in items {
    try await upload.write(item)   // waits for the servant's credits
}
upload.close()

// Bidi stream.
let echo = try feed.echo(prefix: ">")
try await echo.writer.write(Item(id: 1, name: "a"))
echo.writer.close()
for try await reply in echo.reader {
    show(reply)
}
```

On the servant side a server stream returns an `AsyncStream<T>`. Client and
bidi streams are `async` methods that receive an `NPRPCStreamReader` or an
`NPRPCBidiStream`, whose type parameters are in (write, read) order from the
servant's point of view:

```swift
override func watch(limit: UInt32) -> AsyncStream<Item> {
    AsyncStream { continuation in
        for i in 0..<limit { continuation.yield(Item(id: i, name: "item")) }
        continuation.finish()
    }
}

override func echo(prefix: String, stream: NPRPCBidiStream<Reply, Item>) async {
    do {
        for try await item in stream.reader {
            try await stream.writer.write(Reply(text: prefix + item.name))
        }
        stream.writer.close()
    } catch {
        stream.writer.abort()
    }
}
```

### TypeScript

Proxy methods resolve to `NPRPC.StreamReader`, `NPRPC.StreamWriter` or
`NPRPC.BidiStream`:

```ts
for await (const item of await feed.Watch(10)) show(item);

const upload = await feed.Upload('batch-1');
for (const item of items) await upload.write(item);
await upload.close();

const echo = await feed.Echo('>');
await echo.writer.write({ id: 1, name: 'a' });
await echo.writer.close();
for await (const reply of echo.reader) show(reply);
```

A TypeScript servant's server-stream method returns any iterable or async
iterable, and client and bidi methods receive the reader or `BidiStream` as a
parameter.

## Ending a stream

| Action | Effect on the other side |
|---|---|
| Writer `close()` | The reader's loop ends normally. |
| Writer `abort()` | The reader's loop throws. |
| Reader `cancel()`, or dropping the reader | The writer is told to stop: writes fail and a C++ producer coroutine is destroyed. |
| Connection lost | Both ends are cancelled. C++ servant coroutines waiting on a reader are resumed with an error, so their cleanup runs. |

If a server-stream method raises a declared exception before its first item,
the caller gets that exception from the call itself. Once items are flowing, a
failure ends the stream and the reader throws.

## Flow control

Streams are paced per chunk. When a caller opens a stream it reads from, its
reader advertises a window of 32 chunks; in the upload direction the window is
8. The writer sends until the window is used up, then waits. The reader
grants more credit in batches of half the window as it consumes. This is
automatic on every transport: a producer can never flood a consumer that has
stopped reading.

A `write()` that cannot proceed behaves differently per language:

- **C++ `write()`** returns `false` when the transport refused the chunk, for
  example a shared-memory ring the consumer has stopped draining. The stream
  is closed at that point, because a reliable stream cannot skip a chunk.
- **Swift `write` (`async`) and TypeScript `write`** wait for credit instead,
  and throw once the stream is closed.

## Transports

Streams work over every transport. Over native QUIC and WebTransport each
stream gets its own QUIC stream, so a slow stream does not delay other calls.
On the other transports, streams share the connection with ordinary calls.

`[unreliable]` streams are sent as QUIC datagrams over native QUIC, and fall
back to reliable delivery on every other transport.

For the wire protocol and the WebTransport stream layout, see
[internals/WEBTRANSPORT_STREAMS.md](internals/WEBTRANSPORT_STREAMS.md).

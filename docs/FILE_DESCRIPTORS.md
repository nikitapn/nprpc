# File Descriptors

A method can hand an open file to the process on the other end: a pipe, a
socket, a `memfd`, a DMA-BUF holding a video frame. Declare the parameter or
result as `fd`, and the receiver gets its own descriptor for the same open
file, the way `SCM_RIGHTS` on a Unix socket would give it one.

Only the shared-memory transport can do this, and only on Linux. Bytes can be
copied between processes, but an open file can only be handed over by the
kernel.

## Declaring fd

```npidl
interface Frames {
  /// The servant reads (or maps) what the caller passes.
  void Submit(frame: fd, width: u32, height: u32);

  /// A descriptor as the result.
  fd Acquire(id: u64);

  /// Or as an out parameter, alongside others.
  void Describe(id: u64, size: out u64, fd: out fd);
}
```

`fd` is a whole parameter (`in` or `out`) or the whole return value, and
nothing else. npidl rejects it inside a `message` or exception, in an alias,
`vector<fd>`, `fd[N]`, an optional (`frame?: fd`), a `one of`, with `direct`,
and in streaming methods. A parameter may still be called `fd`.

## Using it

The descriptor a caller passes stays the caller's: the runtime sends a
duplicate. A descriptor that arrives belongs to whoever receives it. Every
descriptor the runtime creates is close-on-exec.

| | Caller (proxy) | Servant |
|---|---|---|
| `in` | C++ `int`, Swift `Int32`: borrowed for the call | C++ `nprpc::UniqueFd`, Swift `UniqueFD`: owned |
| `out` / return | C++ `nprpc::UniqueFd`, Swift `UniqueFD`: owned | the same, filled in by the servant |

### C++

```cpp
// Servant
class FramesImpl : public IFrames_Servant {
  void Submit(nprpc::UniqueFd frame, uint32_t width, uint32_t height) override
  {
    import_dmabuf(frame.get(), width, height);
  } // frame closes here unless moved somewhere

  nprpc::UniqueFd Acquire(uint64_t id) override
  {
    // A descriptor this servant keeps: send a duplicate.
    return nprpc::UniqueFd::dup(frames_.at(id).fd);
  }

  void Describe(uint64_t id, uint64_t& size, nprpc::UniqueFd& fd) override
  {
    size = frames_.at(id).size;
    fd = nprpc::UniqueFd(::memfd_create("frame", MFD_CLOEXEC)); // given away
  }
};

// Caller
frames->Submit(dmabuf_fd, 1920, 1080);   // dmabuf_fd stays open
nprpc::UniqueFd frame = frames->Acquire(7);
int raw = frame.release();               // now raw is ours to close
```

`nprpc::UniqueFd` closes its descriptor on destruction. `get()` reads it,
`release()` gives it up without closing, `reset(fd)` replaces it, and
`UniqueFd::dup(fd)` makes an owned close-on-exec copy of a descriptor that
stays someone else's.

### Swift

```swift
// Servant
final class FramesImpl: FramesServant, @unchecked Sendable {
    override func submit(frame: UniqueFD, width: UInt32, height: UInt32) {
        importDmabuf(frame.rawValue, width, height)
    }
    override func acquire(id: UInt64) -> UniqueFD {
        try! UniqueFD(duplicating: frames[id]!.fd)  // keep our own
    }
}

// Caller: a descriptor that came from C++ through interop goes in as is.
try await frames.submit(frame: cxxFrame.fd(), width: 1920, height: 1080)

let frame = try await frames.acquire(id: 7)
cxxRenderer.adopt(frame.release())   // hand it on; no longer ours to close
```

`UniqueFD` is a class, so it fits in the tuples that methods with several
outputs return. It closes its descriptor when the last reference goes away.
`UniqueFD(adopting:)` takes ownership of a descriptor, and
`UniqueFD(duplicating:)` copies one that stays the caller's.

## Transports

Over shared memory, a call with descriptors works like any other call. Over
TCP, WebSocket, HTTP, QUIC or WebTransport, the proxy throws before anything
is sent: `nprpc::Exception` in C++, `RuntimeError` in Swift. TypeScript gets
no `fd` methods at all, so an interface can mix them with methods a browser
calls. The browser just doesn't see the `fd` ones.

So activate objects with `fd` methods with the `shm` flag, and make sure the
caller is on the same machine. With several transports advertised, a local
client picks shared memory anyway.

## How it works

The message itself still goes through the shared-memory ring, and an `fd`
field holds an index into the descriptors attached to it. The descriptors go
over a Unix socket that each shared-memory listener opens next to its rings:
`/dev/shm/nprpc_<listener>_fd`. The socket has the same permissions as the
listener's accept ring. Each connection binds itself to a channel while it is
being set up, and calls without descriptors never touch the socket.

Because it lives next to the rings, the socket reaches any process that can
reach them. That includes a container that mounts the same directory as its
`/dev/shm`.

- **Sending.** The sender passes the descriptors as one `SCM_RIGHTS` record,
  tagged with the ring slot of its message. It sends the record before it
  commits the message, so the receiver finds the descriptors already waiting
  when it reads the message.
- **Limits.** At most 253 descriptors per message, the kernel's limit.
- **Unused descriptors.** Any the receiver does not take, because of a bad
  request, an unknown object or a dropped reply, are closed with the message.
- **Bad indices.** An index with no descriptor behind it is rejected: the
  servant is not called and the caller gets `ExceptionBadInput`.
- **Older versions.** A server from before this has no socket, and a client
  from before this never connects to one. Their channels work as before;
  only `fd` calls are refused.
- **Stale sockets.** A listener removes its socket when it stops. A process
  that dies without unwinding leaves the socket behind, and the next server
  to start removes it, the same way it removes stale rings.

Anyone who can list the directory can see channel ids, so another process
could claim a channel's side channel first. That gives it nothing it couldn't
already do through the rings, but it does take away the real client's fd
support.

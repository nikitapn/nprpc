# Streaming RPC Implementation Plan

## Overview

Add support for all three streaming modes — server-to-client, client-to-server, and bidirectional — over all NPRPC transports with C++20 coroutine support on both sides.

## Goals

1. **Efficient streaming** - Leverage zero-copy shared memory for local streams, minimize copies for remote
2. **Backpressure** - Prevent unbounded buffering, allow client to control flow
3. **Multiplexing** - Interleave stream chunks with regular RPC calls without blocking
4. **Transport agnostic** - Work seamlessly over TCP, WebSocket, HTTP, shared memory, UDP (where applicable)
5. **Clean API** - C++20 coroutines on server, async iterators/generators on client (C++, Swift, TypeScript)
6. **Lifecycle management** - Proper cleanup on cancellation, errors, disconnection
7. **All three streaming directions** - `server_stream`, `client_stream`, `bidi_stream`

## IDL Syntax

Three streaming keywords are supported. All allow regular `in` parameters that are transmitted in the `StreamInit` handshake before the stream opens — the server can validate them and raise exceptions before any data flows.

```npidl
interface FileServer {
    // ── Server → Client ──────────────────────────────────────────────────────
    // Server streams chunks until complete or cancelled.
    // Renamed from the original `stream<T>` to be explicit.
    server_stream<vector<u8>> DownloadFile(filename: in string);

    // Multiple init parameters allowed
    server_stream<LogEntry> GetLogs(since: in u64, filter: in string);

    // Exceptions allowed (raised during StreamInit, before stream opens)
    server_stream<DataChunk> GetData(id: in u32) raises (NotFound, PermissionDenied);

    // ── Client → Server ──────────────────────────────────────────────────────
    // Client streams chunks to server; server processes and returns a scalar reply.
    // Init parameters describe the upload before bytes start flowing.
    void UploadFile(fileName: in string, client_stream<vector<u8>> data);

    // ── Bidirectional ─────────────────────────────────────────────────────────
    // Both sides stream simultaneously over the same logical stream_id.
    // Classic use-cases: TCP proxying, chat, shell-over-RPC.
    bidi_stream<vector<u8>, vector<u8>> OpenTunnel(host: in string, port: in u16);
}
```

**Constraints:**
- Stream types are only valid as the primary data channel of a method, not as arbitrary `in`/`out` parameters
- `client_stream<T>` and `bidi_stream<In,Out>` replace the return type (the scalar reply is void or comes after the stream closes for `client_stream`)
- Stream element type `T` must be a valid IDL type (primitives, messages, vectors)
- Either side can cancel at any time; cancellation propagates to both ends
- Initial `in` parameters (before stream opens) support the full exception mechanism

### Backward compatibility

The original `stream<T>` keyword is kept as an alias for `server_stream<T>` so existing IDL files continue to compile unchanged.

## Phase 1: Core Protocol & IDL (Week 1)

### 1.1 Protocol Extensions

**New base types** (`idl/nprpc_base.npidl`):
```npidl
// Stream control message types
enum StreamMessageType : u8 {
    STREAM_INIT = 0,      // Client initiates stream
    STREAM_CHUNK = 1,     // Server sends data chunk
    STREAM_COMPLETE = 2,  // Server signals end of stream
    STREAM_ERROR = 3,     // Server reports error
    STREAM_CANCEL = 4     // Client requests cancellation
}

// Stream initialization request (replaces normal RPC call for streaming methods)
message StreamInit {
    msg_id: u32;          // Message ID (for correlation)
    stream_id: u64;       // Unique stream identifier (client-generated)
    interface_id: u64;    // Target interface
    object_id: u64;       // Target object
    func_idx: u32;        // Method index
    params: vector<u8>;   // Serialized input parameters
}

// Data chunk from server to client
message StreamChunk {
    stream_id: u64;       // Stream identifier
    sequence: u64;        // Monotonic sequence number (for ordering)
    data: vector<u8>;     // Serialized chunk data
    window_size: u32;     // Client's remaining buffer capacity (for backpressure)
}

// Stream completion marker
message StreamComplete {
    stream_id: u64;       // Stream identifier
    final_sequence: u64;  // Last sequence number sent
}

// Stream error
message StreamError {
    stream_id: u64;       // Stream identifier
    error_code: u32;      // Error code (exception ID or system error)
    error_data: vector<u8>; // Serialized exception data
}

// Client cancellation request
message StreamCancel {
    stream_id: u64;       // Stream identifier
}
```

**Message type registration:**
- Extend `MessageId` enum with new stream message types
- Add handling in `nprpc::impl::handle_incomming_message()`

**Files to modify:**
- `idl/nprpc_base.npidl` - add new message types
- `include/nprpc/nprpc.hpp` - extend MessageId enum
- `src/nprpc.cpp` - message type registration

### 1.2 IDL Compiler Changes

**Parser updates** (`npidl/src/parser/`):
- Add `stream<T>` type recognition in grammar
- Parse stream return types: `stream<vector<u8>> MethodName(...)`
- Validate:
  - Stream only as return type
  - Element type is valid IDL type
  - Store `is_stream` flag in method metadata

**AST extensions** (`npidl/src/ast.hpp`):
```cpp
struct Method {
    std::string name;
    std::vector<Parameter> params;
    std::optional<Type> return_type;
    bool is_stream = false;           // NEW: marks streaming method
    std::optional<Type> stream_element_type; // NEW: T in stream<T>
    std::vector<std::string> raises;
    bool is_async = false;
    bool is_unreliable = false;
};
```

**Files to modify:**
- `npidl/src/parser/grammar.hpp` - add stream<T> grammar rules
- `npidl/src/parser/parser.cpp` - parse stream return types
- `npidl/src/ast.hpp` - extend Method struct
- `npidl/src/semantic_analyzer.cpp` - validate stream usage

### 1.3 C++ Code Generation

**Generated proxy** (`npidl/src/cpp_builder.cpp`):
```cpp
// For: stream<vector<u8>> GetFile(filename: in string);
// Generate:

nprpc::StreamReader<std::vector<uint8_t>> GetFile(std::string_view filename) {
    auto stream_id = generate_stream_id(); // Unique ID
    
    // Send StreamInit message
    auto buf = prepare_stream_init(stream_id, interface_id, object_id, 
                                    func_idx, params...);
    send_message(buf);
    
    // Return reader that will receive chunks
    return nprpc::StreamReader<std::vector<uint8_t>>(
        get_session(), stream_id
    );
}
```

**Generated servant interface:**
```cpp
// Abstract base class
class TestStream_Servant {
public:
    virtual nprpc::StreamWriter<std::vector<uint8_t>> 
        GetFile(std::string_view filename) = 0;
};

// Dispatcher (in _impl class)
void dispatch_GetFile(flat_buffer& buf, SessionContext& ctx) {
    // Unmarshal parameters
    auto filename = /* unmarshal string */;
    
    // Call servant method (returns coroutine)
    auto writer = servant_->GetFile(filename);
    
    // Register stream with session's stream manager
    ctx.stream_manager().register_stream(
        stream_id, std::move(writer)
    );
}
```

**Files to modify:**
- `npidl/src/cpp_builder.cpp` - generate stream proxy/servant code
- `npidl/src/cpp_builder.hpp` - add stream generation helpers

## Phase 2: Core Runtime (Week 2)

### 2.1 Stream Manager

**New header** (`include/nprpc/impl/stream_manager.hpp`):
```cpp
namespace nprpc::impl {

// Forward declarations
template<typename T> class StreamWriter;
template<typename T> class StreamReader;

// Manages active streams per session
class StreamManager {
public:
    StreamManager(SessionContext& session);
    ~StreamManager();
    
    // Server-side: register outgoing stream
    template<typename T>
    void register_stream(uint64_t stream_id, StreamWriter<T>&& writer);
    
    // Client-side: register incoming stream
    template<typename T>
    void register_reader(uint64_t stream_id, StreamReader<T>* reader);
    
    // Handle incoming chunk
    void on_chunk_received(const StreamChunk& chunk);
    
    // Handle stream completion
    void on_stream_complete(const StreamComplete& msg);
    
    // Handle stream error
    void on_stream_error(const StreamError& msg);
    
    // Handle client cancellation
    void on_stream_cancel(const StreamCancel& msg);
    
    // Send chunk to remote peer
    void send_chunk(uint64_t stream_id, std::span<const uint8_t> data, 
                    uint64_t sequence);
    
    // Send completion marker
    void send_complete(uint64_t stream_id, uint64_t final_sequence);
    
    // Send error
    void send_error(uint64_t stream_id, uint32_t error_code, 
                    std::span<const uint8_t> error_data);
    
    // Cancel all active streams (on disconnect)
    void cancel_all();
    
private:
    SessionContext& session_;
    
    // Active outgoing streams (server-side)
    std::unordered_map<uint64_t, std::unique_ptr<StreamWriterBase>> writers_;
    
    // Active incoming streams (client-side)
    std::unordered_map<uint64_t, StreamReaderBase*> readers_;
    
    // Mutex for thread-safe access
    std::mutex mutex_;
};

} // namespace nprpc::impl
```

**Files to create:**
- `include/nprpc/impl/stream_manager.hpp` - StreamManager declaration
- `src/stream_manager.cpp` - StreamManager implementation

### 2.2 StreamWriter (Server-Side Coroutine)

**New header** (`include/nprpc/stream_writer.hpp`):
```cpp
namespace nprpc {

// Base class for type-erased storage
class StreamWriterBase {
public:
    virtual ~StreamWriterBase() = default;
    virtual void resume() = 0;
    virtual bool is_done() const = 0;
    virtual void cancel() = 0;
};

// Typed stream writer with coroutine support
template<typename T>
class StreamWriter {
public:
    // C++20 coroutine promise type
    struct promise_type {
        StreamWriter<T> get_return_object();
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        
        void return_void() {
            completed_ = true;
        }
        
        // Yielding a value sends it as a chunk
        std::suspend_always yield_value(T&& value) {
            current_value_ = std::move(value);
            has_value_ = true;
            return {};
        }
        
        void unhandled_exception() {
            exception_ = std::current_exception();
        }
        
        T current_value_;
        bool has_value_ = false;
        bool completed_ = false;
        std::exception_ptr exception_;
        uint64_t stream_id_ = 0;
        impl::StreamManager* manager_ = nullptr;
    };
    
    using handle_type = std::coroutine_handle<promise_type>;
    
    explicit StreamWriter(handle_type h) : coro_(h) {}
    
    StreamWriter(StreamWriter&& other) noexcept 
        : coro_(std::exchange(other.coro_, {})) {}
    
    ~StreamWriter() {
        if (coro_) coro_.destroy();
    }
    
    // Resume execution (called by StreamManager when ready to send)
    void resume() {
        if (coro_ && !coro_.done()) {
            coro_.resume();
        }
    }
    
    bool is_done() const {
        return !coro_ || coro_.done();
    }
    
    void cancel() {
        if (coro_ && !coro_.done()) {
            coro_.promise().completed_ = true;
            coro_.destroy();
            coro_ = {};
        }
    }
    
    // Get current yielded value
    std::optional<T> get_value() {
        if (!coro_ || !coro_.promise().has_value_) {
            return std::nullopt;
        }
        auto value = std::move(coro_.promise().current_value_);
        coro_.promise().has_value_ = false;
        return value;
    }
    
    std::exception_ptr get_exception() const {
        return coro_ ? coro_.promise().exception_ : nullptr;
    }
    
private:
    handle_type coro_;
};

// Awaitable for checking cancellation
struct CancellationPoint {
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<>) const noexcept {}
    bool await_resume() const noexcept {
        // TODO: check if stream was cancelled
        return false;
    }
};

// Helper to check if client cancelled
inline CancellationPoint check_cancelled() {
    return CancellationPoint{};
}

} // namespace nprpc
```

**Files to create:**
- `include/nprpc/stream_writer.hpp` - StreamWriter declaration
- `src/stream_writer.cpp` - StreamWriter implementation

### 2.3 StreamReader (Client-Side)

**New header** (`include/nprpc/stream_reader.hpp`):
```cpp
namespace nprpc {

// Client-side stream reader
template<typename T>
class StreamReader {
public:
    StreamReader(std::shared_ptr<SessionContext> session, uint64_t stream_id)
        : session_(std::move(session))
        , stream_id_(stream_id)
    {
        // Register with session's stream manager
        session_->stream_manager().register_reader(stream_id_, this);
    }
    
    ~StreamReader() {
        cancel();
    }
    
    // Async iterator support
    struct iterator {
        StreamReader* reader_;
        std::optional<T> current_;
        bool done_ = false;
        
        iterator(StreamReader* reader, bool done) 
            : reader_(reader), done_(done) {
            if (!done_) {
                ++(*this); // Fetch first value
            }
        }
        
        bool operator!=(const iterator& other) const {
            return done_ != other.done_;
        }
        
        iterator& operator++() {
            current_ = reader_->read_next();
            if (!current_) {
                done_ = true;
            }
            return *this;
        }
        
        T& operator*() { return *current_; }
    };
    
    iterator begin() { return iterator(this, false); }
    iterator end() { return iterator(this, true); }
    
    // C++20 coroutine support: for co_await (auto& chunk : reader)
    auto operator co_await() {
        struct Awaitable {
            StreamReader* reader;
            
            bool await_ready() const noexcept {
                return reader->has_pending_chunk();
            }
            
            void await_suspend(std::coroutine_handle<> h) {
                reader->set_resume_handle(h);
            }
            
            std::optional<T> await_resume() {
                return reader->read_next();
            }
        };
        return Awaitable{this};
    }
    
    // Blocking read (for non-coroutine usage)
    std::optional<T> read_next() {
        std::unique_lock lock(mutex_);
        
        // Wait for chunk or completion
        cv_.wait(lock, [this] {
            return !chunks_.empty() || completed_ || error_;
        });
        
        if (error_) {
            std::rethrow_exception(error_);
        }
        
        if (chunks_.empty()) {
            return std::nullopt; // Stream complete
        }
        
        auto chunk = std::move(chunks_.front());
        chunks_.pop();
        
        // Update window size and send to server
        window_size_++;
        send_window_update();
        
        return chunk;
    }
    
    // Called by StreamManager when chunk arrives
    void on_chunk_received(std::span<const uint8_t> data) {
        std::unique_lock lock(mutex_);
        
        // Deserialize chunk
        flat_buffer buf;
        buf.set_view(const_cast<uint8_t*>(data.data()), 0, data.size());
        T value = /* unmarshal T from buf */;
        
        chunks_.push(std::move(value));
        window_size_--;
        
        cv_.notify_one();
        
        if (resume_handle_) {
            resume_handle_.resume();
            resume_handle_ = {};
        }
    }
    
    void on_complete() {
        std::unique_lock lock(mutex_);
        completed_ = true;
        cv_.notify_one();
        
        if (resume_handle_) {
            resume_handle_.resume();
            resume_handle_ = {};
        }
    }
    
    void on_error(std::exception_ptr ex) {
        std::unique_lock lock(mutex_);
        error_ = ex;
        cv_.notify_one();
        
        if (resume_handle_) {
            resume_handle_.resume();
            resume_handle_ = {};
        }
    }
    
    void cancel() {
        if (cancelled_) return;
        cancelled_ = true;
        
        // Send cancel message to server
        session_->stream_manager().send_cancel(stream_id_);
    }
    
    bool is_complete() const {
        std::unique_lock lock(mutex_);
        return completed_ && chunks_.empty();
    }
    
private:
    std::shared_ptr<SessionContext> session_;
    uint64_t stream_id_;
    
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<T> chunks_;
    
    bool completed_ = false;
    bool cancelled_ = false;
    std::exception_ptr error_;
    
    // Backpressure: window-based flow control
    size_t window_size_ = 16; // Number of chunks client can buffer
    
    // Coroutine support
    std::coroutine_handle<> resume_handle_;
    
    void send_window_update();
    bool has_pending_chunk() const;
    void set_resume_handle(std::coroutine_handle<> h);
};

} // namespace nprpc
```

**Files to create:**
- `include/nprpc/stream_reader.hpp` - StreamReader declaration
- `src/stream_reader.cpp` - StreamReader implementation

### 2.4 Session Integration

**Extend SessionContext** (`include/nprpc/session_context.h`):
```cpp
class SessionContext {
public:
    // ... existing methods ...
    
    // NEW: Access stream manager
    impl::StreamManager& stream_manager() { return stream_manager_; }
    
private:
    impl::StreamManager stream_manager_{*this}; // NEW
};
```

**Extend message dispatcher** (`src/session.cpp`):
```cpp
void handle_incomming_message(flat_buffer& buf, SessionContext& ctx) {
    MessageId msg_type = /* read message type */;
    
    switch (msg_type) {
    // ... existing cases ...
    
    case MessageId::StreamInit:
        handle_stream_init(buf, ctx);
        break;
    
    case MessageId::StreamChunk:
        handle_stream_chunk(buf, ctx);
        break;
    
    case MessageId::StreamComplete:
        handle_stream_complete(buf, ctx);
        break;
    
    case MessageId::StreamError:
        handle_stream_error(buf, ctx);
        break;
    
    case MessageId::StreamCancel:
        handle_stream_cancel(buf, ctx);
        break;
    }
}
```

**Files to modify:**
- `include/nprpc/session_context.h` - add stream_manager_ member
- `src/session_context.cpp` - initialize stream manager
- `src/session.cpp` - add stream message handlers
- `include/nprpc/impl/session.hpp` - declare stream handler methods

## Phase 3: Transport Integration (Week 3)

### 3.1 Shared Memory Optimization

Streaming is where zero-copy shines. For shared memory transport:

**Direct ring buffer writes** (`src/stream_manager.cpp`):
```cpp
void StreamManager::send_chunk_shm(uint64_t stream_id, 
                                    std::span<const uint8_t> data) {
    auto* shm_channel = session_.get_shm_channel();
    if (!shm_channel) {
        send_chunk_tcp(stream_id, data); // Fallback
        return;
    }
    
    // Reserve space in ring buffer
    auto reservation = shm_channel->get_send_ring()->try_reserve_write(
        sizeof(StreamChunk) + data.size()
    );
    
    if (!reservation) {
        // Ring buffer full - apply backpressure
        // Suspend coroutine until space available
        return;
    }
    
    // Write directly to ring buffer (zero-copy)
    flat_buffer fb;
    fb.set_view(reservation.data, sizeof(StreamChunk), 
                reservation.max_size, nullptr, 
                reservation.write_idx, true);
    
    auto chunk_msg = flat::StreamChunk_Direct(fb, 0);
    chunk_msg.stream_id() = stream_id;
    chunk_msg.data(data.size());
    std::memcpy(chunk_msg.data().data(), data.data(), data.size());
    
    shm_channel->get_send_ring()->commit_write(reservation, fb.size());
}
```

**Benefits:**
- File streaming: read from disk → write directly to ring buffer (1 copy total)
- No intermediate buffering
- Natural backpressure via ring buffer capacity

**Files to modify:**
- `src/stream_manager.cpp` - add shared memory path
- `include/nprpc/impl/shared_memory_channel.hpp` - expose ring buffer access

### 3.2 TCP/WebSocket Multiplexing

**Challenge:** Don't block regular RPC calls while streaming large data

**Solution:** Chunk size limits + async writes

```cpp
void StreamManager::send_chunk_tcp(uint64_t stream_id, 
                                    std::span<const uint8_t> data) {
    constexpr size_t MAX_CHUNK_SIZE = 64 * 1024; // 64KB
    
    // Split large data into smaller chunks
    for (size_t offset = 0; offset < data.size(); offset += MAX_CHUNK_SIZE) {
        size_t chunk_size = std::min(MAX_CHUNK_SIZE, data.size() - offset);
        
        // Queue async write (doesn't block)
        session_.async_send_chunk(stream_id, 
                                  data.subspan(offset, chunk_size));
    }
}
```

**Files to modify:**
- `src/stream_manager.cpp` - implement TCP chunk sending
- `src/server_session_socket.cpp` - handle async chunk writes
- `src/server_session_websocket.cpp` - WebSocket framing for chunks

### 3.3 QUIC Transport Streaming with Native QUIC Streams

Native QUIC transport (implemented via MsQuic) now uses **true native QUIC streams** for streaming RPC. Each NPRPC stream gets its own dedicated QUIC stream, providing:

- **Zero head-of-line blocking** between independent streams
- **Parallel data transfer** without interference
- **Independent flow control** per stream

**Architecture:**
```
┌─────────────────────────────────────────────────────────────┐
│                    QUIC Connection                          │
├─────────────────────────────────────────────────────────────┤
│  Main Stream (bidirectional)     - RPC request/response     │
├─────────────────────────────────────────────────────────────┤
│  Data Stream 1 (server→client)   - stream_id=123 chunks     │
├─────────────────────────────────────────────────────────────┤
│  Data Stream 2 (server→client)   - stream_id=456 chunks     │
├─────────────────────────────────────────────────────────────┤
│  ...                                                        │
└─────────────────────────────────────────────────────────────┘
```

**Server-side implementation:**
```cpp
// QuicServerSession::send_stream_message extracts stream_id and routes to native stream
void send_stream_message(flat_buffer&& buffer) override {
    uint64_t stream_id = extract_stream_id(buffer);
    
    // Open native QUIC stream if not already open
    connection_->open_data_stream(stream_id);
    
    // Send on dedicated native stream
    connection_->send_on_stream(stream_id, buffer.data(), buffer.size());
}
```

**Client-side handling:**
```cpp
// Client accepts server-opened streams and routes data to StreamManager
void QuicConnection::handle_connection_event(QUIC_CONNECTION_EVENT* event) {
    case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED:
        // Server opened a new data stream
        accept_data_stream(event->PEER_STREAM_STARTED.Stream);
        break;
}
```

**Note:** This is for **native clients only** (C++ applications using `quic://` URLs).

**Files modified:**
- `src/quic/quic_transport.cpp` - native QUIC stream management
  - `QuicServerConnection::open_data_stream()` - opens new QUIC stream per NPRPC stream
  - `QuicServerConnection::send_on_stream()` - sends on dedicated stream
  - `QuicConnection::data_stream_callback()` - receives data from server streams
- `include/nprpc/impl/quic_transport.hpp` - stream maps and callbacks

### 3.4 Browser Streaming: WebSocket vs WebTransport

**Current browser solution: WebSocket**
- Full-duplex communication over single TCP connection
- Works in all browsers
- Already implemented in NPRPC ✅

**Future browser solution: WebTransport API**
- W3C standard for bidirectional streams over HTTP/3/QUIC
- Provides true QUIC streams to JavaScript
- Lower latency than WebSocket (no TCP head-of-line blocking)
- Not yet widely supported (Chrome/Edge only as of 2024)

```cpp
// Future: WebTransport support
void StreamManager::send_chunk_webtransport(uint64_t stream_id,
                                             std::span<const uint8_t> data) {
    // Use WebTransport's unidirectional/bidirectional streams
    webtransport_session_.send_on_stream(stream_id, data);
}
```

**Streaming transport matrix:**

| Transport    | Client Type  | True QUIC Streams | HOL Blocking | Status      |
|-------------|--------------|-------------------|--------------|-------------|
| WebSocket   | Browser/C++  | No (app-muxed)    | Yes (TCP)    | ✅ Working  |
| Native QUIC | C++ only     | Yes               | No           | ✅ Working  |
| HTTP/3      | Browser      | No (req/res)      | No           | ❌ N/A      |
| WebTransport| Browser      | Yes               | No           | 🔮 Future   |

**Files to modify (future WebTransport):**
- `src/webtransport_server.cpp` - new server implementation
- `nprpc_js/src/webtransport.ts` - browser client

### 3.5 UDP Considerations

UDP unreliable transport is **not suitable** for streaming:
- No ordering guarantees
- No reliability
- Packet loss would corrupt stream

**Decision:** Disable streaming for UDP transport (compile-time error or runtime exception)

## Phase 4: Backpressure & Flow Control (Week 3)

### 4.1 Window-Based Flow Control

**Mechanism:**
1. Client advertises buffer capacity (window_size)
2. Server tracks how many chunks sent
3. Server suspends when window full
4. Client sends window updates as it consumes chunks

**Implementation:**
```cpp
// In StreamWriter (server-side)
template<typename T>
class StreamWriter {
    // ... existing code ...
    
    // Awaitable for backpressure
    struct WaitForCapacity {
        StreamWriter* writer;
        
        bool await_ready() const noexcept {
            return writer->has_send_capacity();
        }
        
        void await_suspend(std::coroutine_handle<> h) {
            writer->set_resume_on_capacity(h);
        }
        
        void await_resume() const noexcept {}
    };
    
    WaitForCapacity wait_for_capacity() {
        return WaitForCapacity{this};
    }
    
private:
    size_t window_size_ = 16;      // Client's capacity
    size_t chunks_in_flight_ = 0;  // Unacknowledged chunks
    std::coroutine_handle<> resume_handle_;
};

// Usage in servant:
StreamWriter<std::vector<uint8_t>> GetFile(std::string_view filename) {
    std::ifstream file(filename, std::ios::binary);
    std::vector<uint8_t> chunk(64*1024);
    
    while (file.read((char*)chunk.data(), chunk.size())) {
        chunk.resize(file.gcount());
        
        co_yield chunk; // Send chunk
        
        // Suspend if client's buffer is full
        co_await wait_for_capacity();
    }
}
```

**Files to modify:**
- `include/nprpc/stream_writer.hpp` - add window tracking
- `include/nprpc/stream_reader.hpp` - send window updates
- `src/stream_manager.cpp` - handle window updates

### 4.2 Timeout Handling

**Challenge:** What if client stops reading but doesn't disconnect?

**Solution:** Timeout suspended streams after inactivity

```cpp
class StreamManager {
    // ... existing code ...
    
    // Check for stalled streams periodically
    void check_timeouts() {
        auto now = std::chrono::steady_clock::now();
        
        for (auto& [stream_id, writer] : writers_) {
            auto elapsed = now - writer->last_activity();
            
            if (elapsed > std::chrono::seconds(30)) {
                // Cancel stalled stream
                send_error(stream_id, ErrorCode::Timeout, {});
                writers_.erase(stream_id);
            }
        }
    }
};
```

**Files to modify:**
- `include/nprpc/impl/stream_manager.hpp` - add timeout checking
- `src/stream_manager.cpp` - implement timeout logic

## Phase 5: Error Handling & Edge Cases (Week 4)

### 5.1 Exception Propagation

**Server-side exception:**
```cpp
StreamWriter<Data> GetData(uint32_t id) {
    if (!validate(id)) {
        throw NotFound("ID not found");  // Propagates as StreamError
    }
    
    try {
        while (has_data()) {
            co_yield get_next_chunk();
        }
    } catch (const std::exception& e) {
        // Caught by promise.unhandled_exception()
        // Sent as StreamError to client
    }
}
```

**Client-side handling:**
```cpp
try {
    auto reader = proxy->GetData(123);
    for (auto& chunk : reader) {
        process(chunk);
    }
} catch (const NotFound& e) {
    // Exception thrown from server
} catch (const StreamError& e) {
    // Stream-level error
}
```

**Files to modify:**
- `include/nprpc/stream_writer.hpp` - exception handling in promise
- `src/stream_manager.cpp` - serialize/send exceptions
- `include/nprpc/stream_reader.hpp` - deserialize/throw exceptions

### 5.2 Disconnection Handling

**Session closes while streaming:**
```cpp
void Session::on_disconnect() {
    // Cancel all active streams
    stream_manager_.cancel_all();
    
    // ... existing disconnect logic ...
}
```

**Client-side:**
```cpp
void StreamReader::on_session_closed() {
    std::unique_lock lock(mutex_);
    error_ = std::make_exception_ptr(
        std::runtime_error("Session closed")
    );
    cv_.notify_one();
}
```

**Files to modify:**
- `src/session.cpp` - cancel streams on disconnect
- `include/nprpc/stream_reader.hpp` - handle session closure

### 5.3 Resource Cleanup

**RAII guarantees:**
- StreamWriter destructor cancels coroutine
- StreamReader destructor sends cancel message
- Session destructor cleans up all streams

**Files to modify:**
- `include/nprpc/stream_writer.hpp` - proper RAII
- `include/nprpc/stream_reader.hpp` - proper RAII
- `src/stream_manager.cpp` - cleanup logic

## Phase 6: Testing (Week 4)

### 6.1 Unit Tests

**Test cases** (`test/src/test_streaming.cpp`):
```cpp
TEST(Streaming, BasicFileTransfer) {
    // Create 1MB test file
    // Stream from server to client
    // Verify integrity
}

TEST(Streaming, Cancellation) {
    // Start stream
    // Cancel mid-transfer
    // Verify cleanup
}

TEST(Streaming, Backpressure) {
    // Slow consumer
    // Verify server suspends
    // Verify no buffer overflow
}

TEST(Streaming, Exception) {
    // Server throws exception
    // Verify client receives it
}

TEST(Streaming, Disconnect) {
    // Start stream
    // Disconnect client
    // Verify server cleans up
}

TEST(Streaming, LargeFile) {
    // Stream 1GB file
    // Verify zero-copy path (shared memory)
    // Measure throughput
}
```

**Files to create:**
- `test/src/test_streaming.cpp` - comprehensive test suite
- `test/idl/test_streaming.npidl` - test IDL definitions

### 6.2 Benchmark

**Compare with alternatives** (`benchmark/src/benchmark_streaming.cpp`):
```cpp
// gRPC server-streaming
BENCHMARK(gRPC_StreamFile);

// NPRPC streaming (shared memory)
BENCHMARK(NPRPC_StreamFile_SHM);

// NPRPC streaming (TCP)
BENCHMARK(NPRPC_StreamFile_TCP);

// Metrics:
// - Throughput (MB/s)
// - Latency (time to first byte)
// - CPU usage
// - Memory overhead
```

**Files to create:**
- `benchmark/src/benchmark_streaming.cpp`
- `benchmark/idl/streaming_benchmark.npidl`

## Phase 7: Documentation (Week 4)

### 7.1 User Guide

**Create** `docs/STREAMING_RPC.md`:
- Overview and use cases
- IDL syntax
- Server-side coroutine examples
- Client-side usage patterns
- Backpressure configuration
- Error handling best practices
- Performance tuning

### 7.2 API Reference

**Update existing docs:**
- `README.md` - add streaming feature highlight
- `docs/NPRPC_TEST_GUIDE.md` - add streaming tests
- Comments in generated code - document stream APIs

## Implementation Schedule

### Week 1: Foundation
- [ ] Day 1-2: Protocol extensions (1.1)
- [ ] Day 3-4: IDL compiler changes (1.2)
- [ ] Day 5: C++ code generation (1.3)

### Week 2: Core Runtime
- [ ] Day 6-7: StreamManager (2.1)
- [ ] Day 8-9: StreamWriter coroutines (2.2)
- [ ] Day 10-11: StreamReader (2.3)
- [ ] Day 12: Session integration (2.4)

### Week 3: Transports & Flow Control
- [ ] Day 13-14: Shared memory optimization (3.1)
- [ ] Day 15: TCP/WebSocket multiplexing (3.2)
- [ ] Day 16: HTTP/3 streaming (3.3)
- [ ] Day 17-18: Backpressure & flow control (4.1, 4.2)

### Week 4: Polish & Testing
- [ ] Day 19-20: Error handling (5.1, 5.2, 5.3)
- [ ] Day 21-22: Unit tests (6.1)
- [ ] Day 23: Benchmarks (6.2)
- [ ] Day 24: Documentation (7.1, 7.2)

## Success Criteria

- [ ] Stream 1GB file with <1% CPU overhead
- [ ] Zero-copy path verified for shared memory
- [ ] All unit tests passing
- [ ] Benchmark shows >500 MB/s for SHM transport
- [ ] Proper cleanup on all error paths
- [ ] Works across all transports (except UDP)
- [ ] Documentation complete

## Future Enhancements (Post-v1)

1. **Compression** - optional gzip/zstd for network transports
2. **Resume capability** - restart interrupted transfers
3. **Multi-stream parallel downloads** - split large files across streams

---

## Phase 8: Client Streaming & Bidirectional Streams

> **Status:** Planned. Server streaming (Phase 1–7) must be complete first.

### 8.1 New IDL keywords

| Keyword | Chunks: client→server | Chunks: server→client | Init args |
|---|---|---|---|
| `server_stream<T>` | — | ✓ | ✓ |
| `client_stream<T>` | ✓ | — | ✓ |
| `bidi_stream<In,Out>` | ✓ | ✓ | ✓ |

### 8.2 Wire protocol change

Add a `stream_kind` field to `StreamInit` (1 byte, no existing field reuse):

```npidl
message StreamInit {
  stream_id: u64;
  poa_idx: poa_idx_t;
  interface_idx: ifs_idx_t;
  object_id: oid_t;
  func_idx: fn_idx_t;
  stream_kind: u8;  // 0=server_stream, 1=client_stream, 2=bidi_stream  ← NEW
  // init params follow (serialized as normal RPC args)
}
```

Existing `StreamDataChunk` / `StreamCompletion` / `StreamError` / `StreamCancellation` messages are direction-agnostic and require **no changes** — they carry a `stream_id` and the routing logic in `StreamManager` determines who receives them.

### 8.3 C++ runtime changes

**`StreamManager`** (`include/nprpc/impl/stream_manager.hpp`):
- `StreamInfo` gains an optional `StreamReaderBase* reader` for bidi streams (server also reads incoming chunks)
- New `register_bidi_stream(stream_id, writer, reader)` method
- `on_chunk_received` checks both `readers_` map and `writers_[id].reader` (bidi server reader)

**New header `include/nprpc/bidi_stream.hpp`**:
```cpp
// Owning pair given to server-side bidi handler
template<typename TIn, typename TOut>
struct BidiStream {
    StreamReader<TIn>  reader;   // server reads what client sends
    StreamWriter<TOut> writer;   // server writes to client
};
```

**Session dispatch** (`src/session.cpp`):
- `handle_stream_init` checks `stream_kind` and calls the appropriate registration path

### 8.4 Generated C++ stubs

```cpp
// client_stream — server receives, returns scalar after stream closes
virtual nprpc::task<void> UploadFile(
    std::string_view fileName,
    nprpc::StreamReader<std::vector<uint8_t>>& stream) = 0;

// Client proxy:
nprpc::StreamWriter<std::vector<uint8_t>> UploadFile(std::string_view fileName);

// bidi_stream — both sides active simultaneously
virtual nprpc::task<void> OpenTunnel(
    std::string_view host, uint16_t port,
    nprpc::BidiStream<std::vector<uint8_t>, std::vector<uint8_t>>& stream) = 0;

// Client proxy:
std::pair<nprpc::StreamWriter<std::vector<uint8_t>>,
          nprpc::StreamReader<std::vector<uint8_t>>>
OpenTunnel(std::string_view host, uint16_t port);
```

### 8.5 TypeScript runtime changes

**New `StreamWriter<T>`** (`nprpc_js/src/stream.ts`):
```typescript
export class StreamWriter<T> {
  constructor(
    private readonly connection: Connection,
    private readonly stream_id: bigint,
    private readonly serialize: (value: T) => Uint8Array
  ) {}

  async write(chunk: T): Promise<void> { /* send StreamDataChunk */ }
  async close(): Promise<void>          { /* send StreamCompletion */ }
  async abort(code: number): Promise<void> { /* send StreamError */ }
}
```

**`Rpc.open_bidi_stream<In,Out>()`** (`nprpc_js/src/nprpc.ts`):
```typescript
public async open_bidi_stream<In, Out>(
  endpoint, poa_idx, object_id, interface_idx, func_idx,
  serialize: (v: In) => Uint8Array,
  deserialize: (d: Uint8Array) => Out
): Promise<{ writer: StreamWriter<In>; reader: StreamReader<Out> }>
```

### 8.6 Swift runtime changes

- `StreamWriter<T>` class wrapping `AsyncStream.Continuation` — write pumps `send_chunk` on the session
- `BidiStream<In, Out>` struct pairing reader + writer
- Update `Poa.swift` dispatch to pass `BidiStream` for bidi methods
- npidl Swift codegen for `bidi_stream<>` and `client_stream<>` methods

### 8.7 npidl compiler changes

- Parse `client_stream<T>` and `bidi_stream<In,Out>` in grammar
- Extend `Method` AST node: `stream_kind: StreamKind` enum (`Server | Client | Bidi`), `stream_in_type`, `stream_out_type`
- C++ codegen: emit `BidiStream<In,Out>` parameter for bidi, `StreamReader<T>` for client_stream
- TS codegen: emit `open_bidi_stream(...)` / `open_client_stream(...)` calls
- Swift codegen: emit `StreamWriter<T>` / `BidiStream<In,Out>` return types
- Keep `stream<T>` as alias for `server_stream<T>`

### 8.8 Motivating real-world example

The `nscalc/idl/proxy.npidl` SOCKS5 proxy currently uses a `SessionCallbacks` reverse-servant pattern (register a callback object, get `OnDataReceived` called back). With bidi streams this collapses to:

```npidl
// Before: 3 interfaces, session_id coordination, reverse servant
// After:
interface User {
  bidi_stream<vector<u8>, vector<u8>> OpenTunnel(host: in string, port: in u16);
}
```

Benefits:
- No `RegisterCallbacks` / `session_id` management
- Each tunnel is an independent stream object (no shared mutable map)
- Tunnel lifetime = stream lifetime (RAII)
- Exception from `EstablishTunnel` (DNS error, refused) propagates in `StreamInit` reply before any data flows

### 8.9 Execution order

```
1. IDL: add stream_kind to StreamInit, regenerate nprpc_base stubs    (30 min)
2. C++: StreamManager bidi routing (register_bidi_stream)              (1–2 h)
3. C++: BidiStream<In,Out> header + session dispatch                   (1 h)
4. C++: client-side StreamWriter (wraps send_chunk on connection)      (1 h)
5. npidl: parse new keywords, extend AST, emit C++ stubs               (2–3 h)
6. TS: StreamWriter<T> + open_bidi_stream / open_client_stream         (1 h)
7. npidl: emit TS stubs                                                (1 h)
8. Swift: StreamWriter + BidiStream + codegen                          (2–3 h)
9. Test: update proxy.npidl, write integration test                    (1–2 h)
```

All existing `server_stream` / `stream<T>` tests must continue passing throughout — the new paths are additive.

## Open Questions

1. **Default chunk size?** - 64KB seems reasonable, but should it be configurable?
2. **Window size default?** - 16 chunks (=1MB for 64KB chunks) or dynamic based on bandwidth?
3. **Stream ID generation** - client-side counter vs UUID?
4. **Timeout values** - 30 seconds for stalled streams?
5. **Max concurrent streams** - limit per session?

## Dependencies

**Requires:**
- C++20 compiler (for coroutines)
- Existing NPRPC infrastructure
- All transports (TCP, WebSocket, HTTP/3, SHM)

**Does NOT require:**
- External libraries
- Protocol breaking changes
- ABI changes (additive only)

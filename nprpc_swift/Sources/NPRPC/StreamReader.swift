// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

import Foundation

private let nprpcEmptyStreamFinalSequence = UInt64.max

private func nprpcFinalSequenceForSentChunks(_ sentChunkCount: UInt64) -> UInt64 {
    sentChunkCount == 0 ? nprpcEmptyStreamFinalSequence : sentChunkCount - 1
}

private class StreamReaderBridgeBase {
    func onChunk(data: UnsafeRawPointer, size: Int) {}
    func onComplete() {}
    func onError(errorCode: UInt32) {}
}

private final class StreamReaderBridge<T: Sendable>: StreamReaderBridgeBase {
    private let reader: NPRPCStreamReader<T>

    init(reader: NPRPCStreamReader<T>) {
        self.reader = reader
    }

    override func onChunk(data: UnsafeRawPointer, size: Int) {
        reader.onChunkReceived(data: data, size: size)
    }

    override func onComplete() {
        reader.onComplete()
    }

    override func onError(errorCode: UInt32) {
        reader.onError(RuntimeError(message: "Stream error: \(errorCode)"))
    }
}

internal let nprpcStreamReaderOnChunk: @convention(c) (UnsafeMutableRawPointer?, UnsafeRawPointer?, UInt32) -> Void = {
    context, dataPtr, size in
    guard let context = context, let dataPtr = dataPtr else { return }
    let bridge = Unmanaged<StreamReaderBridgeBase>.fromOpaque(context).takeUnretainedValue()
    bridge.onChunk(data: dataPtr, size: Int(size))
}

internal let nprpcStreamReaderOnComplete: @convention(c) (UnsafeMutableRawPointer?) -> Void = {
    context in
    guard let context = context else { return }
    let bridge = Unmanaged<StreamReaderBridgeBase>.fromOpaque(context).takeUnretainedValue()
    bridge.onComplete()
}

internal let nprpcStreamReaderOnError: @convention(c) (UnsafeMutableRawPointer?, UInt32) -> Void = {
    context, errorCode in
    guard let context = context else { return }
    let bridge = Unmanaged<StreamReaderBridgeBase>.fromOpaque(context).takeUnretainedValue()
    bridge.onError(errorCode: errorCode)
}

internal let nprpcStreamReaderOnDestroy: @convention(c) (UnsafeMutableRawPointer?) -> Void = {
    context in
    guard let context = context else { return }
    Unmanaged<StreamReaderBridgeBase>.fromOpaque(context).release()
}

/// Client-side stream reader wrapper
/// Wraps AsyncThrowingStream to receive chunks from server
public class NPRPCStreamReader<T: Sendable>: @unchecked Sendable, AsyncSequence {
    public typealias Element = T

    private let streamId: UInt64
    private let stream: AsyncThrowingStream<T, Error>
    private let continuation: AsyncThrowingStream<T, Error>.Continuation
    private var buffer: FlatBuffer?
    private let deserializer: (UnsafeRawPointer, Int) -> T
    // Optional callback invoked after each yielded chunk to send a window
    // update back to the producer.  Set by createObjectStreamReader /
    // createStreamManagerReader via the windowUpdateFn parameter.
    internal var windowUpdateFn: (() -> Void)?
    // Set via retainStreamManager() by createStreamManagerReader — nil for
    // createObjectStreamReader, which doesn't have a stream_manager pointer
    // to retain (window updates are already skipped on that path).
    private var streamManagerHandle: UnsafeMutableRawPointer?
    /// Fired once when the consumer first enters `next()` (before awaiting a
    /// chunk).  Used by stream-init exception probing on the servant side.
    public var onFirstAccess: (@Sendable () -> Void)?
    private var firstAccessFired = false
    private let firstAccessLock = NSLock()

    public init(streamId: UInt64, buffer: FlatBuffer, deserializer: @escaping (UnsafeRawPointer, Int) -> T) {
        self.streamId = streamId
        self.buffer = buffer
        self.deserializer = deserializer

        var cont: AsyncThrowingStream<T, Error>.Continuation?
        self.stream = AsyncThrowingStream { continuation in
            cont = continuation
        }
        self.continuation = cont!
    }

    /// Get the underlying async stream for iteration
    public var asyncStream: AsyncThrowingStream<T, Error> {
        return stream
    }

    public func makeAsyncIterator() -> WindowedIterator {
        WindowedIterator(
            base: stream.makeAsyncIterator(),
            windowUpdateFn: windowUpdateFn,
            onFirstAccess: { [weak self] in self?.fireFirstAccess() }
        )
    }

    private func fireFirstAccess() {
        firstAccessLock.lock()
        defer { firstAccessLock.unlock() }
        guard !firstAccessFired else { return }
        firstAccessFired = true
        onFirstAccess?()
    }

    /// Custom iterator that sends the window update *after* consuming a chunk,
    /// i.e. after `base.next()` returns the value.  This is the correct
    /// backpressure pattern: credit for chunk N is granted once Swift has
    /// received it, not speculatively before requesting N+1.
    /// Calling `windowUpdateFn` before the `await` on `base.next()` — even
    /// when the buffer already holds data — created a suspension-point race on
    /// single-core hosts that caused the iterator to miss the final chunk.
    public struct WindowedIterator: AsyncIteratorProtocol {
        var base: AsyncThrowingStream<T, Error>.AsyncIterator
        let windowUpdateFn: (() -> Void)?
        let onFirstAccess: (() -> Void)?
        private var first = true

        init(
            base: AsyncThrowingStream<T, Error>.AsyncIterator,
            windowUpdateFn: (() -> Void)?,
            onFirstAccess: (() -> Void)?
        ) {
            self.base = base
            self.windowUpdateFn = windowUpdateFn
            self.onFirstAccess = onFirstAccess
        }

        public mutating func next() async throws -> T? {
            if first {
                first = false
                onFirstAccess?()
            }
            let value = try await base.next()
            if value != nil { windowUpdateFn?() }
            return value
        }
    }

    /// Called when a data chunk is received — just buffer it; the window update
    /// is deferred to WindowedIterator.next() so we don't grant a credit while
    /// still on the Boost.Asio strand thread.
    internal func onChunkReceived(data: UnsafeRawPointer, size: Int) {
        guard size > 0 else { return }
        let value = deserializer(data, size)
        continuation.yield(value)
    }

    /// Called when stream completes successfully
    internal func onComplete() {
        continuation.finish()
    }

    /// Called when stream encounters an error
    internal func onError(_ error: Error) {
        continuation.finish(throwing: error)
    }

    /// Cancel the stream
    public func cancel() {
        continuation.finish(throwing: CancellationError())
    }

    internal func makeBridgeContext() -> UnsafeMutableRawPointer {
        Unmanaged.passRetained(StreamReaderBridge(reader: self) as StreamReaderBridgeBase).toOpaque()
    }

    // Takes this reader's own share of the StreamManager's lifetime — see
    // nprpc_stream_manager_retain/release. Must be called at most once.
    internal func retainStreamManager(_ handle: UnsafeMutableRawPointer) {
        precondition(streamManagerHandle == nil, "retainStreamManager called twice")
        nprpc_stream_manager_retain(handle)
        streamManagerHandle = handle
    }

    deinit {
        cancel()
        if let streamManagerHandle {
            nprpc_stream_manager_release(streamManagerHandle)
        }
    }
}

/// Helper to create a byte stream reader (most common case)
public func createByteStreamReader(streamId: UInt64, buffer: FlatBuffer) -> NPRPCStreamReader<UInt8> {
    return NPRPCStreamReader(streamId: streamId, buffer: buffer) { data, count in
        // For single byte, just read the first byte
        return data.load(as: UInt8.self)
    }
}

public func marshal_stream_fundamental<T>(buffer: FlatBuffer, offset: Int, value: T) {
    let elementSize = MemoryLayout<T>.stride
    let missingBytes = offset + elementSize - buffer.size
    if missingBytes > 0 {
        buffer.prepare(missingBytes)
        buffer.commit(missingBytes)
    }
    guard let data = buffer.data else { return }
    data.storeBytes(of: value, toByteOffset: offset, as: T.self)
}

public func marshal_stream_struct<T>(
    buffer: FlatBuffer,
    offset: Int,
    rootSize: Int,
    extraCapacity: Int = 128,
    value: T,
    marshalElement: (FlatBuffer, Int, T) -> Void
) {
    let missingBytes = offset + rootSize - buffer.size
    if missingBytes > 0 {
        buffer.prepare(missingBytes + extraCapacity)
        buffer.commit(missingBytes)
    }
    marshalElement(buffer, offset, value)
}

public func marshal_stream_string(buffer: FlatBuffer, offset: Int, value: String) {
    let missingBytes = offset + 8 - buffer.size
    if missingBytes > 0 {
        buffer.prepare(missingBytes)
        buffer.commit(missingBytes)
    }
    marshal_string(buffer: buffer, offset: offset, string: value)
}

public func marshal_stream_fundamental_vector<T>(buffer: FlatBuffer, offset: Int, value: [T]) {
    let missingBytes = offset + 8 - buffer.size
    if missingBytes > 0 {
        buffer.prepare(missingBytes)
        buffer.commit(missingBytes)
    }
    marshal_fundamental_vector(buffer: buffer, offset: offset, vector: value)
}

public func unmarshal_stream_fundamental_vector<T>(data: UnsafeRawPointer, offset: Int = 0) -> [T] {
    unmarshal_fundamental_vector(buffer: data, offset: offset)
}

public func createObjectStreamReader<T: Sendable>(
    objectHandle: UnsafeMutableRawPointer,
    streamId: UInt64,
    buffer: FlatBuffer,
    unreliable: Bool = false,
    deserializer: @escaping (UnsafeRawPointer, Int) -> T
) -> NPRPCStreamReader<T> {
    let reader = NPRPCStreamReader(streamId: streamId, buffer: buffer, deserializer: deserializer)
    let context = reader.makeBridgeContext()
    nprpc_stream_register_reader(
        objectHandle,
        streamId,
        context,
        nprpcStreamReaderOnChunk,
        nprpcStreamReaderOnComplete,
        nprpcStreamReaderOnError,
        nprpcStreamReaderOnDestroy
    )
    if unreliable {
        nprpc_stream_set_reader_unreliable(objectHandle, streamId, true)
    }
    // For object-handle readers we don't yet have a StreamManager pointer;
    // window updates are currently skipped on this path.
    return reader
}

// Window a consumer advertises in StreamInit.initial_credits when opening a
// stream; refills are granted in batches of half this window.
public let defaultReaderWindow: UInt32 = 32

// Producer's starting credit pool when nothing was advertised (legacy peers).
public let kInitialWindowSize: UInt32 = 8

public func createStreamManagerReader<T: Sendable>(
    streamManager: UnsafeMutableRawPointer,
    streamId: UInt64,
    buffer: FlatBuffer,
    unreliable: Bool = false,
    producerWindow: UInt32 = kInitialWindowSize,
    deserializer: @escaping (UnsafeRawPointer, Int) -> T
) -> NPRPCStreamReader<T> {
    let reader = NPRPCStreamReader(streamId: streamId, buffer: buffer, deserializer: deserializer)
    reader.retainStreamManager(streamManager)
    let context = reader.makeBridgeContext()
    nprpc_stream_manager_register_reader(
        streamManager,
        streamId,
        context,
        nprpcStreamReaderOnChunk,
        nprpcStreamReaderOnComplete,
        nprpcStreamReaderOnError,
        nprpcStreamReaderOnDestroy
    )
    if unreliable {
        nprpc_stream_manager_set_reader_unreliable(streamManager, streamId, true)
    }
    // Watermark-batched flow control: grant credits back once per
    // grantThreshold consumed chunks instead of per chunk.  The threshold
    // must not exceed the producer's window (producerWindow) or the stream
    // deadlocks.  Liveness: if the producer stalls at zero credits, all its
    // chunks are buffered locally, so the counter must reach the threshold.
    // windowUpdateFn is invoked from the single consuming iterator, so the
    // captured counter is not accessed concurrently.
    let grantThreshold = max(1, producerWindow / 2)
    var consumedSinceGrant: UInt32 = 0
    reader.windowUpdateFn = {
        consumedSinceGrant += 1
        if consumedSinceGrant >= grantThreshold {
            nprpc_stream_manager_send_window_update(streamManager, streamId, consumedSinceGrant)
            consumedSinceGrant = 0
        }
    }
    return reader
}

public final class NPRPCStreamWriter<T: Sendable>: @unchecked Sendable {
    private let streamId: UInt64
    private let buffer: FlatBuffer
    private let serializer: (FlatBuffer, Int, T) -> Void
    private let initialPayloadCapacity: Int
    // Returns false if the chunk was dropped instead of sent (the owning
    // session had already died) — see Session::bind_self() on the C++ side.
    private let sendChunk: (FlatBuffer, UInt64) -> Bool
    private let sendComplete: (UInt64, UInt64) -> Void
    private let sendError: (UInt64, UInt32) -> Void
    private let sendCancel: ((UInt64) -> Void)?
    // When non-nil, writes go through credit-based backpressure.
    //
    // Contract: the closure MUST fully consume (copy) `buffer`'s bytes before
    // returning — either into a wire message or into an owned queue.  The
    // success callback may fire later (credit wait), but the buffer storage
    // may be reused for the next write as soon as the closure returns.
    // C++ `write_chunk_or_queue` already does this (memcpy into flat_buffer or
    // pending_writes).  Test sinks must copy if they keep data past return.
    //
    // The callback receives whether the chunk was actually sent (false if the
    // owning session had already died).
    internal let asyncSendChunk: ((FlatBuffer, UInt64, @escaping (Bool) -> Void) -> Void)?
    private var sequence: UInt64 = 0
    private var closed = false
    private let lock = NSLock()
    // Set via retainStreamManager() by createStreamManagerWriter.
    private var streamManagerHandle: UnsafeMutableRawPointer?

    internal init(
        streamId: UInt64,
        buffer: FlatBuffer = FlatBuffer(),
        initialPayloadCapacity: Int,
        serializer: @escaping (FlatBuffer, Int, T) -> Void,
        sendChunk: @escaping (FlatBuffer, UInt64) -> Bool,
        sendComplete: @escaping (UInt64, UInt64) -> Void,
        sendError: @escaping (UInt64, UInt32) -> Void,
        sendCancel: ((UInt64) -> Void)? = nil,
        asyncSendChunk: ((FlatBuffer, UInt64, @escaping (Bool) -> Void) -> Void)? = nil
    ) {
        self.streamId = streamId
        self.buffer = buffer
        self.initialPayloadCapacity = max(initialPayloadCapacity, 1)
        self.serializer = serializer
        self.sendChunk = sendChunk
        self.sendComplete = sendComplete
        self.sendError = sendError
        self.sendCancel = sendCancel
        self.asyncSendChunk = asyncSendChunk
    }

    /// Synchronous write (used when no backpressure hook is configured).
    /// Throws if the writer is already closed, or if this write discovers
    /// the peer's session has died — in the latter case the writer is also
    /// marked closed, since the stream can't recover.
    public func write(_ value: T) throws {
        try performSyncWrite(value)
    }

    // Named distinctly from write(_:)/write(_:) async so that the async
    // overload's synchronous fallback path (below) can call it unambiguously
    // — `write(value)` inside `write(_ value: T) async` resolves back to the
    // async overload itself even without `await` (Swift picks the async
    // candidate when one is in scope), which would recurse forever instead
    // of falling back.
    private func performSyncWrite(_ value: T) throws {
        lock.lock()
        defer { lock.unlock() }
        guard !closed else {
            throw RuntimeError(message: "NPRPCStreamWriter: stream is closed")
        }

        buffer.consume(buffer.size)
        buffer.prepare(initialPayloadCapacity)
        serializer(buffer, 0, value)

        let sent = sendChunk(buffer, sequence)
        sequence += 1
        if !sent {
            closed = true
            throw RuntimeError(message: "NPRPCStreamWriter: write failed, the stream's session is no longer alive")
        }
    }

    private func markClosed() {
        lock.lock()
        closed = true
        lock.unlock()
    }

    /// Async write that respects credit-based backpressure when available.
    /// Falls back to synchronous write if no async hook was configured.
    /// Throws under the same conditions as the synchronous write(_:).
    ///
    /// Serializes into the writer's reusable buffer and hands it to
    /// `asyncSendChunk` under the lock (no per-chunk snapshot copy).  The
    /// C++ path copies the payload before returning; we then await only the
    /// credit/send completion callback.
    public func write(_ value: T) async throws {
        guard let asyncSend = asyncSendChunk else {
            try performSyncWrite(value)
            return
        }

        // nil => writer already closed; otherwise send success flag.
        let outcome: Bool? = await withCheckedContinuation { (cont: CheckedContinuation<Bool?, Never>) in
            lock.lock()
            if closed {
                lock.unlock()
                cont.resume(returning: nil)
                return
            }

            buffer.consume(buffer.size)
            buffer.prepare(initialPayloadCapacity)
            serializer(buffer, 0, value)
            let seq = sequence
            sequence += 1

            // Invoke under lock so concurrent writers cannot overwrite `buffer`
            // until the sink has copied the bytes.  Callback may fire sync
            // (credits available) or later (parked on window update); resume
            // is not re-entrant into this task while we still hold the lock.
            asyncSend(buffer, seq) { ok in
                cont.resume(returning: ok)
            }
            lock.unlock()
        }

        guard let sent = outcome else {
            throw RuntimeError(message: "NPRPCStreamWriter: stream is closed")
        }
        if !sent {
            markClosed()
            throw RuntimeError(message: "NPRPCStreamWriter: write failed, the stream's session is no longer alive")
        }
    }

    public func close() {
        lock.lock()
        defer { lock.unlock() }
        guard !closed else { return }
        sendComplete(streamId, nprpcFinalSequenceForSentChunks(sequence))
        closed = true
    }

    public func abort(errorCode: UInt32 = 1) {
        lock.lock()
        defer { lock.unlock() }
        guard !closed else { return }
        sendError(streamId, errorCode)
        closed = true
    }

    public func cancel() {
        lock.lock()
        defer { lock.unlock() }
        guard !closed else { return }
        sendCancel?(streamId)
        closed = true
    }

    // Takes this writer's own share of the StreamManager's lifetime — see
    // nprpc_stream_manager_retain/release. Must be called at most once.
    internal func retainStreamManager(_ handle: UnsafeMutableRawPointer) {
        precondition(streamManagerHandle == nil, "retainStreamManager called twice")
        nprpc_stream_manager_retain(handle)
        streamManagerHandle = handle
    }

    deinit {
        if let streamManagerHandle {
            nprpc_stream_manager_release(streamManagerHandle)
        }
    }
}

public struct NPRPCBidiStream<TWrite: Sendable, TRead: Sendable>: @unchecked Sendable {
    public let writer: NPRPCStreamWriter<TWrite>
    public let reader: NPRPCStreamReader<TRead>

    public init(writer: NPRPCStreamWriter<TWrite>, reader: NPRPCStreamReader<TRead>) {
        self.writer = writer
        self.reader = reader
    }
}

// MARK: - StreamInit exception probe (servant side)
//
// Client/bidi stream servant methods are `async throws`.  C++ can detect
// "threw before first suspend" via Task::done() + rethrow_if_exception.
// Swift has no equivalent, so we use a first-access gate on the stream
// reader: if the method throws before it ever awaits the next chunk, that
// is an *init-time* failure and must become the StreamInit Exception reply.
// If it reaches the first `for try await` / `.next()`, the stream has
// started and StreamInit already succeeded (or is about to).

/// Result of probing whether a stream servant method failed during init.
public enum StreamInitProbeResult: Sendable {
    /// Method suspended on first stream read (or completed without error).
    case started
    /// Method threw before any stream I/O — encode as StreamInit Exception.
    case initError(any Error)
}

/// Runs `body` and waits until either the first stream access (via
/// `installFirstAccess`) or an init-time throw / clean return.
///
/// Wire `installFirstAccess` to `reader.onFirstAccess = fn`.
public func nprpcProbeStreamInit(
    body: @escaping @Sendable () async throws -> Void,
    installFirstAccess: (@escaping @Sendable () -> Void) -> Void
) -> StreamInitProbeResult {
    final class Probe: @unchecked Sendable {
        private let lock = NSLock()
        private let sem = DispatchSemaphore(value: 0)
        private var done = false
        private var initError: (any Error)?

        func markStarted() {
            lock.lock()
            defer { lock.unlock() }
            guard !done else { return }
            done = true
            sem.signal()
        }

        func markInitError(_ error: any Error) {
            lock.lock()
            defer { lock.unlock() }
            guard !done else { return }
            done = true
            initError = error
            sem.signal()
        }

        func wait() -> StreamInitProbeResult {
            sem.wait()
            lock.lock()
            defer { lock.unlock() }
            if let initError { return .initError(initError) }
            return .started
        }
    }

    let probe = Probe()
    installFirstAccess { probe.markStarted() }

    Task {
        do {
            try await body()
            // Completed without ever touching the stream (or after access).
            probe.markStarted()
        } catch {
            probe.markInitError(error)
        }
    }

    return probe.wait()
}

private final class CallbackBox {
    let fn: (Bool) -> Void
    init(_ fn: @escaping (Bool) -> Void) { self.fn = fn }
}

// C trampoline: releases the retained CallbackBox and invokes fn.
private let nprpcCallbackTrampoline: @convention(c) (UnsafeMutableRawPointer?, Bool) -> Void = { ctx, success in
    guard let ctx else { return }
    Unmanaged<CallbackBox>.fromOpaque(ctx).takeRetainedValue().fn(success)
}

public func createStreamManagerWriter<T: Sendable>(
    streamManager: UnsafeMutableRawPointer,
    streamId: UInt64,
    initialPayloadCapacity: Int,
    initialCredits: UInt32 = 0,
    serializer: @escaping (FlatBuffer, Int, T) -> Void
) -> NPRPCStreamWriter<T> {
    // Register the stream with the credit-tracking system before the first
    // write.  initialCredits carries the consumer's advertised window from
    // StreamInit.initial_credits (0 = legacy default).
    nprpc_stream_manager_register_external_writer(streamManager, streamId, initialCredits)

    let writer = NPRPCStreamWriter(
        streamId: streamId,
        initialPayloadCapacity: initialPayloadCapacity,
        serializer: serializer,
        sendChunk: { buffer, sequence in
            guard let data = buffer.constData else { return true }
            return nprpc_stream_manager_send_chunk(streamManager, streamId, data, UInt32(buffer.size), sequence)
        },
        sendComplete: { streamId, finalSequence in
            nprpc_stream_manager_send_complete(streamManager, streamId, finalSequence)
        },
        sendError: { streamId, errorCode in
            nprpc_stream_manager_send_error(streamManager, streamId, errorCode, nil, 0)
        },
        sendCancel: { streamId in
            nprpc_stream_manager_send_cancel(streamManager, streamId)
        },
        asyncSendChunk: { buffer, sequence, callback in
            // C++ write_chunk_or_queue copies `data` before returning (either
            // into the wire flat_buffer or into pending_writes).  Safe for
            // NPRPCStreamWriter to reuse `buffer` after this closure returns.
            guard let data = buffer.constData else { callback(true); return }
            let boxPtr = Unmanaged.passRetained(CallbackBox(callback)).toOpaque()
            nprpc_stream_manager_write_chunk_async(
                streamManager, streamId,
                data, UInt32(buffer.size), sequence,
                boxPtr, nprpcCallbackTrampoline)
        }
    )
    writer.retainStreamManager(streamManager)
    return writer
}

public func createObjectStreamWriter<T: Sendable>(
    objectHandle: UnsafeMutableRawPointer,
    streamId: UInt64,
    initialPayloadCapacity: Int,
    serializer: @escaping (FlatBuffer, Int, T) -> Void
) throws -> NPRPCStreamWriter<T> {
    guard let streamManager = nprpc_object_get_stream_manager(objectHandle) else {
        throw RuntimeError(message: "Failed to get stream manager for object stream writer")
    }

    return createStreamManagerWriter(
        streamManager: streamManager,
        streamId: streamId,
        initialPayloadCapacity: initialPayloadCapacity,
        serializer: serializer
    )
}

public func createObjectBidiStream<TWrite: Sendable, TRead: Sendable>(
    objectHandle: UnsafeMutableRawPointer,
    streamId: UInt64,
    buffer: FlatBuffer,
    initialPayloadCapacity: Int,
    unreliable: Bool = false,
    serializer: @escaping (FlatBuffer, Int, TWrite) -> Void,
    deserializer: @escaping (UnsafeRawPointer, Int) -> TRead
) throws -> NPRPCBidiStream<TWrite, TRead> {
    let reader = createObjectStreamReader(
        objectHandle: objectHandle,
        streamId: streamId,
        buffer: buffer,
        unreliable: unreliable,
        deserializer: deserializer
    )
    let writer = try createObjectStreamWriter(
        objectHandle: objectHandle,
        streamId: streamId,
        initialPayloadCapacity: initialPayloadCapacity,
        serializer: serializer
    )
    return NPRPCBidiStream(writer: writer, reader: reader)
}

// initialCredits: consumer's advertised window for our writer direction
// (from StreamInit.initial_credits when we're the servant; 0 = legacy).
// producerWindow: the window we advertised for our reader direction (pass
// defaultReaderWindow when the stub sent it in StreamInit).
public func createStreamManagerBidiStream<TWrite: Sendable, TRead: Sendable>(
    streamManager: UnsafeMutableRawPointer,
    streamId: UInt64,
    buffer: FlatBuffer,
    initialPayloadCapacity: Int,
    unreliable: Bool = false,
    initialCredits: UInt32 = 0,
    producerWindow: UInt32 = kInitialWindowSize,
    serializer: @escaping (FlatBuffer, Int, TWrite) -> Void,
    deserializer: @escaping (UnsafeRawPointer, Int) -> TRead
) -> NPRPCBidiStream<TWrite, TRead> {
    let reader = createStreamManagerReader(
        streamManager: streamManager,
        streamId: streamId,
        buffer: buffer,
        unreliable: unreliable,
        producerWindow: producerWindow,
        deserializer: deserializer
    )
    let writer = createStreamManagerWriter(
        streamManager: streamManager,
        streamId: streamId,
        initialPayloadCapacity: initialPayloadCapacity,
        initialCredits: initialCredits,
        serializer: serializer
    )
    return NPRPCBidiStream(writer: writer, reader: reader)
}

/// Server-side stream yielder for sending data chunks to client
public class StreamYielder<T>: @unchecked Sendable {
    private let streamId: UInt64
    private let buffer: FlatBuffer
    private var sequence: UInt64 = 0
    private let serializer: (FlatBuffer, Int, T) -> Void
    private let elementSize: Int
    private let sendChunk: (FlatBuffer) -> Void
    private let sendComplete: (UInt64, UInt64) -> Void
    private var finished = false

    internal init(
        streamId: UInt64,
        buffer: FlatBuffer,
        elementSize: Int,
        serializer: @escaping (FlatBuffer, Int, T) -> Void,
        sendChunk: @escaping (FlatBuffer) -> Void,
        sendComplete: @escaping (UInt64, UInt64) -> Void
    ) {
        self.streamId = streamId
        self.buffer = buffer
        self.elementSize = elementSize
        self.serializer = serializer
        self.sendChunk = sendChunk
        self.sendComplete = sendComplete
    }

    /// Send a value to the client
    public func yield(_ value: T) {
        guard !finished else { return }

        buffer.consume(buffer.size)
        buffer.prepare(elementSize + 16)
        serializer(buffer, 0, value)

        sendChunk(buffer)
        sequence += 1
    }

    /// Mark stream as complete
    public func finish() {
        guard !finished else { return }
        finished = true
        sendComplete(streamId, nprpcFinalSequenceForSentChunks(sequence))
    }

    deinit {
        if !finished {
            finish()
        }
    }
}

/// Helper to create a byte stream yielder (most common case)
public func createByteStreamYielder(
    streamId: UInt64,
    buffer: FlatBuffer,
    sendChunk: @escaping (FlatBuffer) -> Void,
    sendComplete: @escaping (UInt64, UInt64) -> Void
) -> StreamYielder<UInt8> {
    return StreamYielder(
        streamId: streamId,
        buffer: buffer,
        elementSize: 1,
        serializer: { buf, offset, value in
            let missingBytes = offset + 1 - buf.size
            if missingBytes > 0 {
                buf.prepare(missingBytes)
                buf.commit(missingBytes)
            }
            guard let data = buf.data else { return }
            data.storeBytes(of: value, toByteOffset: offset, as: UInt8.self)
        },
        sendChunk: sendChunk,
        sendComplete: sendComplete
    )
}

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
    let bridge = Unmanaged<StreamReaderBridgeBase>.fromOpaque(context).takeRetainedValue()
    bridge.onComplete()
}

internal let nprpcStreamReaderOnError: @convention(c) (UnsafeMutableRawPointer?, UInt32) -> Void = {
    context, errorCode in
    guard let context = context else { return }
    let bridge = Unmanaged<StreamReaderBridgeBase>.fromOpaque(context).takeRetainedValue()
    bridge.onError(errorCode: errorCode)
}

/// Client-side stream reader wrapper
/// Wraps AsyncThrowingStream to receive chunks from server
public class NPRPCStreamReader<T: Sendable>: @unchecked Sendable, AsyncSequence {
    public typealias Element = T

    private let streamId: UInt64
    private var continuation: AsyncThrowingStream<T, Error>.Continuation?
    private let stream: AsyncThrowingStream<T, Error>
    private var buffer: FlatBuffer?
    private let deserializer: (UnsafeRawPointer, Int) -> T
    private let lock = NSLock()
    // Optional callback invoked after each yielded chunk to send a window
    // update back to the producer.  Set by createObjectStreamReader /
    // createStreamManagerReader via the windowUpdateFn parameter.
    internal var windowUpdateFn: (() -> Void)?
    
    public init(streamId: UInt64, buffer: FlatBuffer, deserializer: @escaping (UnsafeRawPointer, Int) -> T) {
        self.streamId = streamId
        self.buffer = buffer
        self.deserializer = deserializer
        
        var cont: AsyncThrowingStream<T, Error>.Continuation?
        self.stream = AsyncThrowingStream { continuation in
            cont = continuation
        }
        self.continuation = cont
    }
    
    /// Get the underlying async stream for iteration
    public var asyncStream: AsyncThrowingStream<T, Error> {
        return stream
    }

    public func makeAsyncIterator() -> WindowedIterator {
        WindowedIterator(base: stream.makeAsyncIterator(), windowUpdateFn: windowUpdateFn)
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

        public mutating func next() async throws -> T? {
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
        continuation?.yield(value)
    }

    /// Called when stream completes successfully
    internal func onComplete() {
        lock.lock()
        let cont = continuation
        continuation = nil
        lock.unlock()
        cont?.finish()
    }
    
    /// Called when stream encounters an error
    internal func onError(_ error: Error) {
        lock.lock()
        let cont = continuation
        continuation = nil
        lock.unlock()
        cont?.finish(throwing: error)
    }
    
    /// Cancel the stream
    public func cancel() {
        lock.lock()
        let cont = continuation
        continuation = nil
        lock.unlock()
        cont?.finish(throwing: CancellationError())
    }

    internal func makeBridgeContext() -> UnsafeMutableRawPointer {
        Unmanaged.passRetained(StreamReaderBridge(reader: self) as StreamReaderBridgeBase).toOpaque()
    }
    
    deinit {
        cancel()
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
        nprpcStreamReaderOnError
    )
    if unreliable {
        nprpc_stream_set_reader_unreliable(objectHandle, streamId, true)
    }
    // For object-handle readers we don't yet have a StreamManager pointer;
    // window updates are currently skipped on this path.
    return reader
}

public func createStreamManagerReader<T: Sendable>(
    streamManager: UnsafeMutableRawPointer,
    streamId: UInt64,
    buffer: FlatBuffer,
    unreliable: Bool = false,
    deserializer: @escaping (UnsafeRawPointer, Int) -> T
) -> NPRPCStreamReader<T> {
    let reader = NPRPCStreamReader(streamId: streamId, buffer: buffer, deserializer: deserializer)
    let context = reader.makeBridgeContext()
    nprpc_stream_manager_register_reader(
        streamManager,
        streamId,
        context,
        nprpcStreamReaderOnChunk,
        nprpcStreamReaderOnComplete,
        nprpcStreamReaderOnError
    )
    if unreliable {
        nprpc_stream_manager_set_reader_unreliable(streamManager, streamId, true)
    }
    // Send one credit back to the server for every chunk consumed.
    reader.windowUpdateFn = {
        nprpc_stream_manager_send_window_update(streamManager, streamId, 1)
    }
    return reader
}

public final class NPRPCStreamWriter<T: Sendable>: @unchecked Sendable {
    private let streamId: UInt64
    private let buffer: FlatBuffer
    private let serializer: (FlatBuffer, Int, T) -> Void
    private let initialPayloadCapacity: Int
    private let sendChunk: (FlatBuffer, UInt64) -> Void
    private let sendComplete: (UInt64, UInt64) -> Void
    private let sendError: (UInt64, UInt32) -> Void
    private let sendCancel: ((UInt64) -> Void)?
    // When non-nil, writes go through credit-based backpressure.
    // The closure serializes the value, enqueues with the C++ layer, and
    // calls the provided callback when the chunk is actually sent.
    internal let asyncSendChunk: ((FlatBuffer, UInt64, @escaping () -> Void) -> Void)?
    private var sequence: UInt64 = 0
    private var closed = false
    private let lock = NSLock()

    internal init(
        streamId: UInt64,
        buffer: FlatBuffer = FlatBuffer(),
        initialPayloadCapacity: Int,
        serializer: @escaping (FlatBuffer, Int, T) -> Void,
        sendChunk: @escaping (FlatBuffer, UInt64) -> Void,
        sendComplete: @escaping (UInt64, UInt64) -> Void,
        sendError: @escaping (UInt64, UInt32) -> Void,
        sendCancel: ((UInt64) -> Void)? = nil,
        asyncSendChunk: ((FlatBuffer, UInt64, @escaping () -> Void) -> Void)? = nil
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
    public func write(_ value: T) {
        lock.lock()
        defer { lock.unlock() }
        guard !closed else {
            return
        }

        buffer.consume(buffer.size)
        buffer.prepare(initialPayloadCapacity)
        serializer(buffer, 0, value)

        sendChunk(buffer, sequence)
        sequence += 1
    }

    // Synchronous helper: serializes `value` into a snapshot buffer and
    // advances the sequence counter.  Returns nil if the writer is closed.
    // Must not be called from an async context (uses NSLock).
    private func prepareChunk(_ value: T) -> (FlatBuffer, UInt64)? {
        lock.lock()
        defer { lock.unlock() }
        guard !closed else { return nil }
        buffer.consume(buffer.size)
        buffer.prepare(initialPayloadCapacity)
        serializer(buffer, 0, value)
        let seq = sequence
        sequence += 1
        let snapshot = FlatBuffer()
        if let src = buffer.constData, buffer.size > 0 {
            snapshot.prepare(buffer.size)
            snapshot.commit(buffer.size)
            if let dst = snapshot.data {
                dst.copyMemory(from: src, byteCount: buffer.size)
            }
        }
        return (snapshot, seq)
    }

    /// Async write that respects credit-based backpressure when available.
    /// Falls back to synchronous write if no async hook was configured.
    public func write(_ value: T) async {
        guard let asyncSend = asyncSendChunk else {
            await write(value)
            return
        }

        guard let (snapshot, seq) = prepareChunk(value) else { return }

        // Suspend until the credit gate opens and the chunk is queued.
        await withCheckedContinuation { (cont: CheckedContinuation<Void, Never>) in
            asyncSend(snapshot, seq) { cont.resume() }
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
}

public struct NPRPCBidiStream<TWrite: Sendable, TRead: Sendable>: @unchecked Sendable {
    public let writer: NPRPCStreamWriter<TWrite>
    public let reader: NPRPCStreamReader<TRead>

    public init(writer: NPRPCStreamWriter<TWrite>, reader: NPRPCStreamReader<TRead>) {
        self.writer = writer
        self.reader = reader
    }
}

private final class CallbackBox {
    let fn: () -> Void
    init(_ fn: @escaping () -> Void) { self.fn = fn }
}

// C trampoline: releases the retained CallbackBox and invokes fn.
private let nprpcCallbackTrampoline: @convention(c) (UnsafeMutableRawPointer?) -> Void = { ctx in
    guard let ctx else { return }
    Unmanaged<CallbackBox>.fromOpaque(ctx).takeRetainedValue().fn()
}

public func createStreamManagerWriter<T: Sendable>(
    streamManager: UnsafeMutableRawPointer,
    streamId: UInt64,
    initialPayloadCapacity: Int,
    serializer: @escaping (FlatBuffer, Int, T) -> Void
) -> NPRPCStreamWriter<T> {
    // Register the stream with the credit-tracking system before the first write.
    nprpc_stream_manager_register_external_writer(streamManager, streamId)

    return NPRPCStreamWriter(
        streamId: streamId,
        initialPayloadCapacity: initialPayloadCapacity,
        serializer: serializer,
        sendChunk: { buffer, sequence in
            guard let data = buffer.constData else { return }
            nprpc_stream_manager_send_chunk(streamManager, streamId, data, UInt32(buffer.size), sequence)
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
            guard let data = buffer.constData else { callback(); return }
            let boxPtr = Unmanaged.passRetained(CallbackBox(callback)).toOpaque()
            nprpc_stream_manager_write_chunk_async(
                streamManager, streamId,
                data, UInt32(buffer.size), sequence,
                boxPtr, nprpcCallbackTrampoline)
        }
    )
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

public func createStreamManagerBidiStream<TWrite: Sendable, TRead: Sendable>(
    streamManager: UnsafeMutableRawPointer,
    streamId: UInt64,
    buffer: FlatBuffer,
    initialPayloadCapacity: Int,
    unreliable: Bool = false,
    serializer: @escaping (FlatBuffer, Int, TWrite) -> Void,
    deserializer: @escaping (UnsafeRawPointer, Int) -> TRead
) -> NPRPCBidiStream<TWrite, TRead> {
    let reader = createStreamManagerReader(
        streamManager: streamManager,
        streamId: streamId,
        buffer: buffer,
        unreliable: unreliable,
        deserializer: deserializer
    )
    let writer = createStreamManagerWriter(
        streamManager: streamManager,
        streamId: streamId,
        initialPayloadCapacity: initialPayloadCapacity,
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

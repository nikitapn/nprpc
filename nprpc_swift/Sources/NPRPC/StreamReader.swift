// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

import Foundation

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

    public func makeAsyncIterator() -> AsyncThrowingStream<T, Error>.Iterator {
        stream.makeAsyncIterator()
    }
    
    /// Called when a data chunk is received from server
    internal func onChunkReceived(data: UnsafeRawPointer, size: Int) {
        guard size > 0 else {
            return
        }

        let value = deserializer(data, size)
        continuation?.yield(value)
    }
    
    /// Called when stream completes successfully
    internal func onComplete() {
        continuation?.finish()
        continuation = nil
    }
    
    /// Called when stream encounters an error
    internal func onError(_ error: Error) {
        continuation?.finish(throwing: error)
        continuation = nil
    }
    
    /// Cancel the stream
    public func cancel() {
        continuation?.finish(throwing: CancellationError())
        continuation = nil
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

public func createObjectStreamReader<T: Sendable>(
    objectHandle: UnsafeMutableRawPointer,
    streamId: UInt64,
    buffer: FlatBuffer,
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
    return reader
}

public func createStreamManagerReader<T: Sendable>(
    streamManager: UnsafeMutableRawPointer,
    streamId: UInt64,
    buffer: FlatBuffer,
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
    private var sequence: UInt64 = 0
    private var closed = false

    internal init(
        streamId: UInt64,
        buffer: FlatBuffer = FlatBuffer(),
        initialPayloadCapacity: Int,
        serializer: @escaping (FlatBuffer, Int, T) -> Void,
        sendChunk: @escaping (FlatBuffer, UInt64) -> Void,
        sendComplete: @escaping (UInt64, UInt64) -> Void,
        sendError: @escaping (UInt64, UInt32) -> Void,
        sendCancel: ((UInt64) -> Void)? = nil
    ) {
        self.streamId = streamId
        self.buffer = buffer
        self.initialPayloadCapacity = max(initialPayloadCapacity, 1)
        self.serializer = serializer
        self.sendChunk = sendChunk
        self.sendComplete = sendComplete
        self.sendError = sendError
        self.sendCancel = sendCancel
    }

    public func write(_ value: T) {
        guard !closed else {
            return
        }

        buffer.consume(buffer.size)
        buffer.prepare(initialPayloadCapacity)
        serializer(buffer, 0, value)

        sendChunk(buffer, sequence)
        sequence += 1
    }

    public func close() {
        guard !closed else { return }
        sendComplete(streamId, sequence == 0 ? 0 : sequence - 1)
        closed = true
    }

    public func abort(errorCode: UInt32 = 1) {
        guard !closed else { return }
        sendError(streamId, errorCode)
        closed = true
    }

    public func cancel() {
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

public func createStreamManagerWriter<T: Sendable>(
    streamManager: UnsafeMutableRawPointer,
    streamId: UInt64,
    initialPayloadCapacity: Int,
    serializer: @escaping (FlatBuffer, Int, T) -> Void
) -> NPRPCStreamWriter<T> {
    NPRPCStreamWriter(
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
    serializer: @escaping (FlatBuffer, Int, TWrite) -> Void,
    deserializer: @escaping (UnsafeRawPointer, Int) -> TRead
) throws -> NPRPCBidiStream<TWrite, TRead> {
    let reader = createObjectStreamReader(
        objectHandle: objectHandle,
        streamId: streamId,
        buffer: buffer,
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
    serializer: @escaping (FlatBuffer, Int, TWrite) -> Void,
    deserializer: @escaping (UnsafeRawPointer, Int) -> TRead
) -> NPRPCBidiStream<TWrite, TRead> {
    let reader = createStreamManagerReader(
        streamManager: streamManager,
        streamId: streamId,
        buffer: buffer,
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
        sendComplete(streamId, sequence)
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

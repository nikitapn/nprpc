// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

import Foundation

/// Client-side stream reader wrapper
/// Wraps AsyncThrowingStream to receive chunks from server
public class NPRPCStreamReader<T: Sendable>: @unchecked Sendable {
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
    
    /// Called when a data chunk is received from server
    internal func onChunkReceived(data: UnsafeRawPointer, size: Int) {
        // StreamChunk layout after Header (16 bytes):
        // stream_id: u64 (8 bytes) - offset 0
        // sequence: u64 (8 bytes) - offset 8
        // data: vector<u8> (8 bytes header: offset + count) - offset 16
        // window_size: u32 (4 bytes) - offset 24
        
        // Read the data vector from the chunk (offset 16 relative to chunk start)
        let dataOffset = 16
        let relativeOffset = data.load(fromByteOffset: dataOffset, as: UInt32.self)
        let count = data.load(fromByteOffset: dataOffset + 4, as: UInt32.self)
        
        if count > 0 {
            let absoluteOffset = dataOffset + Int(relativeOffset)
            let value = deserializer(data.advanced(by: absoluteOffset), Int(count))
            continuation?.yield(value)
        }
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
        
        // Build StreamDataChunk message
        // Header (16) + stream_id (8) + sequence (8) + data vector (8) + window_size (4) = 44 base
        let headerSize = 16
        let chunkFixedSize = 28  // stream_id + sequence + vector header + window_size
        let dataStartOffset = headerSize + chunkFixedSize
        
        buffer.consume(buffer.size)
        buffer.prepare(dataStartOffset + elementSize + 16)
        buffer.commit(dataStartOffset)
        
        guard let data = buffer.data else { return }
        
        // Write header
        data.storeBytes(of: impl.MessageId.streamDataChunk.rawValue, toByteOffset: 4, as: Int32.self)
        data.storeBytes(of: impl.MessageType.request.rawValue, toByteOffset: 8, as: Int32.self)
        data.storeBytes(of: UInt32(0), toByteOffset: 12, as: UInt32.self)  // request_id
        
        // Write StreamChunk fields
        data.storeBytes(of: streamId, toByteOffset: headerSize, as: UInt64.self)
        data.storeBytes(of: sequence, toByteOffset: headerSize + 8, as: UInt64.self)
        // data vector at offset headerSize + 16
        // window_size at offset headerSize + 24
        data.storeBytes(of: UInt32(0), toByteOffset: headerSize + 24, as: UInt32.self)
        
        // Serialize the element into the data vector
        serializer(buffer, headerSize + 16, value)
        
        // Update header size
        guard let finalData = buffer.data else { return }
        finalData.storeBytes(of: UInt32(buffer.size - 4), toByteOffset: 0, as: UInt32.self)
        
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
            // For a single byte, write inline (relative offset 8, count 1, then the byte)
            guard let data = buf.data else { return }
            let dataOffset = buf.size
            buf.prepare(1)
            buf.commit(1)
            guard let newData = buf.data else { return }
            newData.storeBytes(of: UInt32(dataOffset - offset), toByteOffset: offset, as: UInt32.self)
            newData.storeBytes(of: UInt32(1), toByteOffset: offset + 4, as: UInt32.self)
            newData.storeBytes(of: value, toByteOffset: dataOffset, as: UInt8.self)
        },
        sendChunk: sendChunk,
        sendComplete: sendComplete
    )
}

// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

import CNprpc

/// Swift wrapper for nprpc::flat_buffer
/// Marked as @unchecked Sendable because ownership is explicitly transferred between tasks
/// and the underlying C++ memory is safely encapsulated.
///
/// Owned buffers use a C++ per-thread free-list (`nprpc_flatbuffer_acquire` /
/// `release`) so repeated RPCs recycle heap capacity instead of `new`/`delete`
/// every call.  Safe with async: `send_receive_async` moves storage into the
/// session; the empty shell returns to the pool on deinit.
public class FlatBuffer: @unchecked Sendable {
    public var handle: UnsafeMutableRawPointer
    private var owned: Bool
    
    /// Create a FlatBuffer from the per-thread pool (Swift owns it).
    public init() {
        self.handle = nprpc_flatbuffer_acquire()
        self.owned = true
    }

    /// Explicit pool acquire (same as `init()`; kept for call sites that want
    /// the intent spelled out next to `release()`).
    public static func acquire() -> FlatBuffer {
        FlatBuffer()
    }
    
    /// Wrap an existing C++ flat_buffer
    /// - Parameters:
    ///   - wrapping: Opaque pointer to C++ flat_buffer
    ///   - owned: If true, Swift will return the buffer to the pool (or free it)
    ///     on deinit (default: false)
    internal init(wrapping: UnsafeMutableRawPointer, owned: Bool = false) {
        self.handle = wrapping
        self.owned = owned
    }
    
    deinit {
        if owned {
            nprpc_flatbuffer_release(handle)
        }
    }
    
    /// Get mutable data pointer for writing
    public var data: UnsafeMutableRawPointer? {
        return nprpc_flatbuffer_data(handle)
    }
    
    /// Get const data pointer for reading
    public var constData: UnsafeRawPointer? {
        return nprpc_flatbuffer_cdata(handle)
    }
    
    /// Get buffer size
    public var size: Int {
        return nprpc_flatbuffer_size(handle)
    }

    /// Store a scalar using a freshly fetched data pointer so callers remain safe across reallocations.
    public func storeBytes<T>(of value: T, toByteOffset offset: Int, as type: T.Type) {
        guard let data = self.data else { return }
        data.storeBytes(of: value, toByteOffset: offset, as: type)
    }
    
    /// Prepare space for writing
    public func prepare(_ n: Int) {
        nprpc_flatbuffer_prepare(handle, n)
    }
    
    /// Commit written data
    public func commit(_ n: Int) {
        nprpc_flatbuffer_commit(handle, n)
    }
    
    /// Consume data
    public func consume(_ n: Int) {
        nprpc_flatbuffer_consume(handle, n)
    }
}

/// Allocate space in buffer for vectors and strings
/// Writes relative offset and count at vectorOffset, returns absolute data offset
func _alloc(buffer: FlatBuffer, vectorOffset: Int, count: Int, elementSize: Int, align: Int) -> Int {
    if count == 0 {
        guard let data = buffer.data else {
            fatalError("Buffer data is nil")
        }
        data.storeBytes(of: UInt32(0), toByteOffset: vectorOffset, as: UInt32.self)
        data.storeBytes(of: UInt32(0), toByteOffset: vectorOffset + 4, as: UInt32.self)
        return 0
    }

    let currentOffset = buffer.size
    let alignedOffset = (currentOffset + align - 1) & ~(align - 1)

    let addedSize = count * elementSize + (alignedOffset - currentOffset)
    buffer.prepare(addedSize)
    buffer.commit(addedSize)

    // Get fresh data pointer after potential reallocation
    guard let data = buffer.data else {
        fatalError("Buffer data is nil")
    }

    // Write relative offset and count
    let relativeOffset = UInt32(alignedOffset - vectorOffset)
    data.storeBytes(of: relativeOffset, toByteOffset: vectorOffset, as: UInt32.self)
    data.storeBytes(of: UInt32(count), toByteOffset: vectorOffset + 4, as: UInt32.self)

    return alignedOffset
}

/// Allocate space in buffer for Optional values
/// Writes relative offset at flat_offset, returns absolute data offset
func _alloc1(buffer: FlatBuffer, flatOffset: Int, elementSize: Int, align: Int) -> Int {
    let currentOffset = buffer.size
    let alignedOffset = (currentOffset + align - 1) & ~(align - 1)

    let addedSize = elementSize + (alignedOffset - currentOffset)
    buffer.prepare(addedSize)
    buffer.commit(addedSize)

    // Get fresh data pointer after potential reallocation
    guard let data = buffer.data else {
        fatalError("Buffer data is nil")
    }

    // Write relative offset (from flatOffset to data location)
    let relativeOffset = UInt32(alignedOffset - flatOffset)
    data.storeBytes(of: relativeOffset, toByteOffset: flatOffset, as: UInt32.self)

    return alignedOffset
}

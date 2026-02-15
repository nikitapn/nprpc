// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

import CNprpc

/// Swift wrapper for nprpc::flat_buffer
/// Marked as @unchecked Sendable because ownership is explicitly transferred between tasks
/// and the underlying C++ memory is safely encapsulated
public class FlatBuffer: @unchecked Sendable {
    public var handle: UnsafeMutableRawPointer
    private var owned: Bool
    
    /// Create a new FlatBuffer (Swift owns it)
    public init() {
        self.handle = nprpc_flatbuffer_create()
        self.owned = true
    }
    
    /// Wrap an existing C++ flat_buffer
    /// - Parameters:
    ///   - wrapping: Opaque pointer to C++ flat_buffer
    ///   - owned: If true, Swift will free the buffer on deinit (default: false)
    internal init(wrapping: UnsafeMutableRawPointer, owned: Bool = false) {
        self.handle = wrapping
        self.owned = owned
    }
    
    deinit {
        if owned {
            nprpc_flatbuffer_destroy(handle)
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

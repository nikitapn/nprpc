// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

import CNprpc

/// Swift wrapper for nprpc::flat_buffer
public class FlatBuffer {
    internal var handle: UnsafeMutableRawPointer
    private var owned: Bool
    
    /// Create a new FlatBuffer (Swift owns it)
    public init() {
        self.handle = nprpc_flatbuffer_create()
        self.owned = true
    }
    
    /// Wrap an existing C++ flat_buffer (Swift doesn't own it)
    internal init(wrapping: UnsafeMutableRawPointer) {
        self.handle = wrapping
        self.owned = false
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

// Helper functions for standard replies
public func handleStandardReply(buffer: FlatBuffer) -> Int32 {
    return nprpc_handle_standard_reply(buffer.handle)
}

public func makeSimpleAnswer(buffer: FlatBuffer, messageId: UInt32) {
    nprpc_make_simple_answer(buffer.handle, messageId)
}

// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

// Main module for NPRPC Swift bindings
// Re-exports all public types and functions

// Export C++ bridge module
@_exported import CNprpc

// Export Swift wrappers
// These are now in separate files for better organization

/// Get the NPRPC library version
public func version() -> String {
    return String(cString: nprpc_swift.get_version())
}

// ============================================================================
// MARK: - Basic Interop Tests (for POC)
// ============================================================================

/// Test basic C++ interop - add two numbers
public func testAdd(_ a: Int32, _ b: Int32) -> Int32 {
    return nprpc_swift.add_numbers(a, b)
}

/// Test string passing to C++
public func testGreet(_ name: String) -> String {
    // Convert Swift String to std::string for C++ interop
    let cxxName = std.string(name)
    let result = nprpc_swift.greet(cxxName)
    return String(cString: nprpc_swift.string_to_cstr(result))
}

/// Test array return from C++
public func testArray() -> [Int32] {
    let vec = nprpc_swift.get_test_array()
    var result: [Int32] = []
    for i in 0..<vec.size() {
        result.append(vec[i])
    }
    return result
}

// ============================================================================
// MARK: - NPRPC Protocol Utilities
// ============================================================================

/// Create a simple answer message (for servant dispatch)
/// Equivalent to nprpc::impl::make_simple_answer in C++
public func makeSimpleAnswer(buffer: FlatBuffer, messageId: impl.MessageId) {
    // Clear the buffer
    buffer.consume(buffer.size)
    
    // Prepare header (16 bytes)
    buffer.prepare(16)
    buffer.commit(16)
    
    guard let data = buffer.data else { return }
    
    // Write header:
    // - size (4 bytes, set to 12 = 16 - 4)
    // - message_id (4 bytes)
    // - message_type (4 bytes, 1 = Answer)
    // - reserved (4 bytes)
    data.storeBytes(of: UInt32(12), toByteOffset: 0, as: UInt32.self)
    data.storeBytes(of: messageId.rawValue, toByteOffset: 4, as: Int32.self)
    data.storeBytes(of: impl.MessageType.answer.rawValue, toByteOffset: 8, as: Int32.self)
    data.storeBytes(of: UInt32(0), toByteOffset: 12, as: UInt32.self) // reserved
}

/// Handle standard RPC reply (for client proxy)
/// Returns 0 for success, -1 for BlockResponse (has data), or error code for errors
public func handleStandardReply(buffer: FlatBuffer) throws -> Int32 {
    guard let data = buffer.constData else { return -2 }
    
    // Read message_id from offset 4
    let messageId = data.load(fromByteOffset: 4, as: Int32.self)
    
    // Check message type based on impl.MessageId enum values
    // functionCall=0, streamInit=1, streamDataChunk=2, streamCompletion=3,
    // streamError=4, streamCancellation=5, blockResponse=6, addReference=7,
    // releaseObject=8, success=9, exception=10, error_PoaNotExist=11, etc.
    
    switch messageId {
    case impl.MessageId.success.rawValue:
        return 0
    case impl.MessageId.exception.rawValue:
        return 1
    case impl.MessageId.blockResponse.rawValue:
        return -1
    case impl.MessageId.error_ObjectNotExist.rawValue:
        throw ExceptionObjectNotExist()
    case impl.MessageId.error_CommFailure.rawValue:
        throw ExceptionCommFailure()
    case impl.MessageId.error_UnknownFunctionIdx.rawValue:
        throw ExceptionUnknownFunctionIndex()
    case impl.MessageId.error_UnknownMessageId.rawValue:
        throw ExceptionUnknownMessageId()
    case impl.MessageId.error_BadAccess.rawValue:
        throw ExceptionBadAccess()
    case impl.MessageId.error_BadInput.rawValue:
        throw ExceptionBadInput()
    default:
        return -1
    }
}

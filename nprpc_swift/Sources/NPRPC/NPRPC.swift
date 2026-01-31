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
public func handleStandardReply(buffer: FlatBuffer) -> Int32 {
    guard let data = buffer.constData else { return -2 }
    
    // Read message_id from offset 4
    let messageId = data.load(fromByteOffset: 4, as: UInt32.self)
    
    // Check message type
    // 0 = Request, 1 = Answer
    // Success = 10, Exception = 11, BlockResponse = 2
    // Errors: PoaNotExist = 12, ObjectNotExist = 13, etc.
    
    switch messageId {
    case 10: // Success
        return 0
    case 2: // BlockResponse (has return data)
        return -1
    case 11: // Exception
        return Int32(messageId)
    default: // Error codes
        return Int32(messageId)
    }
}

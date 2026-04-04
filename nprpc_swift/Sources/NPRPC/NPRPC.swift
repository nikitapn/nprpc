// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

// Main module for NPRPC Swift bindings
// Re-exports all public types and functions

// Export C bridge module
@_exported import CNprpc

/// Get current thread ID for logging
/// Should match C++ thread ID for consistency in logs
public func getThreadId() -> UInt32 {
    return nprpc_get_thread_id()
}

/// Get current thread name for logging
public func getThreadName() -> String {
    return String(cString: nprpc_get_thread_name())
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
    // - size (4 bytes, set to 16)
    // - message_id (4 bytes)
    // - message_type (4 bytes, 1 = Answer)
    // - reserved (4 bytes)
    data.storeBytes(of: UInt32(16), toByteOffset: 0, as: UInt32.self)
    data.storeBytes(of: messageId.rawValue, toByteOffset: 4, as: UInt32.self)
    data.storeBytes(of: impl.MessageType.Answer.rawValue, toByteOffset: 8, as: UInt32.self)
    data.storeBytes(of: UInt32(0), toByteOffset: 12, as: UInt32.self) // reserved
}

/// Handle standard RPC reply (for client proxy)
/// Returns 0 for success, -1 for BlockResponse (has data), or error code for errors
public func handleStandardReply(buffer: FlatBuffer) throws -> Int32 {
    guard let data = buffer.constData else { return -2 }

    // Read message_id from offset 4
    let messageId = data.load(fromByteOffset: 4, as: UInt32.self)

    switch messageId {
    case impl.MessageId.Success.rawValue:
        return 0
    case impl.MessageId.Exception.rawValue:
        return 1
    case impl.MessageId.BlockResponse.rawValue:
        return -1
    case impl.MessageId.Error_ObjectNotExist.rawValue:
        throw ExceptionObjectNotExist()
    case impl.MessageId.Error_CommFailure.rawValue:
        throw ExceptionCommFailure()
    case impl.MessageId.Error_UnknownFunctionIdx.rawValue:
        throw ExceptionUnknownFunctionIndex()
    case impl.MessageId.Error_UnknownMessageId.rawValue:
        throw ExceptionUnknownMessageId()
    case impl.MessageId.Error_BadAccess.rawValue:
        throw ExceptionBadAccess()
    case impl.MessageId.Error_BadInput.rawValue:
        throw ExceptionBadInput()
    default:
        return -1
    }
}

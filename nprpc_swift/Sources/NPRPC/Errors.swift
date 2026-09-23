// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

// Swift error types bridging C++ exceptions from NPRPC

/// Base protocol for all NPRPC errors
public protocol NPRPCError: Error {
    /// What went wrong, for logs and error messages.
    var message: String { get }
}

/// Generic NPRPC runtime error
public struct RuntimeError: NPRPCError {
    /// What went wrong.
    public let message: String
    
    /// An error with `message`.
    public init(message: String) {
        self.message = message
    }
}

/// Connection-related error
public struct ConnectionError: NPRPCError {
    /// What went wrong.
    public let message: String
    
    /// An error with `message`.
    public init(message: String) {
        self.message = message
    }
}

/// Buffer operation error
public struct BufferError: NPRPCError {
    /// What went wrong.
    public let message: String
    
    /// An error with `message`.
    public init(message: String) {
        self.message = message
    }
}

/// Unexpected reply error
public struct UnexpectedReplyError: NPRPCError {
    /// What went wrong.
    public let message: String
    
    /// An error with `message`.
    public init(message: String) {
        self.message = message
    }
}

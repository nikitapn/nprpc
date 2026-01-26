// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

// Swift error types bridging C++ exceptions from NPRPC

/// Base protocol for all NPRPC errors
public protocol NPRPCError: Error {
    var message: String { get }
}

/// Generic NPRPC runtime error
public struct RuntimeError: NPRPCError {
    public let message: String
    
    public init(message: String) {
        self.message = message
    }
}

/// Connection-related error
public struct ConnectionError: NPRPCError {
    public let message: String
    
    public init(message: String) {
        self.message = message
    }
}

/// Buffer operation error
public struct BufferError: NPRPCError {
    public let message: String
    
    public init(message: String) {
        self.message = message
    }
}

/// Unexpected reply error
public struct UnexpectedReplyError: NPRPCError {
    public let message: String
    
    public init(message: String) {
        self.message = message
    }
}

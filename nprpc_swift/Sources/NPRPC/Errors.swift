// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

// Swift error types bridging NPRPC results.

import CNprpc

/// Base protocol for all NPRPC errors
public protocol NPRPCError: Error {
    /// What went wrong, for logs and error messages.
    var message: String { get }
}

/// A local RPC setup or transport failure. Remote IDL errors retain their
/// generated types and are decoded from a successful transport response.
public struct RpcError: NPRPCError, Sendable {
    public enum Kind: Int32, Sendable {
        case invalidArgument   = 1
        case noEndpoint        = 2
        case notInitialized    = 3
        case timeout           = 4
        case cancelled         = 5
        case connectionFailure = 6
        case outOfMemory       = 7
        case internalError     = 8
        case unsupportedTransport = 9
    }

    public let kind: Kind
    public let message: String
    /// The original transport/system code, or zero when unavailable.
    public let nativeCode: Int32
    public let nativeCategory: String?

    public init(kind: Kind, message: String, nativeCode: Int32 = 0,
                nativeCategory: String? = nil) {
        self.kind = kind
        self.message = message
        self.nativeCode = nativeCode
        self.nativeCategory = nativeCategory
    }

    // The C++ error and strings are borrowed only during completion.
    internal init(bridge: NprpcBridgeError) {
        kind = Kind(rawValue: bridge.kind.rawValue) ?? .internalError
        message = bridge.message.map { String(cString: $0) } ?? "Unknown RPC error"
        nativeCode = bridge.native_code
        let category = bridge.native_category.map { String(cString: $0) }
        nativeCategory = category?.isEmpty == false ? category : nil
    }
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

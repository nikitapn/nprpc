// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

// Swift error types bridging C++ exceptions from NPRPC

/// Base protocol for all NPRPC errors
public protocol NPRPCError: Error {
    var message: String { get }
}

/// Object not found in POA (equivalent to C++ object_not_exist)
public struct ObjectNotFoundError: NPRPCError {
    public let message: String
    public let objectId: UInt64?
    
    public init(message: String, objectId: UInt64? = nil) {
        self.message = message
        self.objectId = objectId
    }
}

/// Transport-level connection error
public struct ConnectionError: NPRPCError {
    public let message: String
    public let endpoint: String?
    
    public init(message: String, endpoint: String? = nil) {
        self.message = message
        self.endpoint = endpoint
    }
}

/// RPC timeout error
public struct TimeoutError: NPRPCError {
    public let message: String
    public let durationSeconds: Double?
    
    public init(message: String, durationSeconds: Double? = nil) {
        self.message = message
        self.durationSeconds = durationSeconds
    }
}

/// Marshalling/unmarshalling error
public struct MarshallingError: NPRPCError {
    public let message: String
    
    public init(message: String) {
        self.message = message
    }
}

/// Invalid endpoint URL
public struct InvalidEndpointError: NPRPCError {
    public let message: String
    public let url: String?
    
    public init(message: String, url: String? = nil) {
        self.message = message
        self.url = url
    }
}

/// POA-related errors
public struct PoaError: NPRPCError {
    public let message: String
    
    public init(message: String) {
        self.message = message
    }
}

/// Generic NPRPC runtime error
public struct RuntimeError: NPRPCError {
    public let message: String
    
    public init(message: String) {
        self.message = message
    }
}

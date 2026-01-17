// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

// Swift wrapper for NPRPC POA (Portable Object Adapter)

import CNprpc

/// Object activation flags controlling which transports an object is available on
public struct ObjectActivationFlags: OptionSet, Sendable {
    public let rawValue: UInt32
    
    public init(rawValue: UInt32) {
        self.rawValue = rawValue
    }
    
    /// Allow TCP connections
    public static let allowTcp = ObjectActivationFlags(rawValue: 1 << 0)
    
    /// Allow WebSocket connections
    public static let allowWebSocket = ObjectActivationFlags(rawValue: 1 << 1)
    
    /// Allow HTTP connections
    public static let allowHttp = ObjectActivationFlags(rawValue: 1 << 2)
    
    /// Allow QUIC connections
    public static let allowQuic = ObjectActivationFlags(rawValue: 1 << 3)
    
    /// Allow UDP datagrams
    public static let allowUdp = ObjectActivationFlags(rawValue: 1 << 4)
    
    /// Allow shared memory transport
    public static let allowSharedMemory = ObjectActivationFlags(rawValue: 1 << 5)
    
    /// Allow all transports
    public static let allowAll: ObjectActivationFlags = [
        .allowTcp, .allowWebSocket, .allowHttp, 
        .allowQuic, .allowUdp, .allowSharedMemory
    ]
    
    /// Network transports only (no shared memory)
    public static let networkOnly: ObjectActivationFlags = [
        .allowTcp, .allowWebSocket, .allowHttp, .allowQuic, .allowUdp
    ]
}

/// Object ID policy for POA
public enum ObjectIdPolicy {
    /// POA generates object IDs automatically
    case systemGenerated
    
    /// User supplies object IDs explicitly
    case userSupplied
}

/// POA lifetime policy
public enum PoaLifetime {
    /// POA lives until explicitly destroyed
    case persistent
    
    /// POA is destroyed when all objects are deactivated
    case transient
}

/// Portable Object Adapter - manages servant lifecycle
///
/// POAs are containers for servants (object implementations). They:
/// - Assign object IDs to servants
/// - Route incoming requests to the correct servant
/// - Control which transports servants are accessible on
/// - Manage servant lifecycle (activation/deactivation)
///
/// Example:
/// ```swift
/// let poa = try rpc.createPoa(
///     maxObjects: 100,
///     lifetime: .persistent,
///     idPolicy: .systemGenerated
/// )
/// 
/// let servant = MyServantImpl()
/// let objectId = try poa.activateObject(
///     servant,
///     flags: .networkOnly
/// )
/// ```
public final class Poa {
    // For POC, this is a placeholder since we don't have actual nprpc::Poa yet
    private let maxObjects: UInt32
    private let lifetime: PoaLifetime
    private let idPolicy: ObjectIdPolicy
    
    init(maxObjects: UInt32, lifetime: PoaLifetime, idPolicy: ObjectIdPolicy) {
        self.maxObjects = maxObjects
        self.lifetime = lifetime
        self.idPolicy = idPolicy
    }
    
    /// Maximum number of objects this POA can hold
    public var capacity: UInt32 {
        return maxObjects
    }
    
    // TODO: Implement actual servant activation when we link against libnprpc
    // This requires C++ bridge functions for:
    // - nprpc::Poa::activate_object()
    // - nprpc::Poa::activate_object_with_id()
    // - nprpc::Poa::deactivate_object()
}

// MARK: - Rpc extension for POA management
extension Rpc {
    /// Create a new POA with specified configuration
    /// - Parameters:
    ///   - maxObjects: Maximum number of objects (0 = unlimited)
    ///   - lifetime: Lifetime policy
    ///   - idPolicy: Object ID assignment policy
    /// - Returns: New POA instance
    /// - Throws: PoaError if creation fails
    public func createPoa(
        maxObjects: UInt32 = 0,
        lifetime: PoaLifetime = .persistent,
        idPolicy: ObjectIdPolicy = .systemGenerated
    ) throws -> Poa {
        guard self.isInitialized else {
            throw RuntimeError(message: "Rpc not initialized")
        }
        
        // TODO: Call actual nprpc::Rpc::create_poa() via C++ bridge
        // For POC, just create a placeholder
        return Poa(
            maxObjects: maxObjects,
            lifetime: lifetime,
            idPolicy: idPolicy
        )
    }
}

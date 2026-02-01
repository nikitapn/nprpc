// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

// Swift wrapper for NPRPC POA (Portable Object Adapter)

import CNprpc
import Foundation

#if canImport(Glibc)
import Glibc
#elseif canImport(Darwin)
import Darwin
#endif

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

// MARK: - Global dispatch function for C++ callback
/// This is a C-compatible function that C++ calls when dispatching to Swift servants.
/// It extracts the NPRPCServant from the opaque pointer and calls its dispatch method.
private let globalServantDispatch: @convention(c) (UnsafeMutableRawPointer?, UnsafeMutableRawPointer?, UnsafeMutableRawPointer?, UnsafeMutableRawPointer?) -> Void = { servantPtr, rxBuffer, txBuffer, endpointPtr in
    guard let servantPtr = servantPtr else { return }
    guard let rxBuffer = rxBuffer else { return }
    guard let txBuffer = txBuffer else { return }
    
    // Reconstruct servant (don't consume - just borrow)
    let servant = Unmanaged<NPRPCServant>.fromOpaque(servantPtr).takeUnretainedValue()
    
    // Wrap rx buffer for servant dispatch
    let buffer = FlatBuffer(wrapping: rxBuffer)
    
    // Create endpoint info (simplified for now)
    let endpoint = NPRPCEndpoint(type: .tcp, hostname: "localhost", port: 0)
    
    // Call servant dispatch - it writes response back to the same buffer
    servant.dispatch(buffer: buffer, remoteEndpoint: endpoint)
    
    // Copy response from rx_buffer to tx_buffer
    // The generated servant code writes output to the same buffer it receives
    // But C++ expects response in tx_buffer
    let responseSize = nprpc_flatbuffer_size(rxBuffer)
    if responseSize > 0 {
        // Clear tx buffer and prepare space
        let txSizeBefore = nprpc_flatbuffer_size(txBuffer)
        nprpc_flatbuffer_consume(txBuffer, txSizeBefore)
        nprpc_flatbuffer_prepare(txBuffer, responseSize)
        nprpc_flatbuffer_commit(txBuffer, responseSize)
        
        // Copy data
        if let srcData = nprpc_flatbuffer_cdata(rxBuffer),
           let dstData = nprpc_flatbuffer_data(txBuffer) {
            memcpy(dstData, srcData, responseSize)
        }
    }
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
    /// Opaque handle to C++ nprpc::Poa
    internal let handle: UnsafeMutableRawPointer
    
    internal init(handle: UnsafeMutableRawPointer) {
        self.handle = handle
    }
    
    /// POA index
    public var index: UInt16 {
        return nprpc_poa_get_index(handle)
    }
    
    /// Activate a servant in this POA
    /// - Parameters:
    ///   - servant: The servant to activate
    ///   - flags: Transport flags controlling how the object is accessible
    /// - Returns: ObjectId data for the activated object
    /// - Throws: RuntimeError if activation fails
    public func activateObject(
        _ servant: NPRPCServant,
        flags: ObjectActivationFlags = .networkOnly
    ) throws -> detail.ObjectId {
        // Get unmanaged pointer to servant (we need to prevent Swift from deallocating it)
        let unmanagedServant = Unmanaged.passRetained(servant)
        let servantPtr = unmanagedServant.toOpaque()
        
        guard let oidPtr = nprpc_poa_activate_swift_servant(
            handle,
            servantPtr,
            servant.getClass(),
            flags.rawValue,
            globalServantDispatch
        ) else {
            // Release the retained servant since activation failed
            unmanagedServant.release()
            throw RuntimeError(message: "Failed to activate servant")
        }
        
        // Read ObjectId from the returned pointer
        let objectId = detail.ObjectId(
            object_id: nprpc_objectid_get_object_id(oidPtr),
            poa_idx: nprpc_objectid_get_poa_idx(oidPtr),
            flags: nprpc_objectid_get_flags(oidPtr),
            origin: Array(UnsafeBufferPointer(
                start: nprpc_objectid_get_origin(oidPtr),
                count: 16
            )),
            class_id: String(cString: nprpc_objectid_get_class_id(oidPtr)),
            urls: String(cString: nprpc_objectid_get_urls(oidPtr))
        )
        
        // Free the ObjectId allocated by C++
        nprpc_objectid_destroy(oidPtr)
        
        return objectId
    }
    
    /// Deactivate an object by its ID
    /// - Parameter objectId: The object ID to deactivate
    public func deactivateObject(_ objectId: UInt64) {
        nprpc_poa_deactivate_object(handle, objectId)
    }
}

// MARK: - Rpc extension for POA management
extension Rpc {
    /// Create a new POA with specified configuration
    /// - Parameters:
    ///   - maxObjects: Maximum number of objects (0 = unlimited)
    ///   - lifetime: Lifetime policy
    ///   - idPolicy: Object ID assignment policy
    /// - Returns: New POA instance
    /// - Throws: RuntimeError if creation fails
    public func createPoa(
        maxObjects: UInt32 = 0,
        lifetime: PoaLifetime = .persistent,
        idPolicy: ObjectIdPolicy = .systemGenerated
    ) throws -> Poa {
        guard self.isInitialized, let rpcHandle = self.handle else {
            throw RuntimeError(message: "Rpc not initialized")
        }
        
        let lifespanValue: UInt32 = (lifetime == .persistent) ? 0 : 1
        let idPolicyValue: UInt32 = (idPolicy == .systemGenerated) ? 0 : 1
        
        // Use the C bridge function instead of C++ method 
        // Convert typed pointer to raw pointer for C function
        let rawHandle = UnsafeMutableRawPointer(rpcHandle)
        guard let poaHandle = nprpc_rpc_create_poa(rawHandle, maxObjects, lifespanValue, idPolicyValue) else {
            throw RuntimeError(message: "Failed to create POA")
        }
        
        return Poa(handle: poaHandle)
    }
}

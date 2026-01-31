// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

import CNprpc

/// Represents NPRPC object metadata for marshalling
/// This is a typealias to the IDL-generated detail.ObjectId
public typealias NPRPCObjectData = detail.ObjectId

/// Swift representation of a remote object
public class NPRPCObject {
    internal let handle: UnsafeMutableRawPointer
    
    public let objectId: UInt64
    public let poaIdx: UInt16
    public var endpoint: NPRPCEndpoint
    public let session: NPRPCSession
    public var timeout: UInt32
    
    public var data: NPRPCObjectData {
        NPRPCObjectData(
            object_id: objectId,
            poa_idx: poaIdx,
            flags: 0,
            origin: [UInt8](repeating: 0, count: 16),
            class_id: "",
            urls: ""
        )
    }
    
    internal init(handle: UnsafeMutableRawPointer,
                  objectId: UInt64,
                  poaIdx: UInt16,
                  endpoint: NPRPCEndpoint,
                  session: NPRPCSession,
                  timeout: UInt32 = 30000) {
        self.handle = handle
        self.objectId = objectId
        self.poaIdx = poaIdx
        self.endpoint = endpoint
        self.session = session
        self.timeout = timeout
    }
    
    deinit {
        // Release reference to C++ object
        nprpc_object_release(handle)
    }
}

/// Type-safe wrapper for remote object references
/// Generic parameter T is the interface type (e.g., Nameserver)
public struct ObjectPtr<T> {
    public let data: NPRPCObjectData
    
    public init(data: NPRPCObjectData) {
        self.data = data
    }
    
    /// Default init for zero/nil object
    public init() {
        self.data = NPRPCObjectData(
            object_id: 0,
            poa_idx: 0,
            flags: 0,
            origin: [UInt8](repeating: 0, count: 16),
            class_id: "",
            urls: ""
        )
    }
}

/// Dummy endpoint for now
public struct NPRPCEndpoint {
    // TODO: Implement endpoint details
}

/// Dummy session for now
public struct NPRPCSession {
    internal let handle: UnsafeMutableRawPointer
    
    /// Send a request and receive a reply (for client-side calls)
    /// TODO: Implement via C++ bridge
    public func sendReceive(buffer: FlatBuffer, timeout: UInt32) throws {
        throw RuntimeError(message: "Client-side RPC not yet implemented in Swift")
    }
}

/// Base class for Swift servants
open class NPRPCServant {
    public init() {}
    
    /// Override this to handle RPC dispatch
    open func dispatch(buffer: FlatBuffer, remoteEndpoint: NPRPCEndpoint) {
        fatalError("Subclass must override dispatch")
    }
}

/// Swift servant bridge - connects Swift servants to C++ POA
internal class SwiftServantBridge {
    weak var servant: NPRPCServant?
    let className: String
    
    init(servant: NPRPCServant, className: String) {
        self.servant = servant
        self.className = className
    }
    
    /// C-compatible dispatch function that will be called from C++
    static let dispatchTrampoline: @convention(c) (UnsafeMutableRawPointer?, UnsafeMutableRawPointer?, UnsafeMutableRawPointer?, UnsafeMutableRawPointer?) -> Void = { swiftServantPtr, rxBufferPtr, txBufferPtr, endpointPtr in
        guard let swiftServantPtr = swiftServantPtr,
              let rxBufferPtr = rxBufferPtr else {
            return
        }
        
        let bridge = Unmanaged<SwiftServantBridge>.fromOpaque(swiftServantPtr).takeUnretainedValue()
        guard let servant = bridge.servant else { return }
        
        // Wrap C++ flat_buffer without taking ownership
        let buffer = FlatBuffer(wrapping: rxBufferPtr)
        let endpoint = NPRPCEndpoint() // TODO: Extract from endpointPtr
        
        servant.dispatch(buffer: buffer, remoteEndpoint: endpoint)
    }
}

// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

import CNprpc

/// Represents NPRPC object metadata for marshalling
public struct NPRPCObjectData {
    public let objectId: UInt64
    public let poaIdx: UInt16
    public let flags: UInt16
    public let origin: [UInt8]  // UUID - 16 bytes
    public let classId: String
    public let urls: String
    
    public init(objectId: UInt64, poaIdx: UInt16, flags: UInt16, origin: [UInt8], classId: String, urls: String) {
        self.objectId = objectId
        self.poaIdx = poaIdx
        self.flags = flags
        self.origin = origin
        self.classId = classId
        self.urls = urls
    }
}

/// Swift representation of a remote object
public class NPRPCObject {
    internal let handle: UnsafeMutableRawPointer
    
    public let objectId: UInt64
    public let poaIdx: UInt16
    public let endpoint: NPRPCEndpoint
    public let session: NPRPCSession
    public let timeout: UInt32
    
    public var data: NPRPCObjectData {
        NPRPCObjectData(
            objectId: objectId,
            poaIdx: poaIdx,
            flags: 0,
            origin: [UInt8](repeating: 0, count: 16),
            classId: "",
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

/// Dummy endpoint for now
public struct NPRPCEndpoint {
    // TODO: Implement endpoint details
}

/// Dummy session for now
public struct NPRPCSession {
    internal let handle: UnsafeMutableRawPointer
    
    // TODO: Implement sendReceive for client-side calls
    // public func sendReceive(buffer: FlatBuffer, timeout: UInt32) throws {
    //     // Not implemented yet
    // }
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

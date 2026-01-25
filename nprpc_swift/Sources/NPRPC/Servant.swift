// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

import CNprpc

/// Swift representation of a remote object
public class NPRPCObject {
    internal let handle: UnsafeMutableRawPointer
    
    public let objectId: UInt64
    public let poaIdx: UInt16
    public let endpoint: NPRPCEndpoint
    public let session: NPRPCSession
    public let timeout: UInt32
    
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
    
    public func sendReceive(buffer: FlatBuffer, timeout: UInt32) throws {
        let result = nprpc_session_send_receive(handle, buffer.handle, timeout)
        if result != 0 {
            throw NPRPCError.rpcError
        }
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

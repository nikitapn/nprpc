// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

/// Endpoint information for connecting to remote objects
public struct NPRPCEndpoint {
    /// Type of transport (TCP, HTTP, WebSocket, etc.)
    public let type: EndPointType
    
    /// Hostname or IP address
    public let hostname: String
    
    /// Port number (0 for shared memory)
    public let port: UInt16
    
    /// Initialize with all components
    public init(type: EndPointType = .tcp, hostname: String = "", port: UInt16 = 0) {
        self.type = type
        self.hostname = hostname
        self.port = port
    }
    
    /// Check if this endpoint is empty/uninitialized
    public var isEmpty: Bool {
        hostname.isEmpty
    }
    
    /// Convert to URL string (e.g., "tcp://localhost:15000")
    public func toURL() -> String {
        let prefix: String
        switch type {
        case .tcp, .tcpTethered: prefix = "tcp://"
        case .webSocket: prefix = "ws://"
        case .securedWebSocket: prefix = "wss://"
        case .http: prefix = "http://"
        case .securedHttp: prefix = "https://"
        case .sharedMemory: return "mem://\(hostname)"  // No port for shared memory
        case .udp: prefix = "udp://"
        case .quic: prefix = "quic://"
        }
        return "\(prefix)\(hostname):\(port)"
    }
}

/// Base class for Swift servants
open class NPRPCServant {
    public init() {}
    
    /// Override this to return the class name/ID of this servant
    /// This is typically the fully qualified interface name from the IDL
    open func getClass() -> String {
        fatalError("Subclass must override getClass to return the interface class ID")
    }
    
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

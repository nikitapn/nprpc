// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

import CNprpc

/// Represents NPRPC object metadata for marshalling
/// This is a typealias to the IDL-generated detail.ObjectId
public typealias NPRPCObjectData = detail.ObjectId

/// Swift representation of a remote C++ nprpc::Object
/// All data is accessed through the C++ object via the bridge - no duplication
public class NPRPCObject {
    /// Opaque handle to C++ nprpc::Object
    internal let handle: UnsafeMutableRawPointer
    
    // MARK: - ObjectId properties (from C++ ObjectId base class)
    
    /// The unique object ID within its POA
    public var objectId: UInt64 {
        nprpc_objectid_get_object_id(handle)
    }
    
    /// The POA index where this object is activated
    public var poaIdx: UInt16 {
        nprpc_objectid_get_poa_idx(handle)
    }
    
    /// Object flags (persistence, etc.)
    public var flags: UInt16 {
        nprpc_objectid_get_flags(handle)
    }
    
    /// The class ID string identifying the interface type
    public var classId: String {
        String(cString: nprpc_objectid_get_class_id(handle))
    }
    
    /// Available endpoint URLs for this object
    public var urls: String {
        String(cString: nprpc_objectid_get_urls(handle))
    }
    
    /// The origin UUID of the process that created this object
    public var origin: [UInt8] {
        guard let ptr = nprpc_objectid_get_origin(handle) else {
            return [UInt8](repeating: 0, count: 16)
        }
        return Array(UnsafeBufferPointer(start: ptr, count: 16))
    }
    
    // MARK: - Object properties (from C++ Object class)
    
    /// The currently selected endpoint for communication
    public var endpoint: NPRPCEndpoint {
        NPRPCEndpoint(
            type: EndPointType(rawValue: Int32(nprpc_object_get_endpoint_type(handle))) ?? .tcp,
            hostname: String(cString: nprpc_object_get_endpoint_hostname(handle)),
            port: nprpc_object_get_endpoint_port(handle)
        )
    }
    
    /// Timeout in milliseconds for RPC calls
    public var timeout: UInt32 {
        get { nprpc_object_get_timeout(handle) }
        set { _ = nprpc_object_set_timeout(handle, newValue) }
    }
    
    // MARK: - Methods
    
    /// Increment reference count
    @discardableResult
    public func addRef() -> UInt32 {
        nprpc_object_add_ref(handle)
    }
    
    /// Decrement reference count (called automatically in deinit)
    @discardableResult
    public func release() -> UInt32 {
        // Note: This is exposed but typically you let deinit handle it
        // Calling this manually may lead to double-release!
        nprpc_object_release(handle)
        return 0  // C++ release() returns the new count, but our bridge is void
    }
    
    /// Select the best available endpoint for communication
    /// - Returns: true if an endpoint was selected, false if none available
    @discardableResult
    public func selectEndpoint() -> Bool {
        nprpc_object_select_endpoint(handle)
    }
    
    /// Get the class name (same as classId, for compatibility with C++ API)
    public func getClass() -> String {
        classId
    }
    
    // MARK: - RPC Communication
    
    /// Send a request and receive a reply (for client-side calls)
    /// - Parameters:
    ///   - buffer: The FlatBuffer containing the request/response
    ///   - timeout: Timeout in milliseconds
    /// - Throws: RpcError on communication failure
    public func sendReceive(buffer: FlatBuffer, timeout: UInt32) throws {
        let result = nprpc_object_send_receive(handle, buffer.handle, timeout)
        switch result {
        case 0:
            return  // Success
        case -1:
            throw RuntimeError(message: "RPC call failed: invalid arguments")
        case -2:
            throw RuntimeError(message: "RPC call failed: could not select endpoint")
        case -3:
            throw RuntimeError(message: "RPC call failed: communication error")
        default:
            throw RuntimeError(message: "RPC call failed: unknown error (\(result))")
        }
    }
    
    // MARK: - Data conversion for marshalling
    
    /// Get ObjectId data for marshalling to remote calls
    public var data: NPRPCObjectData {
        NPRPCObjectData(
            object_id: objectId,
            poa_idx: poaIdx,
            flags: flags,
            origin: origin,
            class_id: classId,
            urls: urls
        )
    }
    
    // MARK: - Initialization
    
    /// Initialize with a C++ Object handle
    /// - Parameter handle: Opaque pointer to nprpc::Object (takes ownership of reference)
    internal init(handle: UnsafeMutableRawPointer) {
        self.handle = handle
    }
    
    deinit {
        // Release our reference to C++ object
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

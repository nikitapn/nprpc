import CNprpc

/// Represents NPRPC object metadata for marshalling
/// This is a typealias to the IDL-generated detail.ObjectId
public typealias NPRPCObjectData = detail.ObjectId

internal let invalidObjectId = UInt64.max

/// Helper class to box a Swift continuation for passing to C++ as an opaque pointer
/// Used for async RPC calls with callback-based completion
private final class ContinuationBox<T> {
    let continuation: CheckedContinuation<T, Error>
    init(_ continuation: CheckedContinuation<T, Error>) {
        self.continuation = continuation
    }
}

/// Swift representation of a remote C++ nprpc::Object
/// All data is accessed through the C++ object via the bridge - no duplication
open class NPRPCObject: Codable, @unchecked Sendable {
    // Class ID for this object type (used for type checking in narrow)
    // Will be overridden in subclasses to return the correct class ID string
    open class var classId: String {
      "unknown"
    }

    /// Opaque handle to C++ nprpc::Object
    internal let handle: UnsafeMutableRawPointer

    // MARK: - Codable

    private enum CodingKeys: String, CodingKey {
        case ior
    }

    public func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        try container.encode(toString(), forKey: .ior)
    }

    public required init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        let ior = try container.decode(String.self, forKey: .ior)
        guard let h = nprpc_object_from_string(ior) else {
            throw DecodingError.dataCorruptedError(forKey: .ior, in: container, debugDescription: "Invalid NPRPC IOR string")
        }
        self.handle = h
    }

    // MARK: - ObjectId properties (from C++ ObjectId base class)
    // NOTE: We use nprpc_object_get_* functions because handle is Object* (not raw ObjectId*)
    // Object has a vtable, so the accessor functions must cast to Object* not ObjectId*

    /// The unique object ID within its POA
    public var objectId: UInt64 {
        nprpc_object_get_object_id(handle)
    }

    /// The POA index where this object is activated
    public var poaIdx: UInt16 {
        nprpc_object_get_poa_idx(handle)
    }

    /// Object flags (persistence, etc.)
    public var flags: UInt16 {
        nprpc_object_get_flags(handle)
    }

    /// The class ID string identifying the interface type
    public var classId: String {
        String(cString: nprpc_object_get_class_id(handle))
    }

    /// Available endpoint URLs for this object
    public var urls: String {
        String(cString: nprpc_object_get_urls(handle))
    }

    /// The origin UUID of the process that created this object
    public var origin: [UInt8] {
        guard let ptr = nprpc_object_get_origin(handle) else {
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

    /// Select endpoint with a preferred endpoint hint
    /// - Parameter endpoint: The preferred endpoint to try to select
    /// - Returns: true if the endpoint was selected, false if not available
    @discardableResult
    public func selectEndpoint(_ endpoint: NPRPCEndpoint) -> Bool {
        nprpc_object_select_endpoint_with_info(
            handle,
            UInt32(endpoint.type.rawValue),
            endpoint.hostname,
            endpoint.port
        )
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

    /// Send a request asynchronously with async/await support
    /// - Parameters:
    ///   - buffer: The FlatBuffer containing the request (ownership transferred)
    ///   - timeout: Timeout in milliseconds
    /// - Throws: RpcError on communication failure
    public func sendAsync(buffer: FlatBuffer, timeout: UInt32) async throws {
        try await withCheckedThrowingContinuation { (continuation: CheckedContinuation<Void, Error>) in
            // Box the continuation so we can pass a pointer to C++
            let boxedContinuation = Unmanaged.passRetained(
                ContinuationBox(continuation)
            ).toOpaque()
            
            // C callback that resumes the Swift continuation
            let callback: swift_async_callback = { context, errorCode, errorMessage in
                guard let context = context else { return }
                let box = Unmanaged<ContinuationBox<Void>>.fromOpaque(context).takeRetainedValue()
                if errorCode == 0 {
                    box.continuation.resume()
                } else {
                    let msg = errorMessage.map { String(cString: $0) } ?? "Unknown async RPC error"
                    box.continuation.resume(throwing: RuntimeError(message: msg))
                }
            }
            
            let result = nprpc_object_send_async(
                handle,
                buffer.handle,
                boxedContinuation,
                callback,
                timeout
            )
            
            if result != 0 {
                // Failed to start - take back ownership and resume with error
                let box = Unmanaged<ContinuationBox<Void>>.fromOpaque(boxedContinuation).takeRetainedValue()
                let errorMsg: String
                switch result {
                case -1: errorMsg = "Async RPC call failed: invalid arguments"
                case -2: errorMsg = "Async RPC call failed: could not select endpoint"
                default: errorMsg = "Async RPC call failed: unknown error (\(result))"
                }
                box.continuation.resume(throwing: RuntimeError(message: errorMsg))
            }
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
    public required init(handle: UnsafeMutableRawPointer) {
        self.handle = handle
    }

    /// Create an empty/null object (not connected to any C++ object)
    /// Used as a placeholder in struct default values
    public init() {
        // Create an empty object (not connected to any C++ object)
        self.handle = UnsafeMutableRawPointer(bitPattern: 0x1)!  // Dummy non-null pointer
    }

    // MARK: - String Serialization (NPRPC IOR format)

    /// Serialize this object reference to a string (similar to CORBA IOR)
    /// Format: "NPRPC1:<base64_encoded_data>"
    /// This string can be shared and used to recreate the object reference elsewhere.
    public func toString() -> String {
        guard let cStr = nprpc_object_to_string(handle) else {
            return ""
        }
        defer { nprpc_free_string(cStr) }
        return String(cString: cStr)
    }

    /// Create an NPRPCObject from a serialized string (NPRPC IOR format)
    /// - Parameter str: The serialized object reference string
    /// - Returns: A new NPRPCObject, or nil if parsing failed
    public static func fromString(_ str: String) -> NPRPCObject? {
        guard let handle = nprpc_object_from_string(str) else {
            return nil
        }
        return NPRPCObject(handle: handle)
    }

    /// Create an NPRPCObject from ObjectId data
    /// - Parameter objectId: The ObjectId containing all reference data
    /// - Returns: A new NPRPCObject, or nil if creation failed
    public static func fromObjectId(_ objectId: detail.ObjectId) -> NPRPCObject? {
        // Convert origin array to raw bytes
        let originBytes = objectId.origin
        guard originBytes.count == 16 else { return nil }

        let handle = originBytes.withUnsafeBufferPointer { originPtr -> UnsafeMutableRawPointer? in
            guard let baseAddress = originPtr.baseAddress else { return nil }
            return nprpc_create_object_from_components(
                objectId.object_id,
                objectId.poa_idx,
                objectId.flags,
                baseAddress,
                objectId.class_id,
                objectId.urls
            )
        }

        guard let handle = handle else { return nil }
        let obj = NPRPCObject(handle: handle)

        // Select endpoint so the object is ready for RPC calls
        obj.selectEndpoint()

        return obj
    }

    deinit {
        // Release our reference to C++ object
        nprpc_object_release(handle)
    }
}

/// Narrow Object reference to a specific class (for clientside proxies)
func narrow<T: NPRPCObject>(_ object: NPRPCObject, to type: T.Type) -> T? {
    // In a full implementation, we would check the classId against the expected interface
    // For this simplified version, we just create a new instance of T with the same handle
    if (object.classId != T.classId) {
        return nil  // Class ID mismatch
    }
    return T(handle: object.handle)
}
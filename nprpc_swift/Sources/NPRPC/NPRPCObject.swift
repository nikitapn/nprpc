import Foundation
import CNprpc

/// Represents NPRPC object metadata for marshalling
/// This is a typealias to the IDL-generated detail.ObjectId
public typealias NPRPCObjectData = detail.ObjectId

internal let invalidObjectId = UInt64.max

/// Helper class to box a Swift continuation for passing to C++ as an opaque pointer
/// Used for async RPC calls with callback-based completion
// A call in flight, which either its reply or its task's cancellation finishes,
// whichever comes first. A reply after that is dropped, which frees its buffer.
private final class PendingCall: @unchecked Sendable {
    private let lock = NSLock()
    private var continuation: CheckedContinuation<FlatBuffer?, Error>?
    private var cancelled = false

    // False if the task was already cancelled, in which case nothing should be sent.
    func start(_ continuation: CheckedContinuation<FlatBuffer?, Error>) -> Bool {
        lock.lock()
        let wasCancelled = cancelled
        if !wasCancelled { self.continuation = continuation }
        lock.unlock()
        if wasCancelled { continuation.resume(throwing: CancellationError()) }
        return !wasCancelled
    }

    func cancel() {
        lock.lock()
        cancelled = true
        let waiting = continuation
        continuation = nil
        lock.unlock()
        waiting?.resume(throwing: CancellationError())
    }

    func finish(_ result: Result<FlatBuffer?, Error>) {
        lock.lock()
        let waiting = continuation
        continuation = nil
        lock.unlock()
        waiting?.resume(with: result)
    }
}

/// Swift representation of a remote C++ nprpc::Object
/// All data is accessed through the C++ object via the bridge - no duplication
open class NPRPCObject: Codable, @unchecked Sendable {
    /// Class ID for this proxy type, used for type checking in `narrow`.
    /// Generated subclasses override it with their interface's id.
    open class var classId: String {
      "unknown"
    }

    /// Opaque handle to C++ nprpc::Object
    public let handle: UnsafeMutableRawPointer

    // MARK: - Codable

    private enum CodingKeys: String, CodingKey {
        case ior
    }

    /// Encodes the reference as its IOR string (`ObjectId.toString()`).
    public func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        try container.encode(toString(), forKey: .ior)
    }

    /// Decodes a reference from its IOR string. Throws for a malformed one.
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
            type: EndPointType(rawValue: nprpc_object_get_endpoint_type(handle)) ?? .Tcp,
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

    /// Fire-and-forget send for IDL `[unreliable]` methods.
    ///
    /// Writes the request and returns. Unlike `sendAsync` / `sendAsyncReceive`,
    /// this never waits for a reply and must not occupy the transport's
    /// reply-wait queue — the server deliberately sends nothing back for
    /// `[unreliable]` (e.g. LavaUI `Present`). Using a reply waiter there
    /// times out after ~1s and, on shared memory, steals later real replies
    /// that are matched by queue position.
    ///
    /// - Parameter buffer: Request buffer; storage is consumed (moved) like
    ///   the other send paths.
    /// - Returns: `true` if the transport accepted the write.
    @discardableResult
    public func sendUnreliable(buffer: FlatBuffer) -> Bool {
        nprpc_object_send_unreliable(handle, buffer.handle) == 0
    }

    /// Send a request and wait for its reply, discarding the response body.
    /// Throws `RpcError` for setup and transport failures.
    public func sendAsync(buffer: FlatBuffer, timeout: UInt32) async throws {
        _ = try await sendResult(buffer: buffer, timeout: timeout, receiveResponse: false)
    }

    /// Send a request and receive its response buffer.
    /// Throws `RpcError` for setup and transport failures. Generated proxies
    /// decode remote IDL errors separately from the returned response.
    public func sendAsyncReceive(buffer: FlatBuffer, timeout: UInt32) async throws -> FlatBuffer {
        guard let response = try await sendResult(buffer: buffer, timeout: timeout, receiveResponse: true) else {
            throw RpcError(kind: .internalError, message: "RPC completed without a response buffer")
        }
        return response
    }

    private func sendResult(buffer: FlatBuffer, timeout: UInt32,
                            receiveResponse: Bool) async throws -> FlatBuffer? {
        try Task.checkCancellation()
        let call = PendingCall()
        // Cancelling stops this task waiting. The request itself stays pending
        // underneath until its reply or timeout, because transports match replies
        // to requests, and the server has no way to hear about it anyway.
        return try await withTaskCancellationHandler {
            try await withCheckedThrowingContinuation { (continuation: CheckedContinuation<FlatBuffer?, Error>) in
                guard call.start(continuation) else { return }
                let context = Unmanaged.passRetained(call).toOpaque()
                let callback: swift_rpc_completion = { context, error, response in
                    guard let context else { return }
                    let call = Unmanaged<PendingCall>.fromOpaque(context).takeRetainedValue()
                    if let error {
                        call.finish(.failure(RpcError(bridge: error.pointee)))
                    } else {
                        call.finish(.success(response.map { FlatBuffer(wrapping: $0, owned: true) }))
                    }
                }
                // Completion consumes the context exactly once, even for an inline
                // setup failure. There is no separate startup-error ownership path.
                nprpc_object_send_async_result(handle, buffer.handle, context, callback,
                                               timeout, receiveResponse)
            }
        } onCancel: {
            call.cancel()
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
        // nprpc_object_release(handle)
    }
}

/// Why `narrow` returned nil, for an error message. The part of a class ID after
/// the `#` is a hash of the interface's IDL, so two IDs that differ only there
/// are the same interface built from different versions of its IDL.
public func narrowFailure<T: NPRPCObject>(_ object: NPRPCObject, to type: T.Type) -> String {
    let actual = object.classId.split(separator: "#", maxSplits: 1, omittingEmptySubsequences: false)
    let expected = T.classId.split(separator: "#", maxSplits: 1, omittingEmptySubsequences: false)
    if actual.first == expected.first {
        let theirs = actual.count > 1 ? String(actual[1]) : "none"
        let ours = expected.count > 1 ? String(expected[1]) : "none"
        return "\(expected[0]): the server's schema \(theirs) differs from this client's \(ours). Rebuild the older one."
    }
    return "This reference is a \(actual.first ?? ""), not a \(expected.first ?? "")."
}

/// Narrow Object reference to a specific class (for clientside proxies)
public func narrow<T: NPRPCObject>(_ object: NPRPCObject, to type: T.Type) -> T? {
    // In a full implementation, we would check the classId against the expected interface
    // For this simplified version, we just create a new instance of T with the same handle
    if (object.classId != T.classId) {
        return nil  // Class ID mismatch
    }
    return T(handle: object.handle)
}
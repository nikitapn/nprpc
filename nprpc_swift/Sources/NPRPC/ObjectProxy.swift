// Base class for all NPRPC proxy objects
// Mirrors C++ nprpc::Object functionality

open class NPRPCObjectProxy {
  public let object: NPRPCObject
  
  public init(_ object: NPRPCObject) {
    self.object = object
  }
  
  // Reference counting - delegates to C++ Object
  @discardableResult
  public func addRef() -> UInt32 {
    return object.addRef()
  }
  
  @discardableResult
  public func release() -> UInt32 {
    return object.release()
  }
  
  // Timeout management - delegates to C++ Object
  @discardableResult
  public func setTimeout(_ timeoutMs: UInt32) -> UInt32 {
    let oldTimeout = object.timeout
    object.timeout = timeoutMs
    return oldTimeout
  }
  
  public func getTimeout() -> UInt32 {
    return object.timeout
  }
  
  // Endpoint access - delegates to C++ Object
  public func getEndpoint() -> NPRPCEndpoint {
    return object.endpoint
  }
  
  // Select endpoint - delegates to C++ Object's select_endpoint()
  @discardableResult
  public func selectEndpoint(_ remoteEndpoint: NPRPCEndpoint? = nil) -> Bool {
    // TODO: Support passing a specific endpoint when C++ API supports it
    // For now, call C++ select_endpoint() which picks the best available
    return object.selectEndpoint()
  }
  
  // Object ID access - delegates to C++ ObjectId
  public func getObjectId() -> UInt64 {
    return object.objectId
  }
  
  public func getPoaIdx() -> UInt16 {
    return object.poaIdx
  }
  
  // Class ID (for type identification)
  open func getClass() -> String {
    return object.classId
  }
}

// Base class for all NPRPC proxy objects
// Mirrors C++ nprpc::Object functionality

open class NPRPCObjectProxy {
  public let object: NPRPCObject
  
  public init(_ object: NPRPCObject) {
    self.object = object
  }
  
  // Reference counting
  public func addRef() -> UInt32 {
    // TODO: Implement actual reference counting
    return 1
  }
  
  public func release() -> UInt32 {
    // TODO: Implement actual reference counting
    return 0
  }
  
  // Timeout management
  public func setTimeout(_ timeoutMs: UInt32) -> UInt32 {
    let oldTimeout = object.timeout
    object.timeout = timeoutMs
    return oldTimeout
  }
  
  public func getTimeout() -> UInt32 {
    return object.timeout
  }
  
  // Endpoint access
  public func getEndpoint() -> NPRPCEndpoint {
    return object.endpoint
  }
  
  // Select endpoint (returns true if successful)
  public func selectEndpoint(_ remoteEndpoint: NPRPCEndpoint? = nil) -> Bool {
    // TODO: Implement endpoint selection logic
    if let endpoint = remoteEndpoint {
      object.endpoint = endpoint
      return true
    }
    return true
  }
  
  // Object ID access
  public func getObjectId() -> UInt64 {
    return object.objectId
  }
  
  public func getPoaIdx() -> UInt16 {
    return object.poaIdx
  }
  
  // Class ID (for type identification)
  open func getClass() -> String {
    // Override in subclasses
    return ""
  }
}

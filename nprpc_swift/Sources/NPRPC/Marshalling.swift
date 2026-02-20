// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

// Marshalling helper functions for generated Swift code

import Foundation

// MARK: - Vector Marshalling

public func marshal_fundamental_vector<T>(buffer: FlatBuffer, offset: Int, vector: [T]) {
    let elementSize = MemoryLayout<T>.stride
    let alignment = MemoryLayout<T>.alignment
    let dataOffset = _alloc(buffer: buffer, vectorOffset: offset, count: vector.count, elementSize: elementSize, align: alignment)

    if vector.count > 0 {
        // Get fresh pointer after allocation
        guard let data = buffer.data else { return }
        vector.withUnsafeBytes { bytes in
            data.advanced(by: dataOffset).copyMemory(
                from: bytes.baseAddress!,
                byteCount: vector.count * elementSize
            )
        }
    }
}

public func unmarshal_fundamental_vector<T>(buffer: UnsafeRawPointer, offset: Int) -> [T] {
    let dataOffset = Int(buffer.load(fromByteOffset: offset + 0, as: UInt32.self)) + offset
    let count = Int(buffer.load(fromByteOffset: offset + 4, as: UInt32.self))
    guard count > 0 else { return [] }

    let elementSize = MemoryLayout<T>.stride
    var result: [T] = []
    result.reserveCapacity(count)

    for i in 0..<count {
        let element = buffer.load(fromByteOffset: dataOffset + i * elementSize, as: T.self)
        result.append(element)
    }

    return result
}

public func marshal_struct_vector<T>(
    buffer: FlatBuffer,
    offset: Int,
    vector: [T],
    elementSize: Int,
    elementAlignment: Int,
    marshalElement: (FlatBuffer, Int, T) -> Void
) {
    let dataOffset = _alloc(buffer: buffer, vectorOffset: offset, count: vector.count, elementSize: elementSize, align: elementAlignment)

    if vector.count > 0 {
        for (index, element) in vector.enumerated() {
            marshalElement(buffer, dataOffset + index * elementSize, element)
        }
    }
}

public func unmarshal_struct_vector<T>(
    buffer: UnsafeRawPointer,
    offset: Int,
    elementSize: Int,
    unmarshalElement: (UnsafeRawPointer, Int) -> T
) -> [T] {
    let dataOffset = Int(buffer.load(fromByteOffset: offset + 0, as: UInt32.self)) + offset
    let n = buffer.load(fromByteOffset: offset + 4, as: UInt32.self)
    guard n != 0 else { return [] }

    var result: [T] = []
    result.reserveCapacity(Int(n))
    
    for i in 0..<Int(n) {
        let element = unmarshalElement(buffer, dataOffset + i * elementSize)
        result.append(element)
    }

    return result
}

// MARK: - Array Marshalling

public func marshal_fundamental_array<T>(buffer: FlatBuffer, offset: Int, array: [T], count: Int) {
    guard let data = buffer.data else { return }
    array.withUnsafeBytes { bytes in
        data.advanced(by: offset).copyMemory(
            from: bytes.baseAddress!,
            byteCount: count * MemoryLayout<T>.stride
        )
    }
}

public func unmarshal_fundamental_array<T>(buffer: UnsafeRawPointer, offset: Int, count: Int) -> [T] {
    let pointer = buffer.advanced(by: offset).assumingMemoryBound(to: T.self)
    return Array(UnsafeBufferPointer(start: pointer, count: count))
}

// MARK: - Optional Marshalling

public func marshal_optional_fundamental<T>(buffer: FlatBuffer, offset: Int, value: T) {
    let elementSize = MemoryLayout<T>.stride
    let alignment = MemoryLayout<T>.alignment

    // Allocate space and get absolute offset where value will be stored
    let dataOffset = _alloc1(buffer: buffer, flatOffset: offset, elementSize: elementSize, align: alignment)

    // Write the value at the allocated location - get fresh pointer after allocation
    guard let data = buffer.data else { return }
    data.storeBytes(of: value, toByteOffset: dataOffset, as: T.self)
}

public func marshal_optional_struct<T>(
    buffer: FlatBuffer,
    offset: Int,
    value: T,
    marshalFunc: (FlatBuffer, Int) -> Void
) {
    // For structs, estimate size - the marshal function will extend if needed
    let dataOffset = _alloc1(buffer: buffer, flatOffset: offset, elementSize: 128, align: 4)

    marshalFunc(buffer, dataOffset)
}

public func unmarshal_optional_fundamental<T>(buffer: UnsafeRawPointer, offset: Int) -> T? {
    // Read the relative offset
    let relativeOffset = buffer.load(fromByteOffset: offset, as: UInt32.self)
    guard relativeOffset != 0 else { return nil }

    // Calculate absolute offset and read value
    let dataOffset = offset + Int(relativeOffset)
    return buffer.load(fromByteOffset: dataOffset, as: T.self)
}

public func unmarshal_optional_struct<T>(
    buffer: UnsafeRawPointer,
    offset: Int,
    unmarshalFunc: (UnsafeRawPointer, Int) -> T
) -> T? {
    // Read the relative offset
    let relativeOffset = buffer.load(fromByteOffset: offset, as: UInt32.self)
    guard relativeOffset != 0 else { return nil }
    
    // Calculate absolute offset and unmarshal
    let dataOffset = offset + Int(relativeOffset)
    return unmarshalFunc(buffer, dataOffset)
}

// MARK: - String Marshalling

public func marshal_string(buffer: FlatBuffer, offset: Int, string: String) {
    let utf8 = Array(string.utf8)
    let dataOffset = _alloc(buffer: buffer, vectorOffset: offset, count: utf8.count, elementSize: 1, align: 1)

    if utf8.count > 0 {
        guard let data = buffer.data else { return }
        utf8.withUnsafeBytes { bytes in
            data.advanced(by: dataOffset).copyMemory(
                from: bytes.baseAddress!,
                byteCount: utf8.count
            )
        }
    }
}

public func unmarshal_string(buffer: UnsafeRawPointer, offset: Int) -> String {
    let dataOffset = Int(buffer.load(fromByteOffset: offset, as: UInt32.self)) + offset
    let count = Int(buffer.load(fromByteOffset: offset + 4, as: UInt32.self))
    guard count > 0 else { return "" }

    let dataPtr = buffer.advanced(by: dataOffset)
    let data = Data(bytes: dataPtr, count: count)
    return String(data: data, encoding: .utf8) ?? ""
}

// MARK: - ObjectId Marshalling

let marshal_object_id = detail.marshal_ObjectId

// MARK: - Span/View helpers for compatibility

public struct Span<T> {
    public let pointer: UnsafePointer<T>
    public let count: Int
    
    public init(_ array: [T]) {
        self.pointer = array.withUnsafeBufferPointer { $0.baseAddress! }
        self.count = array.count
    }
}

// MARK: - NPRPCObject marshalling

/// Unmarshal a remote object reference from flat buffer
/// This is the Swift equivalent of C++ create_object_from_flat
/// Pass the raw buffer pointer directly to C++ to avoid data duplication
public func unmarshal_object_proxy(buffer: UnsafeRawPointer, offset: Int, endpoint: NPRPCEndpoint) throws -> NPRPCObject {
    // Call the C++ bridge function that handles all the logic
    // It creates a view-mode flat_buffer and ObjectId_Direct on the stack
    var objectHandle: UnsafeMutableRawPointer? = nil
    let result = nprpc_create_object_from_flat(
        UnsafeMutableRawPointer(mutating: buffer),  // Cast away const - C++ won't modify
        UInt32(offset),
        UInt32(endpoint.type.rawValue),
        endpoint.hostname,
        endpoint.port,
        &objectHandle
    )
    
    switch result {
    case 0:
        // Success but null object (invalid_object_id)
        return NPRPCObject()
    case 1:
        // Success with valid object
        guard let handle = objectHandle else {
            throw RuntimeError(message: "Bridge returned success but null handle")
        }
        return NPRPCObject(handle: handle)
    case -3:
        throw RuntimeError(message: "Failed to select endpoint for object, endpoint: \(endpoint.toURL())")
    default:
        throw RuntimeError(message: "Failed to create object from flat buffer, error code: \(result)")
    }
}

// MARK: - Safety Checks for Untrusted Input

/// Check that a struct at the given offset fits within the buffer
/// - Parameters:
///   - bufferSize: Total buffer size in bytes
///   - offset: Offset where struct starts
///   - structSize: Expected size of the struct
/// - Returns: true if valid, false if out of bounds
@inline(__always)
public func check_struct_bounds(bufferSize: Int, offset: Int, structSize: Int) -> Bool {
    return offset >= 0 && structSize >= 0 && offset + structSize <= bufferSize
}

/// Check that a vector/string field has valid bounds
/// The first 4 bytes at offset contain the relative data offset (from offset)
/// The next 4 bytes contain the count
/// - Parameters:
///   - buffer: Raw buffer pointer
///   - bufferSize: Total buffer size in bytes  
///   - offset: Offset where vector header starts
///   - elementSize: Size of each element
/// - Returns: true if valid, false if out of bounds
@inline(__always)
public func check_vector_bounds(buffer: UnsafeRawPointer, bufferSize: Int, offset: Int, elementSize: Int) -> Bool {
    // Check header is within bounds (8 bytes for offset + count)
    guard offset >= 0 && offset + 8 <= bufferSize else { return false }
    
    // Read the relative offset and count
    let relativeOffset = Int(buffer.load(fromByteOffset: offset, as: UInt32.self))
    let count = Int(buffer.load(fromByteOffset: offset + 4, as: UInt32.self))
    
    // Empty vector is always valid
    guard count > 0 else { return true }
    
    // Calculate absolute data offset
    let dataOffset = offset + relativeOffset
    
    // Check data region is within bounds
    // Guard against integer overflow
    guard dataOffset >= 0 && elementSize >= 0 else { return false }
    let dataSize = count * elementSize
    guard dataSize / elementSize == count else { return false }  // Overflow check
    guard dataOffset + dataSize <= bufferSize else { return false }
    
    return true
}

/// Check that a string field has valid bounds
/// Strings are stored like vectors of UInt8
@inline(__always)
public func check_string_bounds(buffer: UnsafeRawPointer, bufferSize: Int, offset: Int) -> Bool {
    return check_vector_bounds(buffer: buffer, bufferSize: bufferSize, offset: offset, elementSize: 1)
}

/// Check that an optional field has valid bounds
/// Optionals have a 4-byte presence flag, followed by the relative data offset
/// - Parameters:
///   - buffer: Raw buffer pointer
///   - bufferSize: Total buffer size in bytes
///   - offset: Offset where optional header starts
///   - valueSize: Size of the optional value (when present)
/// - Returns: true if valid, false if out of bounds
@inline(__always)
public func check_optional_bounds(buffer: UnsafeRawPointer, bufferSize: Int, offset: Int, valueSize: Int) -> Bool {
    // Check header is within bounds (4 bytes for relative offset)
    guard offset >= 0 && offset + 4 <= bufferSize else { return false }
    
    // Read the relative offset (0 means no value)
    let relativeOffset = Int(buffer.load(fromByteOffset: offset, as: UInt32.self))
    
    // No value means valid
    guard relativeOffset != 0 else { return true }
    
    // Calculate absolute data offset
    let dataOffset = offset + relativeOffset
    
    // Check value is within bounds
    guard dataOffset >= 0 && valueSize >= 0 else { return false }
    guard dataOffset + valueSize <= bufferSize else { return false }
    
    return true
}

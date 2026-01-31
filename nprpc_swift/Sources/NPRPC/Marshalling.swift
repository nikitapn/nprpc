// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

// Marshalling helper functions for generated Swift code

import Foundation

// MARK: - Vector Marshalling

public func marshal_fundamental_vector<T>(buffer: UnsafeMutableRawPointer, offset: Int, vector: [T]) {
    let count = UInt32(vector.count)
    buffer.storeBytes(of: count, toByteOffset: offset, as: UInt32.self)
    
    if count > 0 {
        let elementSize = MemoryLayout<T>.stride
        let dataOffset = offset + 4
        vector.withUnsafeBytes { bytes in
            buffer.advanced(by: dataOffset).copyMemory(
                from: bytes.baseAddress!,
                byteCount: Int(count) * elementSize
            )
        }
    }
}

public func marshal_struct_vector<T>(
    buffer: UnsafeMutableRawPointer,
    offset: Int,
    vector: [T],
    elementSize: Int,
    marshalElement: (UnsafeMutableRawPointer, Int, T) -> Void
) {
    let count = UInt32(vector.count)
    buffer.storeBytes(of: count, toByteOffset: offset, as: UInt32.self)
    
    if count > 0 {
        let dataOffset = offset + 4
        for (index, element) in vector.enumerated() {
            marshalElement(buffer, dataOffset + index * elementSize, element)
        }
    }
}

public func unmarshal_fundamental_vector<T>(buffer: UnsafeRawPointer, offset: Int) -> [T] {
    let count = buffer.load(fromByteOffset: offset, as: UInt32.self)
    guard count > 0 else { return [] }
    
    let elementSize = MemoryLayout<T>.stride
    let dataOffset = offset + 4
    var result: [T] = []
    result.reserveCapacity(Int(count))
    
    for i in 0..<Int(count) {
        let element = buffer.load(fromByteOffset: dataOffset + i * elementSize, as: T.self)
        result.append(element)
    }
    
    return result
}

public func unmarshal_struct_vector<T>(
    buffer: UnsafeRawPointer,
    offset: Int,
    elementSize: Int,
    unmarshalElement: (UnsafeRawPointer, Int) -> T
) -> [T] {
    let count = buffer.load(fromByteOffset: offset, as: UInt32.self)
    guard count > 0 else { return [] }
    
    let dataOffset = offset + 4
    var result: [T] = []
    result.reserveCapacity(Int(count))
    
    for i in 0..<Int(count) {
        let element = unmarshalElement(buffer, dataOffset + i * elementSize)
        result.append(element)
    }
    
    return result
}

// MARK: - Array Marshalling

public func marshal_fundamental_array<T>(buffer: UnsafeMutableRawPointer, offset: Int, array: [T]) {
    array.withUnsafeBytes { bytes in
        buffer.advanced(by: offset).copyMemory(
            from: bytes.baseAddress!,
            byteCount: array.count * MemoryLayout<T>.stride
        )
    }
}

public func unmarshal_fundamental_array<T>(buffer: UnsafeRawPointer, offset: Int, count: Int) -> [T] {
    let pointer = buffer.advanced(by: offset).assumingMemoryBound(to: T.self)
    return Array(UnsafeBufferPointer(start: pointer, count: count))
}

// MARK: - Optional Marshalling

public func marshal_optional_fundamental<T>(buffer: UnsafeMutableRawPointer, offset: Int, value: T) {
    buffer.storeBytes(of: UInt32(1), toByteOffset: offset, as: UInt32.self)
    buffer.storeBytes(of: value, toByteOffset: offset + 4, as: T.self)
}

public func marshal_optional_struct<T>(
    buffer: UnsafeMutableRawPointer,
    offset: Int,
    value: T,
    marshalFunc: (UnsafeMutableRawPointer, Int) -> Void
) {
    buffer.storeBytes(of: UInt32(1), toByteOffset: offset, as: UInt32.self)
    marshalFunc(buffer, offset + 4)
}

public func unmarshal_optional_fundamental<T>(buffer: UnsafeRawPointer, offset: Int) -> T? {
    let hasValue = buffer.load(fromByteOffset: offset, as: UInt32.self)
    guard hasValue != 0 else { return nil }
    return buffer.load(fromByteOffset: offset + 4, as: T.self)
}

public func unmarshal_optional_struct<T>(
    buffer: UnsafeRawPointer,
    offset: Int,
    unmarshalFunc: (UnsafeRawPointer, Int) -> T
) -> T? {
    let hasValue = buffer.load(fromByteOffset: offset, as: UInt32.self)
    guard hasValue != 0 else { return nil }
    return unmarshalFunc(buffer, offset + 4)
}

// MARK: - String Marshalling

public func marshal_string(buffer: UnsafeMutableRawPointer, offset: Int, string: String) {
    let utf8 = Array(string.utf8)
    let count = UInt32(utf8.count)
    buffer.storeBytes(of: count, toByteOffset: offset, as: UInt32.self)
    
    if count > 0 {
        utf8.withUnsafeBytes { bytes in
            buffer.advanced(by: offset + 4).copyMemory(
                from: bytes.baseAddress!,
                byteCount: Int(count)
            )
        }
    }
}

public func unmarshal_string(buffer: UnsafeRawPointer, offset: Int) -> String {
    let count = buffer.load(fromByteOffset: offset, as: UInt32.self)
    guard count > 0 else { return "" }
    
    let dataPtr = buffer.advanced(by: offset + 4)
    let data = Data(bytes: dataPtr, count: Int(count))
    return String(data: data, encoding: .utf8) ?? ""
}

// MARK: - ObjectId Marshalling

/// Marshal an object ID to a buffer
/// This is a wrapper that calls the generated detail.marshal_ObjectId
// public func marshal_object_id(buffer: UnsafeMutableRawPointer, offset: Int, objectId: NPRPCObjectData) {
//     let oid = detail.ObjectId(
//         object_id: objectId.objectId,
//         poa_idx: objectId.poaIdx,
//         flags: objectId.flags,
//         origin: objectId.origin,
//         class_id: objectId.classId,
//         urls: objectId.urls
//     )
//     detail.marshal_ObjectId(buffer: buffer, offset: offset, data: oid)
// }

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

// MARK: - Object reference marshalling

/// Base protocol for all NPRPC interface types
public protocol Object {}

/// Unmarshal a remote object reference
/// TODO: Implement actual object proxy creation
public func unmarshal_object_proxy(buffer: UnsafeRawPointer, offset: Int, endpoint: NPRPCEndpoint) -> ObjectPtr<Object> {
    // Use the generated unmarshal function
    let data = detail.unmarshal_ObjectId(buffer: buffer, offset: offset)
    return ObjectPtr<Object>(data: data)
}

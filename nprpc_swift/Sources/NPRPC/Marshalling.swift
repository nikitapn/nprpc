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

public func marshal_object_id(buffer: UnsafeMutableRawPointer, offset: Int, objectId: NPRPCObjectData) {
    buffer.storeBytes(of: objectId.objectId, toByteOffset: offset + 0, as: UInt64.self)
    buffer.storeBytes(of: objectId.poaIdx, toByteOffset: offset + 8, as: UInt16.self)
    buffer.storeBytes(of: objectId.flags, toByteOffset: offset + 10, as: UInt16.self)
    
    // Origin (UUID - 16 bytes)
    objectId.origin.withUnsafeBytes { bytes in
        buffer.advanced(by: offset + 12).copyMemory(
            from: bytes.baseAddress!,
            byteCount: 16
        )
    }
    
    // Class ID (string)
    marshal_string(buffer: buffer, offset: offset + 28, string: objectId.classId)
    
    // URLs (string) - offset depends on class_id length
    let classIdLen = UInt32(objectId.classId.utf8.count)
    let urlsOffset = offset + 28 + 4 + Int(classIdLen)
    marshal_string(buffer: buffer, offset: urlsOffset, string: objectId.urls)
}

// MARK: - Span/View helpers for compatibility

public struct Span<T> {
    public let pointer: UnsafePointer<T>
    public let count: Int
    
    public init(_ array: [T]) {
        self.pointer = array.withUnsafeBufferPointer { $0.baseAddress! }
        self.count = array.count
    }
}

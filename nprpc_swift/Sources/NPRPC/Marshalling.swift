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

/// Unmarshal a remote object reference
public func unmarshal_object_proxy(buffer: UnsafeRawPointer, offset: Int, endpoint: NPRPCEndpoint) -> NPRPCObject {
    // Use the generated unmarshal function
    let data = detail.unmarshal_ObjectId(buffer: buffer, offset: offset)
    return NPRPCObject.fromObjectId(data)!
}

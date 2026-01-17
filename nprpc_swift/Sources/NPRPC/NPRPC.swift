// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

// Main module for NPRPC Swift bindings
// Re-exports all public types and functions

// Export C++ bridge module
@_exported import CNprpc

// Export Swift wrappers
// These are now in separate files for better organization

/// Get the NPRPC library version
public func version() -> String {
    return String(cString: nprpc_swift.get_version())
}

// ============================================================================
// MARK: - Basic Interop Tests (for POC)
// ============================================================================

/// Test basic C++ interop - add two numbers
public func testAdd(_ a: Int32, _ b: Int32) -> Int32 {
    return nprpc_swift.add_numbers(a, b)
}

/// Test string passing to C++
public func testGreet(_ name: String) -> String {
    // Convert Swift String to std::string for C++ interop
    let cxxName = std.string(name)
    let result = nprpc_swift.greet(cxxName)
    return String(cString: nprpc_swift.string_to_cstr(result))
}

/// Test array return from C++
public func testArray() -> [Int32] {
    let vec = nprpc_swift.get_test_array()
    var result: [Int32] = []
    for i in 0..<vec.size() {
        result.append(vec[i])
    }
    return result
}

// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

import XCTest
import CNprpc
@testable import NPRPC

// Note: Full RPC/POA tests require running server
// These are marshalling-only tests for now

final class BasicTests: XCTestCase {
    
    func testFlatBufferOperations() throws {
        // Test FlatBuffer wrapper
        let buffer = FlatBuffer()
        
        // Test size
        let initialSize = buffer.size
        XCTAssertEqual(initialSize, 0, "New buffer should be empty")
        
        // Test prepare/commit
        buffer.prepare(1024)
        buffer.commit(512)
        XCTAssertEqual(buffer.size, 512, "Buffer size should be 512 after commit")
        
        // Test data access
        let data = buffer.data
        XCTAssertNotNil(data, "Buffer data should not be nil")
        
        // Test consume
        buffer.consume(256)
        XCTAssertEqual(buffer.size, 256, "Buffer size should be 256 after consuming 256 bytes")
        
        print("✓ FlatBuffer operations working correctly")
    }
    
    func testPointMarshalling() throws {
        #if canImport(swift_test)
        let point = swift_test_Point(x: 3.14, y: 2.71)
        
        let buffer = FlatBuffer()
        buffer.prepare(1024)
        
        // Marshal point
        buffer.withMutableBytes { ptr in
            swift_test_marshal_Point(buffer: ptr, offset: 0, data: point)
        }
        
        buffer.commit(8) // Point is 2 floats = 8 bytes
        
        XCTAssertEqual(buffer.size, 8, "Point should be 8 bytes")
        
        // Verify marshalled data
        buffer.withConstBytes { ptr in
            let x = ptr.load(fromByteOffset: 0, as: Float.self)
            let y = ptr.load(fromByteOffset: 4, as: Float.self)
            XCTAssertEqual(x, 3.14, accuracy: 0.001)
            XCTAssertEqual(y, 2.71, accuracy: 0.001)
        }
        
        print("✓ Point marshalling completed and verified")
        #else
        print("⚠ Generated swift_test code not available")
        #endif
    }
    
    func testRectangleMarshalling() throws {
        #if canImport(swift_test)
        let topLeft = swift_test_Point(x: 10, y: 20)
        let rect = swift_test_Rectangle(top_left: topLeft, width: 100, height: 50)
        
        let buffer = FlatBuffer()
        buffer.prepare(1024)
        
        // Marshal rectangle
        buffer.withMutableBytes { ptr in
            swift_test_marshal_Rectangle(buffer: ptr, offset: 0, data: rect)
        }
        
        buffer.commit(16) // Rectangle is 4 floats = 16 bytes
        
        XCTAssertEqual(buffer.size, 16, "Rectangle should be 16 bytes")
        
        // Verify marshalled data
        buffer.withConstBytes { ptr in
            let x = ptr.load(fromByteOffset: 0, as: Float.self)
            let y = ptr.load(fromByteOffset: 4, as: Float.self)
            let w = ptr.load(fromByteOffset: 8, as: Float.self)
            let h = ptr.load(fromByteOffset: 12, as: Float.self)
            XCTAssertEqual(x, 10, accuracy: 0.001)
            XCTAssertEqual(y, 20, accuracy: 0.001)
            XCTAssertEqual(w, 100, accuracy: 0.001)
            XCTAssertEqual(h, 50, accuracy: 0.001)
        }
        
        print("✓ Rectangle marshalling completed and verified")
        #else
        print("⚠ Generated swift_test code not available")
        #endif
    }
    
    func testObjectIdAccessors() throws {
        // Test that ObjectId accessor functions are available
        // (actual activation requires full RPC setup)
        let functions: [(String, Any)] = [
            ("nprpc_objectid_get_object_id", nprpc_objectid_get_object_id as Any),
            ("nprpc_objectid_get_poa_idx", nprpc_objectid_get_poa_idx as Any),
            ("nprpc_objectid_get_flags", nprpc_objectid_get_flags as Any),
            ("nprpc_objectid_get_class_id", nprpc_objectid_get_class_id as Any),
            ("nprpc_objectid_get_urls", nprpc_objectid_get_urls as Any),
            ("nprpc_objectid_get_origin", nprpc_objectid_get_origin as Any),
            ("nprpc_objectid_destroy", nprpc_objectid_destroy as Any),
        ]
        
        for (name, _) in functions {
            print("✓ Bridge function available: \(name)")
        }
    }
}

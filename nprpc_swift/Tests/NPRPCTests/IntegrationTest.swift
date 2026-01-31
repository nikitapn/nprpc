// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

// Integration test demonstrating Swift servant implementation
// Note: Full RPC roundtrip testing requires servants to be activated in a POA
// and connected to real RPC transport - that's beyond the scope of unit tests

import XCTest
import Foundation
@testable import NPRPC

final class IntegrationTests: XCTestCase {
    /// Document what full integration testing would look like
    func testIntegrationTestPlan() throws {
        // Note: ShapeService is a client proxy that requires an NPRPCObject
        // which is obtained from the nameserver or endpoint connection.
        // Full integration testing requires a running server.
        // For now, we just verify the types compile correctly.
        
        // Test that servant can be instantiated
        class TestShapeServant: ShapeServiceServant {
            override func getRectangle(id: UInt32) throws -> Rectangle {
                return Rectangle(topLeft: Point(x: 0, y: 0), bottomRight: Point(x: 100, y: 100), color: .blue)
            }
            
            override func setRectangle(id: UInt32, rect: Rectangle) throws {
                // Store rectangle (no-op for test)
            }
        }
        
        let servant = TestShapeServant()
        XCTAssertNotNil(servant)
        
        // Test that servant methods work
        let rect = try servant.getRectangle(id: 1)
        XCTAssertEqual(rect.topLeft.x, 0)
        XCTAssertEqual(rect.bottomRight.x, 100)
        XCTAssertEqual(rect.color, .blue)

        // let shapeService = ShapeService(
        //     NPRPCObject(
        //         objectId: 1,
        //         poaIdx: 0,
        //         flags: 0,
        //         origin: [UInt8](repeating: 0, count: 16),
        //         classId: "ShapeService",
        //         urls: "tcp://localhost:12345"
        //     ))

    }
}

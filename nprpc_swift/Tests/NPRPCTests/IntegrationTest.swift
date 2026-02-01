// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

// Integration test demonstrating Swift servant implementation and POA activation

import XCTest
import Foundation
@testable import NPRPC

final class IntegrationTests: XCTestCase {
    
    /// Test servant instantiation and direct method calls
    func testServantDirectCalls() throws {
        class TestShapeServant: ShapeServiceServant {
            var storedRect: Rectangle?
            
            override func getRectangle(id: UInt32) throws -> Rectangle {
                return Rectangle(
                    topLeft: Point(x: 10, y: 20),
                    bottomRight: Point(x: 110, y: 120),
                    color: .green
                )
            }
            
            override func setRectangle(id: UInt32, rect: Rectangle) throws {
                storedRect = rect
            }
        }

        let rpc = try RpcBuilder()
            .setLogLevel(.trace)
            .setHostname("127.0.0.1")
            .withTcp(16000)
            .build()

        // Give the TCP listener time to start accepting connections
        Thread.sleep(forTimeInterval: 0.1)

        let servant = TestShapeServant()

        let poa = try rpc.createPoa(maxObjects: 100)
        let oid = try poa.activateObject(servant)

        // Create ShapeService client proxy from oid
        guard let obj = NPRPCObject.fromObjectId(oid) else {
            XCTFail("Failed to create NPRPCObject from ObjectId")
            return
        }
        
        let client = ShapeService(obj)
        
        // Verify the class ID matches
        XCTAssertEqual(client.getClass(), "basic_test/swift.test.ShapeService")
        XCTAssertEqual(oid.class_id, "basic_test/swift.test.ShapeService")
        
        // Actually call RPC methods through the client proxy
        // This tests the full serialization/dispatch/deserialization cycle
        
        // Test getRectangle - should return the rectangle from our servant
        let rect = try client.getRectangle(id: 42)
        XCTAssertEqual(rect.topLeft.x, 10)
        XCTAssertEqual(rect.topLeft.y, 20)
        XCTAssertEqual(rect.bottomRight.x, 110)
        XCTAssertEqual(rect.bottomRight.y, 120)
        XCTAssertEqual(rect.color, .green)
        
        // Test setRectangle - should store the rectangle in our servant
        let newRect = Rectangle(
            topLeft: Point(x: 5, y: 15),
            bottomRight: Point(x: 50, y: 150),
            color: .blue
        )
        try client.setRectangle(id: 99, rect: newRect)
        
        // Verify the servant received the call
        XCTAssertNotNil(servant.storedRect)
        XCTAssertEqual(servant.storedRect?.topLeft.x, 5)
        XCTAssertEqual(servant.storedRect?.topLeft.y, 15)
        XCTAssertEqual(servant.storedRect?.color, .blue)
        
        print("✓ Full RPC loopback test successful!")
    }
}

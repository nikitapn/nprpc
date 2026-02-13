// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

// Integration test demonstrating Swift servant implementation and POA activation

import XCTest
import Foundation
@testable import NPRPC

final class IntegrationTests: XCTestCase {
    nonisolated(unsafe) static var rpc: Rpc?
    nonisolated(unsafe) static var poa: Poa?

    override class func setUp() {
        super.setUp()
        print("Setting up integration tests (class-wide)...")
        do {
            rpc = try RpcBuilder()
                .setLogLevel(.trace)
                .setHostname("127.0.0.1")
                .withTcp(16000)
                .build()
            // Give the TCP listener time to start accepting connections
            Thread.sleep(forTimeInterval: 0.1)
            poa = try rpc!.createPoa(maxObjects: 100)
        } catch {
            fatalError("Failed to set up test environment: \(error)")
        }
    }

    override class func tearDown() {
        print("Tearing down integration tests (class-wide)...")
        // Rpc/Poa will be cleaned up when deinit is called
        rpc = nil
        poa = nil
        super.tearDown()
    }

    func test1() throws {
        // Placeholder test to ensure test discovery works
        XCTAssertTrue(true)
    }
    
    /// Test servant instantiation and direct method calls
    func testServantDirectCalls() throws {
        class TestShapeServant: ShapeServiceServant {
            var storedRect: Rectangle?
            
            override func getRectangle(id: UInt32) throws -> Rectangle {
                return Rectangle(
                    topLeft: Point(x: 10, y: 20, z: nil, symbol: ""),
                    bottomRight: Point(x: 110, y: 120, z: nil, symbol: ""),
                    color: .green
                )
            }
            
            override func setRectangle(id: UInt32, rect: Rectangle) throws {
                storedRect = rect
            }

            override func getRectangles() throws -> [Rectangle] {
                return [
                    Rectangle(
                        topLeft: Point(x: 1, y: 2, z: nil, symbol: "a"),
                        bottomRight: Point(x: 3, y: 4, z: nil, symbol: "b"),
                        color: .red
                    ),
                    Rectangle(
                        topLeft: Point(x: 5, y: 6, z: nil, symbol: "c"),
                        bottomRight: Point(x: 7, y: 8, z: nil, symbol: "abcd"),
                        color: .blue
                    )
                ]
            }

            override func getNumbers() throws -> [Int32] {
                return [42, 99, -7]
            }

            override func throwingMethod(code: UInt32) throws {
                throw TestException(__ex_id: 0, message: "Test error \(code)", code: code)
            }
        }

        let servant = TestShapeServant()
        let oid = try Self.poa!.activateObject(servant)

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
            topLeft: Point(x: 5, y: 15, z: 33, symbol: "Abcd"),
            bottomRight: Point(x: 40, y: 150, z: nil, symbol: ""),
            color: .blue
        )

        try client.setRectangle(id: 99, rect: newRect)
        XCTAssertNotNil(servant.storedRect)
        XCTAssertEqual(servant.storedRect?.topLeft.x, 5)
        XCTAssertEqual(servant.storedRect?.topLeft.y, 15)
        XCTAssertEqual(servant.storedRect?.topLeft.z, 33)
        XCTAssertEqual(servant.storedRect?.topLeft.symbol, "Abcd")
        XCTAssertEqual(servant.storedRect?.bottomRight.x, 40)
        XCTAssertEqual(servant.storedRect?.bottomRight.y, 150)
        XCTAssertEqual(servant.storedRect?.bottomRight.z, nil)
        XCTAssertEqual(servant.storedRect?.bottomRight.symbol, "")
        XCTAssertEqual(servant.storedRect?.color, .blue)

        // Test getRectangles - should return the array of rectangles from our servant
        let rects = try client.getRectangles()
        XCTAssertEqual(rects.count, 2)
        XCTAssertEqual(rects[0].topLeft.x, 1)
        XCTAssertEqual(rects[0].topLeft.y, 2)
        XCTAssertEqual(rects[0].topLeft.z, nil)
        XCTAssertEqual(rects[0].topLeft.symbol, "a")
        XCTAssertEqual(rects[0].bottomRight.x, 3)
        XCTAssertEqual(rects[0].bottomRight.y, 4)
        XCTAssertEqual(rects[0].bottomRight.z, nil) 
        XCTAssertEqual(rects[0].bottomRight.symbol, "b")
        XCTAssertEqual(rects[0].color, .red)

        XCTAssertEqual(rects[1].topLeft.x, 5)
        XCTAssertEqual(rects[1].topLeft.y, 6)
        XCTAssertEqual(rects[1].topLeft.z, nil)
        XCTAssertEqual(rects[1].topLeft.symbol, "c")
        XCTAssertEqual(rects[1].bottomRight.x, 7)
        XCTAssertEqual(rects[1].bottomRight.y, 8)
        XCTAssertEqual(rects[1].bottomRight.z, nil)
        XCTAssertEqual(rects[1].bottomRight.symbol, "abcd")
        XCTAssertEqual(rects[1].color, .blue)

        // Test getNumbers - should return the array of integers from our servant
        let nums = try client.getNumbers()
        XCTAssertEqual(nums, [42, 99, -7])
    }

    /// Test exception handling through RPC
    func testExceptionHandling() throws {
        class ExceptionServant: ShapeServiceServant {
            override func getRectangle(id: UInt32) throws -> Rectangle {
                return Rectangle()
            }
            override func setRectangle(id: UInt32, rect: Rectangle) throws {}
            override func getRectangles() throws -> [Rectangle] { return [] }
            override func getNumbers() throws -> [Int32] { return [] }
            override func throwingMethod(code: UInt32) throws {
                throw TestException(__ex_id: 0, message: "Error with code \(code)", code: code)
            }
        }

        let servant = ExceptionServant()
        let oid = try Self.poa!.activateObject(servant)
        guard let obj = NPRPCObject.fromObjectId(oid) else {
            XCTFail("Failed to create NPRPCObject from ObjectId")
            return
        }
        let client = ShapeService(obj)

        // Calling throwingMethod should throw a TestException
        do {
            try client.throwingMethod(code: 42)
            XCTFail("Expected TestException to be thrown")
        } catch let e as TestException {
            XCTAssertEqual(e.code, 42)
            XCTAssertEqual(e.message, "Error with code 42")
        } catch {
            XCTFail("Expected TestException but got: \(error)")
        }

        // Test with different code
        do {
            try client.throwingMethod(code: 99)
            XCTFail("Expected TestException to be thrown")
        } catch let e as TestException {
            XCTAssertEqual(e.code, 99)
            XCTAssertEqual(e.message, "Error with code 99")
        } catch {
            XCTFail("Expected TestException but got: \(error)")
        }
    }
}

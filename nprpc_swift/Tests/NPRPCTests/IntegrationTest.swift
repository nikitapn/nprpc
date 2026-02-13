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
            
            override func getRectangle(id: UInt32) -> Rectangle {
                return Rectangle(
                    topLeft: Point(x: 10, y: 20, z: nil, symbol: ""),
                    bottomRight: Point(x: 110, y: 120, z: nil, symbol: ""),
                    color: .green
                )
            }
            
            override func setRectangle(id: UInt32, rect: Rectangle) {
                storedRect = rect
            }

            override func getRectangles() -> [Rectangle] {
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

            override func getNumbers() -> [Int32] {
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
            override func getRectangle(id: UInt32) -> Rectangle {
                return Rectangle()
            }
            override func setRectangle(id: UInt32, rect: Rectangle) {}
            override func getRectangles() -> [Rectangle] { return [] }
            override func getNumbers() -> [Int32] { return [] }
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

    func testBasicTypes() throws {
        // nprpc_test.npidl defines a TestBasic interface with methods that use various basic types and arrays.
        class TestBasicServantImpl: TestBasicServant {
            var in_receivedA: UInt32 = 0
            var in_receivedB: Bool = false
            var in_receivedC: [UInt8] = []
            var inStruct_receivedA: AAA = AAA()
            var inFlatStruct_receivedValue: UInt32 = 0
            var inFlatStruct_receivedA: FlatStruct = FlatStruct()
            var outFlatStruct_receivedValue: UInt32 = 0
            var outScalarWithException_receivedDevAddr: UInt8 = 0
            var outScalarWithException_receivedAddr: UInt16 = 0
            
            override func returnBoolean() throws -> Bool {
                return true
            }

            override func returnIdArray() throws -> IdArray {
                return [1, 2, 3, 4, 5]
            }

            override func returnU32() throws -> UInt32 {
                return 123456789
            }

            override func in_(a: UInt32, b: Bool, c: [UInt8]) throws -> Bool {
                in_receivedA = a
                in_receivedB = b
                in_receivedC = c
                return true
            }

            override func out() throws -> (UInt32, Bool, [UInt8]) {
                return (123456789, true, [1, 2, 3, 4, 5])
            }

            override func inStruct(a: AAA) throws {
                inStruct_receivedA = a
            }

            override func outStruct() throws -> AAA {
                return AAA(a: 1234, b: "Hello world", c: "Another string")
            }

            override func inFlatStruct(value: UInt32, a: FlatStruct) throws {
                inFlatStruct_receivedValue = value
                inFlatStruct_receivedA = a
            }

            override func outFlatStruct(value: UInt32) throws -> FlatStruct {
                outFlatStruct_receivedValue = value
                return FlatStruct(a: 42, b: 99, c: 1.2)
            }

            override func outArrayOfStructs() throws -> [SimpleStruct] {
                return [
                    SimpleStruct(id: 1),
                    SimpleStruct(id: 2),
                    SimpleStruct(id: 3)
                ]
            }

            override func inException() throws {
                throw SimpleException(__ex_id: 0, message: "This is a test exception", code: 42)
            }

            override func outScalarWithException(dev_addr: UInt8, addr: UInt16) throws -> UInt8   {
                outScalarWithException_receivedDevAddr = dev_addr
                outScalarWithException_receivedAddr = addr
                return 32
            }
        }

        let servant = TestBasicServantImpl()
        let oid = try Self.poa!.activateObject(servant)
        guard let obj = NPRPCObject.fromObjectId(oid) else {
            XCTFail("Failed to create NPRPCObject from ObjectId")
            return
        }

        let client = TestBasic(obj)
        // Returning a boolean value to test basic marshalling of fundamental types
        XCTAssertEqual(try client.returnBoolean(), true)
        // Returning an IdArray to test marshalling of array types
        XCTAssertEqual(try client.returnIdArray(), [1, 2, 3, 4, 5])
        // Returning a UInt32 to test marshalling of unsigned integers
        XCTAssertEqual(try client.returnU32(), 123456789)
        // Testing the in_ method to verify marshalling of input parameters
        XCTAssertEqual(try client.in_(a: 123456789, b: true, c: [1, 2, 3, 4, 5]), true)
        // Verifying that the servant received the correct input parameters
        XCTAssertEqual(servant.in_receivedA, 123456789)
        XCTAssertEqual(servant.in_receivedB, true)
        XCTAssertEqual(servant.in_receivedC, [1, 2, 3, 4, 5])
        // Testing the out method to verify marshalling of output parameters
        let (outA, outB, outC) = try client.out()
        XCTAssertEqual(outA, 123456789)
        XCTAssertEqual(outB, true)
        XCTAssertEqual(outC, [1, 2, 3, 4, 5])
        // Testing marshalling of input structs
        let aaa = AAA(a: 1234, b: "Hello world", c: "Another string")
        try client.inStruct(a: aaa)
        XCTAssertEqual(servant.inStruct_receivedA.a, 1234)
        XCTAssertEqual(servant.inStruct_receivedA.b, "Hello world")
        XCTAssertEqual(servant.inStruct_receivedA.c, "Another string")
        // Testing marshalling of output structs
        let outStruct = try client.outStruct()
        XCTAssertEqual(outStruct.a, 1234)
        XCTAssertEqual(outStruct.b, "Hello world")
        XCTAssertEqual(outStruct.c, "Another string")
        // Testing marshalling of input flat structs
        let flatStruct = FlatStruct(a: 42, b: 99, c: 1.2)
        try client.inFlatStruct(value: 123456789, a: flatStruct)
        XCTAssertEqual(servant.inFlatStruct_receivedValue, 123456789)
        XCTAssertEqual(servant.inFlatStruct_receivedA.a, 42)
        XCTAssertEqual(servant.inFlatStruct_receivedA.b, 99)
        XCTAssertEqual(servant.inFlatStruct_receivedA.c, 1.2)
        // Testing marshalling of output flat structs
        let outFlatStruct = try client.outFlatStruct(value: 123456789)
        XCTAssertEqual(servant.outFlatStruct_receivedValue, 123456789)
        XCTAssertEqual(outFlatStruct.a, 42)
        XCTAssertEqual(outFlatStruct.b, 99)
        XCTAssertEqual(outFlatStruct.c, 1.2)
        // Testing marshalling of array of structs
        let arrayOfStructs = try client.outArrayOfStructs()
        XCTAssertEqual(arrayOfStructs.count, 3)
        XCTAssertEqual(arrayOfStructs[0].id, 1)
        XCTAssertEqual(arrayOfStructs[1].id, 2)
        XCTAssertEqual(arrayOfStructs[2].id, 3)
        // Testing exception handling
        do {
            try client.inException()
            XCTFail("Expected SimpleException to be thrown")
        } catch let e as SimpleException {
            XCTAssertEqual(e.code, 42)
            XCTAssertEqual(e.message, "This is a test exception")
        } catch {
            XCTFail("Expected SimpleException but got: \(error)")
        }
        // Testing marshalling of output scalar with exception
        let result = try client.outScalarWithException(dev_addr: 10, addr: 783)
        XCTAssertEqual(servant.outScalarWithException_receivedDevAddr, 10)
        XCTAssertEqual(servant.outScalarWithException_receivedAddr, 783)
        XCTAssertEqual(result, 32)
    }
}

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
        do {
            rpc = try RpcBuilder()
                .setLogLevel(.warn)
                .withHostname("localhost")
                .withTcp(16000)
                .withHttp(16001)
                    .ssl(certFile: "/workspace/certs/out/localhost.crt",
                         keyFile: "/workspace/certs/out/localhost.key")
                    .enableHttp3()
                    .rootDir("/workspace")
                    .http3Workers(2)
                    .maxRequestBodySize(10_000)
                    .maxWebSocketMessageSize(24 * 1024 * 1024)
                    .maxWebTransportMessageSize(24 * 1024 * 1024)
                    .maxWebSocketSessionsPerIp(32)
                    .maxWebSocketUpgradesPerIpPerSecond(16, burst: 32)
                    .maxWebSocketRequestsPerSessionPerSecond(120, burst: 240)
                    .maxHttp3ConnectionsPerIp(48)
                    .maxHttp3NewConnectionsPerIpPerSecond(24, burst: 48)
                    .maxHttpRpcRequestsPerIpPerSecond(200, burst: 400)
                    .maxWebTransportConnectsPerIpPerSecond(20, burst: 40)
                    .maxWebTransportRequestsPerSessionPerSecond(150, burst: 300)
                    .maxWebTransportStreamOpensPerSessionPerSecond(80, burst: 160)
                .withQuic(16002)
                    .ssl(certFile: "/workspace/certs/out/localhost.crt",
                         keyFile: "/workspace/certs/out/localhost.key")
                .build()
            // Give the TCP listener time to start accepting connections
            Thread.sleep(forTimeInterval: 0.1)
            poa = try rpc!.createPoa(maxObjects: 100)
            // Start the thread pool to process incoming RPCs (use 4 threads for testing)
            try rpc!.startThreadPool(4)
        } catch {
            fatalError("Failed to set up test environment: \(error)")
        }
    }

    override class func tearDown() {
        // Rpc/Poa will be cleaned up when deinit is called
        rpc = nil
        poa = nil
        super.tearDown()
    }

    func test1() throws {
        // Placeholder test to ensure test discovery works
        XCTAssertTrue(true)
    }

    func testHttpBuilderAdvancedLimitsInitializeRuntime() throws {
        XCTAssertTrue(Self.rpc?.isInitialized == true)
    }

    func testProduceHostJson() throws {
        class HostJsonServant: ShapeServiceServant, @unchecked Sendable {
            override func getRectangle(id: UInt32) -> Rectangle { Rectangle() }
            override func setRectangle(id: UInt32, rect: Rectangle) {}
            override func getRectangles() -> [Rectangle] { [] }
            override func getNumbers() -> [Int32] { [] }
            override func throwingMethod(code: UInt32) throws {}
        }

        let servant = HostJsonServant()
        let oid = try Self.poa!.activateObject(
            servant,
            flags: [.ws, .wss, .http, .https]
        )

        Self.rpc!.clearHostJson()
        try Self.rpc!.addToHostJson(name: "shape", objectId: oid)

        let outputDir = FileManager.default.temporaryDirectory
            .appendingPathComponent("nprpc-swift-host-json-test", isDirectory: true)
        try FileManager.default.createDirectory(at: outputDir, withIntermediateDirectories: true)
        let outputPath = outputDir.appendingPathComponent("host.json").path

        let writtenPath = try Self.rpc!.produceHostJson(outputPath: outputPath)
        XCTAssertEqual(writtenPath, outputPath)

        let text = try String(contentsOfFile: outputPath, encoding: .utf8)
        print("Produced host.json content:\n\(text)")
        XCTAssertTrue(text.contains("\"shape\""))
        XCTAssertTrue(text.contains("\"class_id\": \"basic_test/swift.test.ShapeService\""))
        XCTAssertTrue(text.contains("\"secured\": true"))
        XCTAssertTrue(text.contains("\"urls\": \"ws://localhost:16001;wss://localhost:16001;http://localhost:16001;https://localhost:16001;\""))
    }

    /// Test servant instantiation and direct method calls
    func testServantDirectCalls() throws {
        class TestShapeServant: ShapeServiceServant, @unchecked Sendable {
            var storedRect: Rectangle?

            override func getRectangle(id: UInt32) -> Rectangle {
                return Rectangle(
                    topLeft: Point(x: 10, y: 20, z: nil, symbol: ""),
                    bottomRight: Point(x: 110, y: 120, z: nil, symbol: ""),
                    color: .Green
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
                        color: .Red
                    ),
                    Rectangle(
                        topLeft: Point(x: 5, y: 6, z: nil, symbol: "c"),
                        bottomRight: Point(x: 7, y: 8, z: nil, symbol: "abcd"),
                        color: .Blue
                    )
                ]
            }

            override func getNumbers() -> [Int32] {
                return [42, 99, -7]
            }

            override func throwingMethod(code: UInt32) throws {
                throw TestException(message: "Test error \(code)", code: code)
            }
        }

        let servant = TestShapeServant()
        let oid = try Self.poa!.activateObject(servant, flags: .ws)

        // Create ShapeService client proxy from oid
        guard let obj = NPRPCObject.fromObjectId(oid) else {
            XCTFail("Failed to create NPRPCObject from ObjectId")
            return
        }
        let client = narrow(obj, to: ShapeService.self)!

        // Verify the class ID matches
        XCTAssertEqual(client.classId, "basic_test/swift.test.ShapeService")
        XCTAssertEqual(oid.class_id, "basic_test/swift.test.ShapeService")

        // Actually call RPC methods through the client proxy
        // This tests the full serialization/dispatch/deserialization cycle

        // Test getRectangle - should return the rectangle from our servant
        let rect = try client.getRectangle(id: 42)
        XCTAssertEqual(rect.topLeft.x, 10)
        XCTAssertEqual(rect.topLeft.y, 20)
        XCTAssertEqual(rect.bottomRight.x, 110)
        XCTAssertEqual(rect.bottomRight.y, 120)
        XCTAssertEqual(rect.color, .Green)

        // Test setRectangle - should store the rectangle in our servant
        let newRect = Rectangle(
            topLeft: Point(x: 5, y: 15, z: 33, symbol: "Abcd"),
            bottomRight: Point(x: 40, y: 150, z: nil, symbol: ""),
            color: .Blue
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
        XCTAssertEqual(servant.storedRect?.color, .Blue)

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
        XCTAssertEqual(rects[0].color, .Red)

        XCTAssertEqual(rects[1].topLeft.x, 5)
        XCTAssertEqual(rects[1].topLeft.y, 6)
        XCTAssertEqual(rects[1].topLeft.z, nil)
        XCTAssertEqual(rects[1].topLeft.symbol, "c")
        XCTAssertEqual(rects[1].bottomRight.x, 7)
        XCTAssertEqual(rects[1].bottomRight.y, 8)
        XCTAssertEqual(rects[1].bottomRight.z, nil)
        XCTAssertEqual(rects[1].bottomRight.symbol, "abcd")
        XCTAssertEqual(rects[1].color, .Blue)

        // Test getNumbers - should return the array of integers from our servant
        let nums = try client.getNumbers()
        XCTAssertEqual(nums, [42, 99, -7])
    }

    /// Test exception handling through RPC
    func testExceptionHandling() throws {
        class ExceptionServant: ShapeServiceServant, @unchecked Sendable {
            override func getRectangle(id: UInt32) -> Rectangle {
                return Rectangle()
            }
            override func setRectangle(id: UInt32, rect: Rectangle) {}
            override func getRectangles() -> [Rectangle] { return [] }
            override func getNumbers() -> [Int32] { return [] }
            override func throwingMethod(code: UInt32) throws {
                throw TestException(message: "Error with code \(code)", code: code)
            }
        }

        let servant = ExceptionServant()
        let oid = try Self.poa!.activateObject(servant, flags: .tcp)
        guard let obj = NPRPCObject.fromObjectId(oid) else {
            XCTFail("Failed to create NPRPCObject from ObjectId")
            return
        }
        let client = narrow(obj, to: ShapeService.self)!

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
        class TestBasicServantImpl: TestBasicServant, @unchecked Sendable {
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
                throw SimpleException(message: "This is a test exception", code: 42)
            }

            override func multipleExceptions(code: UInt32) throws {
                if code == 0 {
                    throw SimpleException(message: "Simple exception branch", code: 456)
                }

                throw AssertionFailed(message: "Assertion failed branch")
            }

            override func outScalarWithException(dev_addr: UInt8, addr: UInt16) throws -> UInt8   {
                outScalarWithException_receivedDevAddr = dev_addr
                outScalarWithException_receivedAddr = addr
                return 32
            }

            override func returnStringArray(count: UInt32) throws -> [String]   {
                return (1...count).map { "String \($0)" }
            }
        }

        let servant = TestBasicServantImpl()
        let oid = try Self.poa!.activateObject(servant, flags: .tcp)
        guard let obj = NPRPCObject.fromObjectId(oid) else {
            XCTFail("Failed to create NPRPCObject from ObjectId")
            return
        }

        let client = narrow(obj, to: TestBasic.self)!
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
        do {
            try client.multipleExceptions(code: 0)
            XCTFail("Expected SimpleException to be thrown")
        } catch let e as SimpleException {
            XCTAssertEqual(e.code, 456)
            XCTAssertEqual(e.message, "Simple exception branch")
        } catch {
            XCTFail("Expected SimpleException but got: \(error)")
        }
        do {
            try client.multipleExceptions(code: 1)
            XCTFail("Expected AssertionFailed to be thrown")
        } catch let e as AssertionFailed {
            XCTAssertEqual(e.message, "Assertion failed branch")
        } catch {
            XCTFail("Expected AssertionFailed but got: \(error)")
        }
        // Testing marshalling of output scalar with exception
        let result = try client.outScalarWithException(dev_addr: 10, addr: 783)
        XCTAssertEqual(servant.outScalarWithException_receivedDevAddr, 10)
        XCTAssertEqual(servant.outScalarWithException_receivedAddr, 783)
        XCTAssertEqual(result, 32)
        // Testing marshalling of output string array
        let stringArray = try client.returnStringArray(count: 3)
        XCTAssertEqual(stringArray, ["String 1", "String 2", "String 3"])
    }

    func testLargeMessage() throws {
        class TestLargeMessageServantImpl: TestLargeMessageServant, @unchecked Sendable {
            var in_receivedA: UInt32 = 0
            var in_receivedB: Bool = false
            var in_receivedC: [UInt8] = []

            override func in_(a: UInt32, b: Bool, c: [UInt8]) -> Bool {
                in_receivedA = a
                in_receivedB = b
                in_receivedC = c
                return true
            }

            override func out() -> (UInt32, Bool, [UInt8]) {
                // Return a large array (1MB)
                let largeArray = [UInt8](repeating: 0xAB, count: 1024 * 1024)
                return (0xDEADBEEF, true, largeArray)
            }
        }

        let servant = TestLargeMessageServantImpl()
        let oid = try Self.poa!.activateObject(servant, flags: .tcp)
        guard let obj = NPRPCObject.fromObjectId(oid) else {
            XCTFail("Failed to create NPRPCObject from ObjectId")
            return
        }
        let client = narrow(obj, to: TestLargeMessage.self)!

        // Test with a moderately large array (64KB)
        let largeInput = [UInt8](repeating: 0x42, count: 64 * 1024)
        XCTAssertEqual(try client.in_(a: 0xCAFEBABE, b: false, c: largeInput), true)
        XCTAssertEqual(servant.in_receivedA, 0xCAFEBABE)
        XCTAssertEqual(servant.in_receivedB, false)
        XCTAssertEqual(servant.in_receivedC.count, 64 * 1024)
        XCTAssertEqual(servant.in_receivedC.first, 0x42)
        XCTAssertEqual(servant.in_receivedC.last, 0x42)

        // Test output with large array
        let (outA, outB, outC) = try client.out()
        XCTAssertEqual(outA, 0xDEADBEEF)
        XCTAssertEqual(outB, true)
        XCTAssertEqual(outC.count, 1024 * 1024)
        XCTAssertEqual(outC.first, 0xAB)
        XCTAssertEqual(outC.last, 0xAB)
    }

    func testOptional() throws {
        class TestOptionalServantImpl: TestOptionalServant, @unchecked Sendable {
            var inEmpty_receivedA: UInt32? = nil
            var in_receivedA: UInt32? = nil
            var in_receivedB: AAA? = nil

            override func inEmpty(a: UInt32?) throws -> Bool {
                inEmpty_receivedA = a
                return a == nil
            }

            override func in_(a: UInt32?, b: AAA?) throws -> Bool {
                in_receivedA = a
                in_receivedB = b
                return a != nil && b != nil
            }

            override func outEmpty() throws -> UInt32? {
                return nil
            }

            override func out() throws -> UInt32? {
                return 42
            }

            override func returnOpt1() throws -> Opt1 {
                return Opt1(str: "Hello", data: [1, 2, 3, 4, 5])
            }
        }

        let servant = TestOptionalServantImpl()
        let oid = try Self.poa!.activateObject(servant, flags: .tcp)
        guard let obj = NPRPCObject.fromObjectId(oid) else {
            XCTFail("Failed to create NPRPCObject from ObjectId")
            return
        }
        let client = narrow(obj, to: TestOptional.self)!

        // Test InEmpty with nil
        XCTAssertEqual(try client.inEmpty(a: nil), true)
        XCTAssertNil(servant.inEmpty_receivedA)

        // Test InEmpty with value
        XCTAssertEqual(try client.inEmpty(a: 123), false)
        XCTAssertEqual(servant.inEmpty_receivedA, 123)

        // Test In_ with both values
        let aaa = AAA(a: 999, b: "Test", c: "Data")
        XCTAssertEqual(try client.in_(a: 456, b: aaa), true)
        XCTAssertEqual(servant.in_receivedA, 456)
        XCTAssertNotNil(servant.in_receivedB)
        XCTAssertEqual(servant.in_receivedB?.a, 999)
        XCTAssertEqual(servant.in_receivedB?.b, "Test")
        XCTAssertEqual(servant.in_receivedB?.c, "Data")

        // Test In_ with nil values
        XCTAssertEqual(try client.in_(a: nil, b: nil), false)
        XCTAssertNil(servant.in_receivedA)
        XCTAssertNil(servant.in_receivedB)

        // Test OutEmpty - returns nil
        let outEmpty = try client.outEmpty()
        XCTAssertNil(outEmpty)

        // Test Out - returns value
        let outVal = try client.out()
        XCTAssertEqual(outVal, 42)

        // Test ReturnOpt1 - returns struct with optional field populated
        let opt1 = try client.returnOpt1()
        XCTAssertEqual(opt1.str, "Hello")
        XCTAssertEqual(opt1.data, [1, 2, 3, 4, 5])
    }

    func testNested() throws {
        class TestNestedServantImpl: TestNestedServant, @unchecked Sendable {
            override func out() throws -> BBB? {
                return BBB(
                    a: [
                        AAA(a: 1, b: "first", c: "one"),
                        AAA(a: 2, b: "second", c: "two")
                    ],
                    b: [
                        CCC(a: "a1", b: "b1", c: true),
                        CCC(a: "a2", b: "b2", c: false),
                        CCC(a: "a3", b: "b3", c: nil)
                    ]
                )
            }

            override func returnNested() throws -> TopLevel {
                return TopLevel(
                    x: "top",
                    y: Level1(
                        x: "level1",
                        y: Level2(
                            x: "level2",
                            y: [10, 20, 30, 40, 50],
                            z: 0xDEADBEEFCAFEBABE
                        ),
                        z: 12345678901234
                    ),
                    z: 99999999999
                )
            }
        }

        let servant = TestNestedServantImpl()
        let oid = try Self.poa!.activateObject(servant, flags: .quic)
        guard let obj = NPRPCObject.fromObjectId(oid) else {
            XCTFail("Failed to create NPRPCObject from ObjectId")
            return
        }
        let client = narrow(obj, to: TestNested.self)!

        // Test Out - optional nested struct
        let bbb = try client.out()
        XCTAssertNotNil(bbb)
        XCTAssertEqual(bbb?.a.count, 2)
        XCTAssertEqual(bbb?.a[0].a, 1)
        XCTAssertEqual(bbb?.a[0].b, "first")
        XCTAssertEqual(bbb?.a[0].c, "one")
        XCTAssertEqual(bbb?.a[1].a, 2)
        XCTAssertEqual(bbb?.a[1].b, "second")
        XCTAssertEqual(bbb?.a[1].c, "two")
        XCTAssertEqual(bbb?.b.count, 3)
        XCTAssertEqual(bbb?.b[0].a, "a1")
        XCTAssertEqual(bbb?.b[0].b, "b1")
        XCTAssertEqual(bbb?.b[0].c, true)
        XCTAssertEqual(bbb?.b[1].c, false)
        XCTAssertNil(bbb?.b[2].c)

        // Test ReturnNested - deeply nested struct
        let nested = try client.returnNested()
        XCTAssertEqual(nested.x, "top")
        XCTAssertEqual(nested.z, 99999999999)
        XCTAssertEqual(nested.y.x, "level1")
        XCTAssertEqual(nested.y.z, 12345678901234)
        XCTAssertEqual(nested.y.y.x, "level2")
        XCTAssertEqual(nested.y.y.y, [10, 20, 30, 40, 50])
        XCTAssertEqual(nested.y.y.z, 0xDEADBEEFCAFEBABE)
    }

    func testArrays() throws {
        class TestArraysServantImpl: FixedSizeArrayTestServant, @unchecked Sendable {
            var inFixedArray_receivedA: [UInt32] = []
            var inFixedArrayOfStructs_receivedA: [SimpleStruct] = []

            override func inFixedArray(a: [UInt32]) throws {
                inFixedArray_receivedA = a
             }

            override func outFixedArray() throws -> [UInt32] {
                return [10, 20, 30, 40, 50, 60, 70, 80, 90, 100]
            }

            override func outTwoFixedArrays() throws -> ([UInt32], [UInt32]) {
                return ([1, 2, 3, 4, 5, 6, 7, 8, 9, 10],
                    [10, 9, 8, 7, 6, 5, 4, 3, 2, 1])
            }

            override func inFixedArrayOfStructs(a: [SimpleStruct]) throws {
                inFixedArrayOfStructs_receivedA = a
            }

            override func outFixedArrayOfStructs() throws -> [SimpleStruct] {
                return [
                    SimpleStruct(id: 10),
                    SimpleStruct(id: 20),
                    SimpleStruct(id: 30),
                    SimpleStruct(id: 40),
                    SimpleStruct(id: 50)
                ]
            }

            override func outTwoFixedArraysOfStructs() throws -> ([SimpleStruct], [AAA]) {
                let array1 = [
                    SimpleStruct(id: 1),
                    SimpleStruct(id: 2),
                    SimpleStruct(id: 3),
                    SimpleStruct(id: 4),
                    SimpleStruct(id: 5)
                ]
                let array2 = [
                    AAA(a: 10, b: "a", c: "x"),
                    AAA(a: 20, b: "b", c: "y"),
                    AAA(a: 30, b: "c", c: "z"),
                    AAA(a: 40, b: "d", c: "w"),
                    AAA(a: 50, b: "e", c: "v")
                ]
                return (array1, array2)
            }
        }

        let servant = TestArraysServantImpl()
        let oid = try Self.poa!.activateObject(servant, flags: .tcp)
        guard let obj = NPRPCObject.fromObjectId(oid) else {
            XCTFail("Failed to create NPRPCObject from ObjectId")
            return
        }
        let client = narrow(obj, to: FixedSizeArrayTest.self)!
        // Test InFixedArray
        let fixedArray: [UInt32] = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
        try client.inFixedArray(a: fixedArray)
        XCTAssertEqual(servant.inFixedArray_receivedA, fixedArray)

        // Test OutFixedArray
        let outArray = try client.outFixedArray()
        XCTAssertEqual(outArray, [10, 20, 30, 40, 50, 60, 70, 80, 90, 100])

        // Test OutTwoFixedArrays
        let (outArray1, outArray2) = try client.outTwoFixedArrays()
        XCTAssertEqual(outArray1, [1, 2, 3, 4, 5, 6, 7, 8, 9, 10])
        XCTAssertEqual(outArray2, [10, 9, 8, 7, 6, 5, 4, 3, 2, 1])

        // Test InFixedArrayOfStructs
        let structArray = [
            SimpleStruct(id: 1),
            SimpleStruct(id: 2),
            SimpleStruct(id: 3),
            SimpleStruct(id: 4),
            SimpleStruct(id: 5)
        ]
        try client.inFixedArrayOfStructs(a: structArray)
        XCTAssertEqual(servant.inFixedArrayOfStructs_receivedA.count, structArray.count)
        for i in 0..<structArray.count {
            XCTAssertEqual(servant.inFixedArrayOfStructs_receivedA[i].id, structArray[i].id)
        }

        // Test OutFixedArrayOfStructs
        let outStructArray = try client.outFixedArrayOfStructs()
        XCTAssertEqual(outStructArray.count, 5)
        XCTAssertEqual(outStructArray[0].id, 10)
        XCTAssertEqual(outStructArray[1].id, 20)
        XCTAssertEqual(outStructArray[2].id, 30)
        XCTAssertEqual(outStructArray[3].id, 40)
        XCTAssertEqual(outStructArray[4].id, 50)

        // Test OutTwoFixedArraysOfStructs
        let (outStructArray1, outStructArray2) = try client.outTwoFixedArraysOfStructs()
        XCTAssertEqual(outStructArray1.count, 5)
        XCTAssertEqual(outStructArray1[0].id, 1)
        XCTAssertEqual(outStructArray1[1].id, 2)
        XCTAssertEqual(outStructArray1[2].id, 3)
        XCTAssertEqual(outStructArray1[3].id, 4)
        XCTAssertEqual(outStructArray1[4].id, 5)
        XCTAssertEqual(outStructArray2.count, 5)
        XCTAssertEqual(outStructArray2[0].a, 10)
        XCTAssertEqual(outStructArray2[0].b, "a")
        XCTAssertEqual(outStructArray2[0].c, "x")
        XCTAssertEqual(outStructArray2[1].a, 20)
        XCTAssertEqual(outStructArray2[1].b, "b")
        XCTAssertEqual(outStructArray2[1].c, "y")
        XCTAssertEqual(outStructArray2[2].a, 30)
        XCTAssertEqual(outStructArray2[2].b, "c")
        XCTAssertEqual(outStructArray2[2].c, "z")
        XCTAssertEqual(outStructArray2[3].a, 40)
        XCTAssertEqual(outStructArray2[3].b, "d")
        XCTAssertEqual(outStructArray2[3].c, "w")
        XCTAssertEqual(outStructArray2[4].a, 50)
        XCTAssertEqual(outStructArray2[4].b, "e")
        XCTAssertEqual(outStructArray2[4].c, "v")

        // Tests that the servant correctly handles receiving arrays of the expected fixed size, and that it correctly returns arrays of the expected fixed size.
        // The test also verifies that the contents of the arrays are correctly transmitted and received, ensuring that the marshalling and unmarshalling of fixed-size arrays works as intended.

        // Test 1: Send array with FEWER elements than expected (3 instead of 10)
        // Should print warning and copy only 3 elements
        let smallArray: [UInt32] = [100, 200, 300]
        try client.inFixedArray(a: smallArray)
        XCTAssertEqual(servant.inFixedArray_receivedA.count, 10, "Should receive 5 elements sent")
        XCTAssertEqual(servant.inFixedArray_receivedA[0], 100)
        XCTAssertEqual(servant.inFixedArray_receivedA[1], 200)
        XCTAssertEqual(servant.inFixedArray_receivedA[2], 300)

        // Test 2: Send array with MORE elements than expected (12 instead of 10)
        // Should print warning and copy only first 10 elements
        let largeArray: [UInt32] = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12]
        try client.inFixedArray(a: largeArray)
        XCTAssertEqual(servant.inFixedArray_receivedA.count, 10, "Should receive only 10 elements (max size)")
        for i in 0..<10 {
            XCTAssertEqual(servant.inFixedArray_receivedA[i], UInt32(i + 1), "Element \(i) should be \(i + 1)")
        }

        // Test 3: Send struct array with fewer elements (3 instead of 10)
        let smallStructArray = [
            SimpleStruct(id: 42),
            SimpleStruct(id: 43),
            SimpleStruct(id: 44)
        ]
        try client.inFixedArrayOfStructs(a: smallStructArray)
        XCTAssertEqual(servant.inFixedArrayOfStructs_receivedA.count, 5, "Should receive 5 struct elements sent")
        // The first 3 elements should be from the sent array, and the remaining should be garbage/default values (since the servant's array is fixed size 5)
        XCTAssertEqual(servant.inFixedArrayOfStructs_receivedA[0].id, 42)
        XCTAssertEqual(servant.inFixedArrayOfStructs_receivedA[1].id, 43)
        XCTAssertEqual(servant.inFixedArrayOfStructs_receivedA[2].id, 44)

        // Test 4: Send struct array with more elements (7 instead of 5)
        let largeStructArray = [
            SimpleStruct(id: 1),
            SimpleStruct(id: 2),
            SimpleStruct(id: 3),
            SimpleStruct(id: 4),
            SimpleStruct(id: 5),
            SimpleStruct(id: 6),
            SimpleStruct(id: 7),
        ]
        try client.inFixedArrayOfStructs(a: largeStructArray)
        XCTAssertEqual(servant.inFixedArrayOfStructs_receivedA.count, 5, "Should receive only 5 struct elements (max size)")
        for i in 0..<5 {
            XCTAssertEqual(servant.inFixedArrayOfStructs_receivedA[i].id, UInt32(i + 1), "Element \(i) should be \(i + 1)")
        }
    }

    func testObjects() async throws {
        // This test verifies that object references can be passed as parameters and returned from methods, and that the servant can narrow them to the correct type and call methods on them.
        let semaphore = DispatchSemaphore(value: 0)

        // SimpleObject servant that stores a value
        class SimpleObjectImpl: SimpleObjectServant, @unchecked Sendable {
            var value: UInt32 = 0
            override func setValue(a: UInt32) {
                value = a
            }
        }

        // TestObjects servant that receives and manipulates object references
        class TestObjectsImpl: TestObjectsServant, @unchecked Sendable {
            var receivedObject: SimpleObject?
            let semaphore: DispatchSemaphore

            init(semaphore: DispatchSemaphore) {
                self.semaphore = semaphore
                super.init()
            }

            override func sendObject(o: NPRPCObject) throws {
                // Narrow the received object to SimpleObject
                guard let simpleObj = narrow(o, to: SimpleObject.self) else {
                    throw AssertionFailed(message: "Invalid object type passed to SendObject")
                }

                // Store the object for later and call SetValue on it
                receivedObject = simpleObj
                // setValue is async - use Task to call from sync context
                let sem = semaphore
                Task { [simpleObj, sem] in
                    await simpleObj.setValue(a: 42)
                    sem.signal()
                }
            }

            override func releaseReceivedObject() throws {
                guard receivedObject != nil else {
                    throw AssertionFailed(message: "No object was received yet")
                }
                receivedObject = nil
            }

            override func sendNestedObjects(o: NestedObjects) throws {
                // Narrow both objects to SimpleObject
                guard let obj1 = narrow(o.object1, to: SimpleObject.self),
                      let obj2 = narrow(o.object2, to: SimpleObject.self) else {
                    throw AssertionFailed(message: "Invalid object types in NestedObjects")
                }

                // Call methods on both objects - use Task for async calls
                let sem = semaphore
                Task { [obj1, obj2, sem] in
                    await obj1.setValue(a: 100)
                    await obj2.setValue(a: 200)
                    sem.signal()
                }
            }
        }

        // Create and activate SimpleObject servants
        let simpleServant1 = SimpleObjectImpl()
        let simpleOid1 = try Self.poa!.activateObject(simpleServant1, flags: .tcp)
        guard let simpleObj1 = NPRPCObject.fromObjectId(simpleOid1) else {
            XCTFail("Failed to create NPRPCObject from ObjectId")
            return
        }

        let simpleServant2 = SimpleObjectImpl()
        let simpleOid2 = try Self.poa!.activateObject(simpleServant2, flags: .tcp)
        guard let simpleObj2 = NPRPCObject.fromObjectId(simpleOid2) else {
            XCTFail("Failed to create NPRPCObject from ObjectId")
            return
        }

        // Create and activate TestObjects servant
        let testObjectsServant = TestObjectsImpl(semaphore: semaphore)
        let testOid = try Self.poa!.activateObject(testObjectsServant, flags: .tcp)
        guard let testObj = NPRPCObject.fromObjectId(testOid) else {
            XCTFail("Failed to create NPRPCObject from ObjectId")
            return
        }
        let testClient = narrow(testObj, to: TestObjects.self)!

        // Test 1: Send a single object
        try testClient.sendObject(o: simpleObj1)
        semaphore.wait()
        XCTAssertEqual(simpleServant1.value, 42, "SendObject should have called SetValue(42) on the object")

        // Test 2: Release the received object
        try testClient.releaseReceivedObject()

        // Test 3: Send nested objects
        let nested = NestedObjects(object1: simpleObj1, object2: simpleObj2)
        try testClient.sendNestedObjects(o: nested)
        semaphore.wait()
        XCTAssertEqual(simpleServant1.value, 100, "SendNestedObjects should have called SetValue(100) on object1")
        XCTAssertEqual(simpleServant2.value, 200, "SendNestedObjects should have called SetValue(200) on object2")
    }

    /// Test async methods with proper Swift async/await
    func testAsyncMethods() async throws {
        // AsyncTest servant implementation
        class AsyncTestImpl: AsyncTestServant, @unchecked Sendable {
            var receivedArg1: UInt32 = 0
            var receivedArg2: String = ""
            var method2Arg1: UInt32 = 0

            override func method1(arg1: UInt32, arg2: String) {
                receivedArg1 = arg1
                receivedArg2 = arg2
            }

            override func method2(arg1: UInt32) -> String {
                method2Arg1 = arg1
                return "Response for \(arg1)"
            }
        }

        // Create and activate servant
        let servant = AsyncTestImpl()
        let oid = try Self.poa!.activateObject(servant, flags: .tcp)
        guard let obj = NPRPCObject.fromObjectId(oid) else {
            XCTFail("Failed to create NPRPCObject from ObjectId")
            return
        }
        let client = narrow(obj, to: AsyncTest.self)!

        // Test 1: Async method with no return value
        // await blocks until the RPC completes and servant has executed
        await client.method1(arg1: 42, arg2: "Hello async!")
        XCTAssertEqual(servant.receivedArg1, 42)
        XCTAssertEqual(servant.receivedArg2, "Hello async!")

        // Test 2: Async method with output value
        // Async methods with outputs throw because they need to wait for a response
        let result = try await client.method2(arg1: 123)
        XCTAssertEqual(servant.method2Arg1, 123)
        XCTAssertEqual(result, "Response for 123")

        // Test 3: Multiple concurrent async calls
        servant.receivedArg1 = 0
        servant.receivedArg2 = ""

        async let call1: Void = client.method1(arg1: 100, arg2: "First")
        async let call2: Void = client.method1(arg1: 200, arg2: "Second")
        async let call3: Void = client.method1(arg1: 300, arg2: "Third")

        // Wait for all calls to complete
        _ = await (call1, call2, call3)

        // All calls have completed - the last received values depend on execution order
        XCTAssertTrue(servant.receivedArg1 >= 100, "At least one async call should have completed")
    }

    /// Test bad input validation for untrusted interfaces
    /// This simulates a malicious client sending a malformed buffer with an oversized vector
    func testBadInput() throws {
        // TestBadInput servant implementation (interface marked as [trusted=false])
        class TestBadInputImpl: TestBadInputServant,@unchecked Sendable {
            override func in_(a: [UInt8]) {
                // This should never be called - the safety check should reject the input
            }
        }

        // Create and activate servant
        let servant = TestBadInputImpl()
        let oid = try Self.poa!.activateObject(servant, flags: .tcp)
        guard let obj = NPRPCObject.fromObjectId(oid) else {
            XCTFail("Failed to create NPRPCObject from ObjectId")
            return
        }
        let client = narrow(obj, to: TestBadInput.self)!

        // Create a malformed buffer (similar to C++ test)
        let buffer = FlatBuffer()
        buffer.prepare(2048)
        buffer.commit(40)  // Header (16) + CallHeader (24) = 40 bytes

        guard let data = buffer.data else {
            XCTFail("Failed to get buffer data")
            return
        }

        // Write message header (16 bytes starting at offset 0)
        data.storeBytes(of: UInt32(0), toByteOffset: 0, as: UInt32.self)    // size (set later)
        data.storeBytes(of: impl.MessageId.FunctionCall.rawValue, toByteOffset: 4, as: UInt32.self)  // msg_id: FunctionCall
        data.storeBytes(of: impl.MessageType.Request.rawValue, toByteOffset: 8, as: UInt32.self)    // msg_type: Request
        data.storeBytes(of: UInt32(0), toByteOffset: 12, as: UInt32.self)   // reserved

        // Write call header (starting at offset 16)
        data.storeBytes(of: client.poaIdx, toByteOffset: 16, as: UInt16.self)     // poa_idx
        data.storeBytes(of: UInt8(0), toByteOffset: 18, as: UInt8.self)            // interface_idx
        data.storeBytes(of: UInt8(0), toByteOffset: 19, as: UInt8.self)            // function_idx
        data.storeBytes(of: client.objectId, toByteOffset: 24, as: UInt64.self)   // object_id

        // Commit additional space for the "payload"
        buffer.commit(1024)

        // Get fresh data pointer after commit
        guard let finalData = buffer.data else {
            XCTFail("Failed to get buffer data after commit")
            return
        }

        // Set correct size in header (total size of the buffer)
        finalData.storeBytes(of: UInt32(buffer.size), toByteOffset: 0, as: UInt32.self)

        // Write malformed vector at offset 32 (where input struct begins)
        // Vector format: [relative_offset: u32, count: u32]
        // Set count to 0xDEADBEEF (much larger than buffer size)
        finalData.storeBytes(of: UInt32(8), toByteOffset: 32, as: UInt32.self)          // relative offset
        finalData.storeBytes(of: UInt32(0xDEADBEEF), toByteOffset: 36, as: UInt32.self) // count (malicious!)

        // Send the malformed buffer - should get ExceptionBadInput
        do {
            try client.sendReceive(buffer: buffer, timeout: client.timeout)
            let _ = try handleStandardReply(buffer: buffer)
            XCTFail("Expected ExceptionBadInput to be thrown")
        } catch is ExceptionBadInput {
            // Expected - test passed
        } catch {
            XCTFail("Unexpected error: \(error)")
        }
    }

    func testUntrusted() throws {
        class UntrustedImpl: TestBadInputServant, @unchecked Sendable {
            var in_receivedA: [UInt8] = []
            var inStrings_receivedA: String = ""
            var inStrings_receivedB: String = ""
            var send_receivedMsg: ChatMessage? = nil

            override func in_(a: [UInt8]) {
                in_receivedA = a
            }

            override func inStrings(a: String, b: String) -> Bool {
                inStrings_receivedA = a
                inStrings_receivedB = b
                return true
            }

            override func send(msg: ChatMessage) -> Bool   {
                send_receivedMsg = msg
                return false
            }
        }

        // Create and activate servant
        let servant = UntrustedImpl()
        let oid = try Self.poa!.activateObject(servant, flags: .tcp)
        guard let obj = NPRPCObject.fromObjectId(oid) else {
            XCTFail("Failed to create NPRPCObject from ObjectId")
            return
        }
        let client = narrow(obj, to: TestBadInput.self)!

        try client.in_(a: [1, 2, 3, 4, 5])
        XCTAssertEqual(servant.in_receivedA, [1, 2, 3, 4, 5])

        let result = try client.inStrings(a: "Hello", b: "World")
        XCTAssertEqual(result, true)
        XCTAssertEqual(servant.inStrings_receivedA, "Hello")
        XCTAssertEqual(servant.inStrings_receivedB, "World")

        let result2 = try client.send(msg: ChatMessage(
            timestamp: 1234567890,
            str: "Test message",
            attachment: nil
        ))

        XCTAssertEqual(result2, false)
        XCTAssertNotNil(servant.send_receivedMsg)
        XCTAssertEqual(servant.send_receivedMsg?.timestamp, 1234567890)
        XCTAssertEqual(servant.send_receivedMsg?.str, "Test message")
        XCTAssertNil(servant.send_receivedMsg?.attachment)
    }

    // Test streaming RPC methods
    // This tests the full round-trip for server, client, and bidi streams.
    func testStreams() async throws {
        func expectAAA(_ value: AAA, _ expected: AAA, file: StaticString = #filePath, line: UInt = #line) {
            XCTAssertEqual(value.a, expected.a, file: file, line: line)
            XCTAssertEqual(value.b, expected.b, file: file, line: line)
            XCTAssertEqual(value.c, expected.c, file: file, line: line)
        }

        class TestStreamsImpl: TestStreamsServant, @unchecked Sendable {
            let uploadExpectation: XCTestExpectation
            let stringUploadExpectation: XCTestExpectation
            let binaryUploadExpectation: XCTestExpectation
            let u16VectorUploadExpectation: XCTestExpectation
            let objectVectorUploadExpectation: XCTestExpectation
            let u16ArrayUploadExpectation: XCTestExpectation
            let objectArrayUploadExpectation: XCTestExpectation
            let objectUploadExpectation: XCTestExpectation
            var uploadedBytes: [UInt8] = []
            var uploadedStrings: [String] = []
            var uploadedBinaryChunks: [[UInt8]] = []
            var uploadedU16Vectors: [[UInt16]] = []
            var uploadedObjectVectors: [[AAA]] = []
            var uploadedU16Arrays: [[UInt16]] = []
            var uploadedObjectArrays: [[AAA]] = []
            var uploadedObjects: [AAA] = []
            var uploadError: Error?
            var stringUploadError: Error?
            var binaryUploadError: Error?
            var u16VectorUploadError: Error?
            var objectVectorUploadError: Error?
            var u16ArrayUploadError: Error?
            var objectArrayUploadError: Error?
            var objectUploadError: Error?

            static func transformAAA(_ value: AAA, suffix: String) -> AAA {
                AAA(a: value.a + 100, b: value.b + suffix, c: value.c + suffix)
            }

            static func transformAliasOptionalPayload(_ value: AliasOptionalStreamPayload, suffix: String, delta: UInt32) -> AliasOptionalStreamPayload {
                var result = value
                result.id += delta
                result.ids = result.ids.map { $0 + delta }
                let mask = UInt8(truncatingIfNeeded: delta)
                result.payload = result.payload.map { $0 ^ mask }
                if let label = result.label {
                    result.label = label + suffix
                }
                if let item = result.item {
                    result.item = transformAAA(item, suffix: suffix)
                }
                if let maybeId = result.maybe_id {
                    result.maybe_id = maybeId + delta
                }
                if let maybeIds = result.maybe_ids {
                    result.maybe_ids = maybeIds.map { $0 + delta }
                }
                if let maybePayload = result.maybe_payload {
                    result.maybe_payload = maybePayload.map { $0 ^ mask }
                }
                return result
            }

            init(
                uploadExpectation: XCTestExpectation,
                stringUploadExpectation: XCTestExpectation,
                binaryUploadExpectation: XCTestExpectation,
                u16VectorUploadExpectation: XCTestExpectation,
                objectVectorUploadExpectation: XCTestExpectation,
                u16ArrayUploadExpectation: XCTestExpectation,
                objectArrayUploadExpectation: XCTestExpectation,
                objectUploadExpectation: XCTestExpectation
            ) {
                self.uploadExpectation = uploadExpectation
                self.stringUploadExpectation = stringUploadExpectation
                self.binaryUploadExpectation = binaryUploadExpectation
                self.u16VectorUploadExpectation = u16VectorUploadExpectation
                self.objectVectorUploadExpectation = objectVectorUploadExpectation
                self.u16ArrayUploadExpectation = u16ArrayUploadExpectation
                self.objectArrayUploadExpectation = objectArrayUploadExpectation
                self.objectUploadExpectation = objectUploadExpectation
                super.init()
            }

            override func getByteStream(size: UInt64) -> AsyncStream<UInt8> {
                return AsyncStream { continuation in
                    let count = min(size, 256)
                    for i in 0..<count {
                        continuation.yield(UInt8(i & 0xFF))
                    }
                    continuation.finish()
                }
            }

            override func getObjectStream(count: UInt32) -> AsyncStream<AAA> {
                return AsyncStream { continuation in
                    for i in 0..<count {
                        continuation.yield(
                            AAA(
                                a: i,
                                b: "name_\(i)",
                                c: "value_\(i)"
                            )
                        )
                    }
                    continuation.finish()
                }
            }

            override func getStringStream(count: UInt32) -> AsyncStream<String> {
                return AsyncStream { continuation in
                    for i in 0..<count {
                        continuation.yield("item_\(i)")
                    }
                    continuation.finish()
                }
            }

            override func getBinaryStream(count: UInt32) -> AsyncStream<[UInt8]> {
                return AsyncStream { continuation in
                    for i in 0..<count {
                        continuation.yield([UInt8(i), UInt8(i + 1), UInt8(i + 2)])
                    }
                    continuation.finish()
                }
            }

            override func getU16VectorStream(count: UInt32) -> AsyncStream<[UInt16]> {
                return AsyncStream { continuation in
                    for i in 0..<count {
                        let base = UInt16(i)
                        continuation.yield([100 + base, 200 + base, 300 + base])
                    }
                    continuation.finish()
                }
            }

            override func getObjectVectorStream(count: UInt32) -> AsyncStream<[AAA]> {
                return AsyncStream { continuation in
                    for i in 0..<count {
                        continuation.yield([
                            AAA(a: 10 * i + 1, b: "vec_\(i)_0", c: "payload_\(i)_0"),
                            AAA(a: 10 * i + 2, b: "vec_\(i)_1", c: "payload_\(i)_1"),
                        ])
                    }
                    continuation.finish()
                }
            }

            override func getU16ArrayStream(count: UInt32) -> AsyncStream<[UInt16]> {
                return AsyncStream { continuation in
                    for i in 0..<count {
                        let base = UInt16(i)
                        continuation.yield([base, base + 10, base + 20, base + 30])
                    }
                    continuation.finish()
                }
            }

            override func getObjectArrayStream(count: UInt32) -> AsyncStream<[AAA]> {
                return AsyncStream { continuation in
                    for i in 0..<count {
                        continuation.yield([
                            AAA(a: 10 * i + 1, b: "arr_\(i)_0", c: "item_\(i)_0"),
                            AAA(a: 10 * i + 2, b: "arr_\(i)_1", c: "item_\(i)_1"),
                        ])
                    }
                    continuation.finish()
                }
            }

            override func uploadByteStream(expected_size: UInt64, data: NPRPCStreamReader<UInt8>) async {
                var values: [UInt8] = []
                values.reserveCapacity(Int(expected_size))

                do {
                    for try await byte in data {
                        values.append(byte)
                    }
                } catch {
                    uploadError = error
                }

                uploadedBytes = values
                uploadExpectation.fulfill()
            }

            override func uploadObjectStream(expected_count: UInt64, data: NPRPCStreamReader<AAA>) async {
                var values: [AAA] = []
                values.reserveCapacity(Int(expected_count))

                do {
                    for try await value in data {
                        values.append(value)
                    }
                } catch {
                    objectUploadError = error
                }

                uploadedObjects = values
                objectUploadExpectation.fulfill()
            }

            override func uploadStringStream(expected_count: UInt64, data: NPRPCStreamReader<String>) async {
                var values: [String] = []
                values.reserveCapacity(Int(expected_count))

                do {
                    for try await value in data {
                        values.append(value)
                    }
                } catch {
                    stringUploadError = error
                }

                uploadedStrings = values
                stringUploadExpectation.fulfill()
            }

            override func uploadBinaryStream(expected_count: UInt64, data: NPRPCStreamReader<[UInt8]>) async {
                var values: [[UInt8]] = []
                values.reserveCapacity(Int(expected_count))

                do {
                    for try await value in data {
                        values.append(value)
                    }
                } catch {
                    binaryUploadError = error
                }

                uploadedBinaryChunks = values
                binaryUploadExpectation.fulfill()
            }

            override func uploadU16VectorStream(expected_count: UInt64, data: NPRPCStreamReader<[UInt16]>) async {
                var values: [[UInt16]] = []
                values.reserveCapacity(Int(expected_count))

                do {
                    for try await value in data {
                        values.append(value)
                    }
                } catch {
                    u16VectorUploadError = error
                }

                uploadedU16Vectors = values
                u16VectorUploadExpectation.fulfill()
            }

            override func uploadObjectVectorStream(expected_count: UInt64, data: NPRPCStreamReader<[AAA]>) async {
                var values: [[AAA]] = []
                values.reserveCapacity(Int(expected_count))

                do {
                    for try await value in data {
                        values.append(value)
                    }
                } catch {
                    objectVectorUploadError = error
                }

                uploadedObjectVectors = values
                objectVectorUploadExpectation.fulfill()
            }

            override func uploadU16ArrayStream(expected_count: UInt64, data: NPRPCStreamReader<[UInt16]>) async {
                var values: [[UInt16]] = []
                values.reserveCapacity(Int(expected_count))

                do {
                    for try await value in data {
                        values.append(value)
                    }
                } catch {
                    u16ArrayUploadError = error
                }

                uploadedU16Arrays = values
                u16ArrayUploadExpectation.fulfill()
            }

            override func uploadObjectArrayStream(expected_count: UInt64, data: NPRPCStreamReader<[AAA]>) async {
                var values: [[AAA]] = []
                values.reserveCapacity(Int(expected_count))

                do {
                    for try await value in data {
                        values.append(value)
                    }
                } catch {
                    objectArrayUploadError = error
                }

                uploadedObjectArrays = values
                objectArrayUploadExpectation.fulfill()
            }

            override func echoByteStream(xor_mask: UInt8, stream: NPRPCBidiStream<UInt8, UInt8>) async {
                do {
                    for try await byte in stream.reader {
                        await stream.writer.write(byte ^ xor_mask)
                    }
                    stream.writer.close()
                } catch {
                    stream.writer.abort()
                }
            }

            override func echoStringStream(suffix: String, stream: NPRPCBidiStream<String, String>) async {
                do {
                    for try await value in stream.reader {
                        await stream.writer.write(value + suffix)
                    }
                    stream.writer.close()
                } catch {
                    stream.writer.abort()
                }
            }

            override func echoBinaryStream(xor_mask: UInt8, stream: NPRPCBidiStream<[UInt8], [UInt8]>) async {
                do {
                    for try await value in stream.reader {
                        await stream.writer.write(value.map { $0 ^ xor_mask })
                    }
                    stream.writer.close()
                } catch {
                    stream.writer.abort()
                }
            }

            override func echoU16VectorStream(delta: UInt16, stream: NPRPCBidiStream<[UInt16], [UInt16]>) async {
                do {
                    for try await value in stream.reader {
                        await stream.writer.write(value.map { $0 + delta })
                    }
                    stream.writer.close()
                } catch {
                    stream.writer.abort()
                }
            }

            override func echoObjectVectorStream(suffix: String, stream: NPRPCBidiStream<[AAA], [AAA]>) async {
                do {
                    for try await value in stream.reader {
                        await stream.writer.write(value.map { Self.transformAAA($0, suffix: suffix) })
                    }
                    stream.writer.close()
                } catch {
                    stream.writer.abort()
                }
            }

            override func echoU16ArrayStream(delta: UInt16, stream: NPRPCBidiStream<[UInt16], [UInt16]>) async {
                do {
                    for try await value in stream.reader {
                        await stream.writer.write(value.map { $0 + delta })
                    }
                    stream.writer.close()
                } catch {
                    stream.writer.abort()
                }
            }

            override func echoObjectArrayStream(suffix: String, stream: NPRPCBidiStream<[AAA], [AAA]>) async {
                do {
                    for try await value in stream.reader {
                        await stream.writer.write(value.map { Self.transformAAA($0, suffix: suffix) })
                    }
                    stream.writer.close()
                } catch {
                    stream.writer.abort()
                }
            }

            override func echoAliasOptionalStream(suffix: String, delta: UInt32, stream: NPRPCBidiStream<AliasOptionalStreamPayload, AliasOptionalStreamPayload>) async {
                do {
                    for try await value in stream.reader {
                        await stream.writer.write(Self.transformAliasOptionalPayload(value, suffix: suffix, delta: delta))
                    }
                    stream.writer.close()
                } catch {
                    stream.writer.abort()
                }
            }

            override func echoObjectStream(suffix: String, stream: NPRPCBidiStream<AAA, AAA>) async {
                do {
                    for try await value in stream.reader {
                        await stream.writer.write(Self.transformAAA(value, suffix: suffix))
                    }
                    stream.writer.close()
                } catch {
                    stream.writer.abort()
                }
            }
        }

        let uploadExpectation = expectation(description: "client stream upload completed")
        let stringUploadExpectation = expectation(description: "string client stream upload completed")
        let binaryUploadExpectation = expectation(description: "binary client stream upload completed")
        let u16VectorUploadExpectation = expectation(description: "u16 vector client stream upload completed")
        let objectVectorUploadExpectation = expectation(description: "object vector client stream upload completed")
        let u16ArrayUploadExpectation = expectation(description: "u16 array client stream upload completed")
        let objectArrayUploadExpectation = expectation(description: "object array client stream upload completed")
        let objectUploadExpectation = expectation(description: "object client stream upload completed")

        // Create and activate servant
        let servant = TestStreamsImpl(
            uploadExpectation: uploadExpectation,
            stringUploadExpectation: stringUploadExpectation,
            binaryUploadExpectation: binaryUploadExpectation,
            u16VectorUploadExpectation: u16VectorUploadExpectation,
            objectVectorUploadExpectation: objectVectorUploadExpectation,
            u16ArrayUploadExpectation: u16ArrayUploadExpectation,
            objectArrayUploadExpectation: objectArrayUploadExpectation,
            objectUploadExpectation: objectUploadExpectation
        )
        let oid = try Self.poa!.activateObject(servant, flags: .ws)
        XCTAssertEqual(oid.class_id, "nprpc_test/nprpc.test.TestStreams")

        // Create client proxy
        guard let obj = NPRPCObject.fromObjectId(oid) else {
            XCTFail("Failed to create NPRPCObject from ObjectId")
            return
        }
        let client = narrow(obj, to: TestStreams.self)!
        XCTAssertEqual(client.classId, "nprpc_test/nprpc.test.TestStreams")

        let stream = try client.getByteStream(size: 5)
        var receivedValues: [UInt8] = []

        for try await value in stream {
            receivedValues.append(value)
        }
        XCTAssertEqual(receivedValues.count, 5, "Should receive 5 bytes from stream")
        for i in 0..<5 {
            XCTAssertEqual(receivedValues[i], UInt8(i), "Byte \(i) should be \(i)")
        }

        let objectStream = try client.getObjectStream(count: 3)
        var receivedObjects: [AAA] = []
        for try await value in objectStream {
            receivedObjects.append(value)
        }

        XCTAssertEqual(receivedObjects.count, 3)
        XCTAssertEqual(receivedObjects[0].a, 0)
        XCTAssertEqual(receivedObjects[0].b, "name_0")
        XCTAssertEqual(receivedObjects[0].c, "value_0")
        XCTAssertEqual(receivedObjects[2].a, 2)
        XCTAssertEqual(receivedObjects[2].b, "name_2")
        XCTAssertEqual(receivedObjects[2].c, "value_2")

        let stringStream = try client.getStringStream(count: 3)
        var receivedStrings: [String] = []
        for try await value in stringStream {
            receivedStrings.append(value)
        }
        XCTAssertEqual(receivedStrings, ["item_0", "item_1", "item_2"])

        let binaryStream = try client.getBinaryStream(count: 3)
        var receivedBinaryChunks: [[UInt8]] = []
        for try await value in binaryStream {
            receivedBinaryChunks.append(value)
        }
        XCTAssertEqual(receivedBinaryChunks, [[0, 1, 2], [1, 2, 3], [2, 3, 4]])

        let u16VectorStream = try client.getU16VectorStream(count: 3)
        var receivedU16Vectors: [[UInt16]] = []
        for try await value in u16VectorStream {
            receivedU16Vectors.append(value)
        }
        XCTAssertEqual(receivedU16Vectors, [[100, 200, 300], [101, 201, 301], [102, 202, 302]])

        let objectVectorStream = try client.getObjectVectorStream(count: 2)
        var receivedObjectVectors: [[AAA]] = []
        for try await value in objectVectorStream {
            receivedObjectVectors.append(value)
        }
        XCTAssertEqual(receivedObjectVectors.count, 2)
        expectAAA(receivedObjectVectors[0][0], AAA(a: 1, b: "vec_0_0", c: "payload_0_0"))
        expectAAA(receivedObjectVectors[0][1], AAA(a: 2, b: "vec_0_1", c: "payload_0_1"))
        expectAAA(receivedObjectVectors[1][0], AAA(a: 11, b: "vec_1_0", c: "payload_1_0"))
        expectAAA(receivedObjectVectors[1][1], AAA(a: 12, b: "vec_1_1", c: "payload_1_1"))

        let u16ArrayStream = try client.getU16ArrayStream(count: 3)
        var receivedU16Arrays: [[UInt16]] = []
        for try await value in u16ArrayStream {
            receivedU16Arrays.append(value)
        }
        XCTAssertEqual(receivedU16Arrays, [[0, 10, 20, 30], [1, 11, 21, 31], [2, 12, 22, 32]])

        let objectArrayStream = try client.getObjectArrayStream(count: 2)
        var receivedObjectArrays: [[AAA]] = []
        for try await value in objectArrayStream {
            receivedObjectArrays.append(value)
        }
        XCTAssertEqual(receivedObjectArrays.count, 2)
        expectAAA(receivedObjectArrays[0][0], AAA(a: 1, b: "arr_0_0", c: "item_0_0"))
        expectAAA(receivedObjectArrays[0][1], AAA(a: 2, b: "arr_0_1", c: "item_0_1"))
        expectAAA(receivedObjectArrays[1][0], AAA(a: 11, b: "arr_1_0", c: "item_1_0"))
        expectAAA(receivedObjectArrays[1][1], AAA(a: 12, b: "arr_1_1", c: "item_1_1"))

        let uploadWriter = try client.uploadByteStream(expected_size: 5)
        for byte in [UInt8(1), 2, 3, 4, 5] {
            await uploadWriter.write(byte)
        }
        uploadWriter.close()

        await fulfillment(of: [uploadExpectation], timeout: 2.0)
        XCTAssertNil(servant.uploadError)
        XCTAssertEqual(servant.uploadedBytes, [1, 2, 3, 4, 5])

        let uploadObjectWriter = try client.uploadObjectStream(expected_count: 2)
        await uploadObjectWriter.write(AAA(a: 1, b: "first", c: "one"))
        await uploadObjectWriter.write(AAA(a: 2, b: "second", c: "two"))
        uploadObjectWriter.close()

        await fulfillment(of: [objectUploadExpectation], timeout: 2.0)
        XCTAssertNil(servant.objectUploadError)
        XCTAssertEqual(servant.uploadedObjects.count, 2)
        XCTAssertEqual(servant.uploadedObjects[0].a, 1)
        XCTAssertEqual(servant.uploadedObjects[0].b, "first")
        XCTAssertEqual(servant.uploadedObjects[0].c, "one")
        XCTAssertEqual(servant.uploadedObjects[1].a, 2)
        XCTAssertEqual(servant.uploadedObjects[1].b, "second")
        XCTAssertEqual(servant.uploadedObjects[1].c, "two")

        let uploadStringWriter = try client.uploadStringStream(expected_count: 3)
        await uploadStringWriter.write("alpha")
        await uploadStringWriter.write("beta")
        await uploadStringWriter.write("gamma")
        uploadStringWriter.close()

        await fulfillment(of: [stringUploadExpectation], timeout: 2.0)
        XCTAssertNil(servant.stringUploadError)
        XCTAssertEqual(servant.uploadedStrings, ["alpha", "beta", "gamma"])

        let uploadBinaryWriter = try client.uploadBinaryStream(expected_count: 3)
        await uploadBinaryWriter.write([1, 2, 3])
        await uploadBinaryWriter.write([4, 5])
        await uploadBinaryWriter.write([6, 7, 8, 9])
        uploadBinaryWriter.close()

        await fulfillment(of: [binaryUploadExpectation], timeout: 2.0)
        XCTAssertNil(servant.binaryUploadError)
        XCTAssertEqual(servant.uploadedBinaryChunks, [[1, 2, 3], [4, 5], [6, 7, 8, 9]])

        let uploadU16VectorWriter = try client.uploadU16VectorStream(expected_count: 3)
        await uploadU16VectorWriter.write([10, 20, 30])
        await uploadU16VectorWriter.write([40, 50])
        await uploadU16VectorWriter.write([60, 70, 80, 90])
        uploadU16VectorWriter.close()

        await fulfillment(of: [u16VectorUploadExpectation], timeout: 2.0)
        XCTAssertNil(servant.u16VectorUploadError)
        XCTAssertEqual(servant.uploadedU16Vectors, [[10, 20, 30], [40, 50], [60, 70, 80, 90]])

        let uploadObjectVectorWriter = try client.uploadObjectVectorStream(expected_count: 2)
        await uploadObjectVectorWriter.write([
            AAA(a: 1, b: "left_0", c: "payload_0"),
            AAA(a: 2, b: "left_1", c: "payload_1"),
        ])
        await uploadObjectVectorWriter.write([
            AAA(a: 3, b: "right_0", c: "payload_2"),
            AAA(a: 4, b: "right_1", c: "payload_3"),
        ])
        uploadObjectVectorWriter.close()

        await fulfillment(of: [objectVectorUploadExpectation], timeout: 2.0)
        XCTAssertNil(servant.objectVectorUploadError)
        XCTAssertEqual(servant.uploadedObjectVectors.count, 2)
        expectAAA(servant.uploadedObjectVectors[0][0], AAA(a: 1, b: "left_0", c: "payload_0"))
        expectAAA(servant.uploadedObjectVectors[0][1], AAA(a: 2, b: "left_1", c: "payload_1"))
        expectAAA(servant.uploadedObjectVectors[1][0], AAA(a: 3, b: "right_0", c: "payload_2"))
        expectAAA(servant.uploadedObjectVectors[1][1], AAA(a: 4, b: "right_1", c: "payload_3"))

        let uploadU16ArrayWriter = try client.uploadU16ArrayStream(expected_count: 2)
        await uploadU16ArrayWriter.write([1, 2, 3, 4])
        await uploadU16ArrayWriter.write([10, 20, 30, 40])
        uploadU16ArrayWriter.close()

        await fulfillment(of: [u16ArrayUploadExpectation], timeout: 2.0)
        XCTAssertNil(servant.u16ArrayUploadError)
        XCTAssertEqual(servant.uploadedU16Arrays, [[1, 2, 3, 4], [10, 20, 30, 40]])

        let uploadObjectArrayWriter = try client.uploadObjectArrayStream(expected_count: 2)
        await uploadObjectArrayWriter.write([
            AAA(a: 5, b: "array_0_0", c: "item_0_0"),
            AAA(a: 6, b: "array_0_1", c: "item_0_1"),
        ])
        await uploadObjectArrayWriter.write([
            AAA(a: 7, b: "array_1_0", c: "item_1_0"),
            AAA(a: 8, b: "array_1_1", c: "item_1_1"),
        ])
        uploadObjectArrayWriter.close()

        await fulfillment(of: [objectArrayUploadExpectation], timeout: 2.0)
        XCTAssertNil(servant.objectArrayUploadError)
        XCTAssertEqual(servant.uploadedObjectArrays.count, 2)
        expectAAA(servant.uploadedObjectArrays[0][0], AAA(a: 5, b: "array_0_0", c: "item_0_0"))
        expectAAA(servant.uploadedObjectArrays[0][1], AAA(a: 6, b: "array_0_1", c: "item_0_1"))
        expectAAA(servant.uploadedObjectArrays[1][0], AAA(a: 7, b: "array_1_0", c: "item_1_0"))
        expectAAA(servant.uploadedObjectArrays[1][1], AAA(a: 8, b: "array_1_1", c: "item_1_1"))

        let bidiStream = try client.echoByteStream(xor_mask: 0x5A)
        let input: [UInt8] = [10, 11, 12, 13]
        for byte in input {
            await bidiStream.writer.write(byte)
        }
        bidiStream.writer.close()

        var echoed: [UInt8] = []
        for try await byte in bidiStream.reader {
            echoed.append(byte)
        }

        XCTAssertEqual(echoed, input.map { $0 ^ 0x5A })

        let stringBidiStream = try client.echoStringStream(suffix: "-ok")
        for value in ["left", "right"] {
            await stringBidiStream.writer.write(value)
        }
        stringBidiStream.writer.close()

        var echoedStrings: [String] = []
        for try await value in stringBidiStream.reader {
            echoedStrings.append(value)
        }
        XCTAssertEqual(echoedStrings, ["left-ok", "right-ok"])

        let binaryBidiStream = try client.echoBinaryStream(xor_mask: 0x5A)
        for value in [[UInt8(0), 1, 2], [10, 11]] {
            await binaryBidiStream.writer.write(value)
        }
        binaryBidiStream.writer.close()

        var echoedBinaryChunks: [[UInt8]] = []
        for try await value in binaryBidiStream.reader {
            echoedBinaryChunks.append(value)
        }
        XCTAssertEqual(
            echoedBinaryChunks,
            [[UInt8(0x5A), 0x5B, 0x58], [UInt8(10 ^ 0x5A), UInt8(11 ^ 0x5A)]]
        )

        let u16VectorBidiStream = try client.echoU16VectorStream(delta: 7)
        await u16VectorBidiStream.writer.write([1, 2, 3])
        await u16VectorBidiStream.writer.write([100, 200])
        u16VectorBidiStream.writer.close()

        var echoedU16Vectors: [[UInt16]] = []
        for try await value in u16VectorBidiStream.reader {
            echoedU16Vectors.append(value)
        }
        XCTAssertEqual(echoedU16Vectors, [[8, 9, 10], [107, 207]])

        let objectVectorBidiStream = try client.echoObjectVectorStream(suffix: "-ok")
        await objectVectorBidiStream.writer.write([
            AAA(a: 1, b: "vec_a", c: "left"),
            AAA(a: 2, b: "vec_b", c: "right"),
        ])
        await objectVectorBidiStream.writer.write([
            AAA(a: 3, b: "vec_c", c: "up"),
            AAA(a: 4, b: "vec_d", c: "down"),
        ])
        objectVectorBidiStream.writer.close()

        var echoedObjectVectors: [[AAA]] = []
        for try await value in objectVectorBidiStream.reader {
            echoedObjectVectors.append(value)
        }
        XCTAssertEqual(echoedObjectVectors.count, 2)
        expectAAA(echoedObjectVectors[0][0], AAA(a: 101, b: "vec_a-ok", c: "left-ok"))
        expectAAA(echoedObjectVectors[0][1], AAA(a: 102, b: "vec_b-ok", c: "right-ok"))
        expectAAA(echoedObjectVectors[1][0], AAA(a: 103, b: "vec_c-ok", c: "up-ok"))
        expectAAA(echoedObjectVectors[1][1], AAA(a: 104, b: "vec_d-ok", c: "down-ok"))

        let u16ArrayBidiStream = try client.echoU16ArrayStream(delta: 7)
        await u16ArrayBidiStream.writer.write([1, 2, 3, 4])
        await u16ArrayBidiStream.writer.write([10, 20, 30, 40])
        u16ArrayBidiStream.writer.close()

        var echoedU16Arrays: [[UInt16]] = []
        for try await value in u16ArrayBidiStream.reader {
            echoedU16Arrays.append(value)
        }
        XCTAssertEqual(echoedU16Arrays, [[8, 9, 10, 11], [17, 27, 37, 47]])

        let objectArrayBidiStream = try client.echoObjectArrayStream(suffix: "-ok")
        await objectArrayBidiStream.writer.write([
            AAA(a: 11, b: "arr_a", c: "west"),
            AAA(a: 12, b: "arr_b", c: "east"),
        ])
        await objectArrayBidiStream.writer.write([
            AAA(a: 13, b: "arr_c", c: "north"),
            AAA(a: 14, b: "arr_d", c: "south"),
        ])
        objectArrayBidiStream.writer.close()

        var echoedObjectArrays: [[AAA]] = []
        for try await value in objectArrayBidiStream.reader {
            echoedObjectArrays.append(value)
        }
        XCTAssertEqual(echoedObjectArrays.count, 2)
        expectAAA(echoedObjectArrays[0][0], AAA(a: 111, b: "arr_a-ok", c: "west-ok"))
        expectAAA(echoedObjectArrays[0][1], AAA(a: 112, b: "arr_b-ok", c: "east-ok"))
        expectAAA(echoedObjectArrays[1][0], AAA(a: 113, b: "arr_c-ok", c: "north-ok"))
        expectAAA(echoedObjectArrays[1][1], AAA(a: 114, b: "arr_d-ok", c: "south-ok"))

        let aliasBidiStream = try client.echoAliasOptionalStream(suffix: "-ok", delta: 7)
        await aliasBidiStream.writer.write(
            AliasOptionalStreamPayload(
                id: 10,
                ids: [1, 2, 3],
                payload: [0, 1, 2],
                label: "label",
                item: AAA(a: 5, b: "item", c: "payload"),
                maybe_id: 20,
                maybe_ids: [4, 5],
                maybe_payload: [6, 7, 8]
            )
        )
        await aliasBidiStream.writer.write(
            AliasOptionalStreamPayload(
                id: 30,
                ids: [9],
                payload: [9, 8],
                label: nil,
                item: nil,
                maybe_id: nil,
                maybe_ids: nil,
                maybe_payload: nil
            )
        )
        aliasBidiStream.writer.close()

        var echoedAliasPayloads: [AliasOptionalStreamPayload] = []
        for try await value in aliasBidiStream.reader {
            echoedAliasPayloads.append(value)
        }
        XCTAssertEqual(echoedAliasPayloads.count, 2)
        XCTAssertEqual(echoedAliasPayloads[0].id, 17)
        XCTAssertEqual(echoedAliasPayloads[0].ids, [8, 9, 10])
        XCTAssertEqual(echoedAliasPayloads[0].payload, [7, 6, 5])
        XCTAssertEqual(echoedAliasPayloads[0].label, "label-ok")
        XCTAssertNotNil(echoedAliasPayloads[0].item)
        expectAAA(echoedAliasPayloads[0].item!, AAA(a: 105, b: "item-ok", c: "payload-ok"))
        XCTAssertEqual(echoedAliasPayloads[0].maybe_id, 27)
        XCTAssertEqual(echoedAliasPayloads[0].maybe_ids!, [11, 12])
        XCTAssertEqual(echoedAliasPayloads[0].maybe_payload!, [1, 0, 15])

        XCTAssertEqual(echoedAliasPayloads[1].id, 37)
        XCTAssertEqual(echoedAliasPayloads[1].ids, [16])
        XCTAssertEqual(echoedAliasPayloads[1].payload, [14, 15])
        XCTAssertNil(echoedAliasPayloads[1].label)
        XCTAssertNil(echoedAliasPayloads[1].item)
        XCTAssertNil(echoedAliasPayloads[1].maybe_id)
        XCTAssertNil(echoedAliasPayloads[1].maybe_ids)
        XCTAssertNil(echoedAliasPayloads[1].maybe_payload)

        let objectBidiStream = try client.echoObjectStream(suffix: "-ok")
        let objectInput: [AAA] = [
            AAA(a: 10, b: "alpha", c: "one"),
            AAA(a: 20, b: "beta", c: "two"),
        ]
        for value in objectInput {
            await objectBidiStream.writer.write(value)
        }
        objectBidiStream.writer.close()

        var echoedObjects: [AAA] = []
        for try await value in objectBidiStream.reader {
            echoedObjects.append(value)
        }

        XCTAssertEqual(echoedObjects.count, 2)
        XCTAssertEqual(echoedObjects[0].a, 110)
        XCTAssertEqual(echoedObjects[0].b, "alpha-ok")
        XCTAssertEqual(echoedObjects[0].c, "one-ok")
        XCTAssertEqual(echoedObjects[1].a, 120)
        XCTAssertEqual(echoedObjects[1].b, "beta-ok")
        XCTAssertEqual(echoedObjects[1].c, "two-ok")
    }

    func testVariantRpc() throws {
        class TestVariantRpcServantImpl: TestVariantRpcServant, @unchecked Sendable {
            override func echo(value: TestVariant) throws -> TestVariant {
                return value
            }
        }

        let servant = TestVariantRpcServantImpl()
        let oid = try Self.poa!.activateObject(servant, flags: .tcp)
        guard let obj = NPRPCObject.fromObjectId(oid) else {
            XCTFail("Failed to create NPRPCObject from ObjectId")
            return
        }
        let client = narrow(obj, to: TestVariantRpc.self)!

        // Echo payloadA
        let inputA = TestVariant.payloadA(VariantPayloadA(id: 42, label: "hello swift"))
        let resultA = try client.echo(value: inputA)
        if case .payloadA(let val) = resultA {
            XCTAssertEqual(val.id, 42)
            XCTAssertEqual(val.label, "hello swift")
        } else {
            XCTFail("Expected payloadA, got \(resultA)")
        }

        // Echo payloadB
        let inputB = TestVariant.payloadB(VariantPayloadB(code: 99, detail: "swift detail"))
        let resultB = try client.echo(value: inputB)
        if case .payloadB(let val) = resultB {
            XCTAssertEqual(val.code, 99)
            XCTAssertEqual(val.detail, "swift detail")
        } else {
            XCTFail("Expected payloadB, got \(resultB)")
        }

        // Echo payloadA with empty string
        let inputEmpty = TestVariant.payloadA(VariantPayloadA(id: 0, label: ""))
        let resultEmpty = try client.echo(value: inputEmpty)
        if case .payloadA(let val) = resultEmpty {
            XCTAssertEqual(val.id, 0)
            XCTAssertEqual(val.label, "")
        } else {
            XCTFail("Expected payloadA, got \(resultEmpty)")
        }
    }
}

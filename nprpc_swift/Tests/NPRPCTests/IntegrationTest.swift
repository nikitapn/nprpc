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
                .setLogLevel(.info)
                .setHostname("localhost")
                .withTcp(16000)
                .withHttp(16001)
                .withQuic(16002)
                    .ssl(certFile: "/workspace/certs/out/localhost.crt",
                         keyFile: "/workspace/certs/out/localhost.key")
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
                throw TestException(message: "Test error \(code)", code: code)
            }
        }

        let servant = TestShapeServant()
        let oid = try Self.poa!.activateObject(servant)

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
                throw TestException(message: "Error with code \(code)", code: code)
            }
        }

        let servant = ExceptionServant()
        let oid = try Self.poa!.activateObject(servant)
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
                throw SimpleException(message: "This is a test exception", code: 42)
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
        // Testing marshalling of output scalar with exception
        let result = try client.outScalarWithException(dev_addr: 10, addr: 783)
        XCTAssertEqual(servant.outScalarWithException_receivedDevAddr, 10)
        XCTAssertEqual(servant.outScalarWithException_receivedAddr, 783)
        XCTAssertEqual(result, 32)
    }

    func testLargeMessage() throws {
        class TestLargeMessageServantImpl: TestLargeMessageServant {
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
        let oid = try Self.poa!.activateObject(servant)
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
        class TestOptionalServantImpl: TestOptionalServant {
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
        let oid = try Self.poa!.activateObject(servant)
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
        class TestNestedServantImpl: TestNestedServant {
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
        let oid = try Self.poa!.activateObject(servant)
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

    func testObjects() async throws {
        // This test verifies that object references can be passed as parameters and returned from methods, and that the servant can narrow them to the correct type and call methods on them.
        let semaphore = DispatchSemaphore(value: 0)

        // SimpleObject servant that stores a value
        class SimpleObjectImpl: SimpleObjectServant {
            var value: UInt32 = 0
            override func setValue(a: UInt32) {
                value = a
            }
        }

        // TestObjects servant that receives and manipulates object references
        class TestObjectsImpl: TestObjectsServant {
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
        let simpleOid1 = try Self.poa!.activateObject(simpleServant1)
        guard let simpleObj1 = NPRPCObject.fromObjectId(simpleOid1) else {
            XCTFail("Failed to create NPRPCObject from ObjectId")
            return
        }

        let simpleServant2 = SimpleObjectImpl()
        let simpleOid2 = try Self.poa!.activateObject(simpleServant2)
        guard let simpleObj2 = NPRPCObject.fromObjectId(simpleOid2) else {
            XCTFail("Failed to create NPRPCObject from ObjectId")
            return
        }

        // Create and activate TestObjects servant
        let testObjectsServant = TestObjectsImpl(semaphore: semaphore)
        let testOid = try Self.poa!.activateObject(testObjectsServant)
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
        class AsyncTestImpl: AsyncTestServant {
            var receivedArg1: UInt32 = 0
            var receivedArg2: String = ""
            var method2Arg1: UInt32 = 0
            
            override func method1(arg1: UInt32, arg2: String) {
                print("AsyncTestImpl: method1 called with arg1=\(arg1), arg2=\(arg2)")
                receivedArg1 = arg1
                receivedArg2 = arg2
            }
            
            override func method2(arg1: UInt32) -> String {
                print("AsyncTestImpl: method2 called with arg1=\(arg1)")
                method2Arg1 = arg1
                return "Response for \(arg1)"
            }
        }
        
        // Create and activate servant
        let servant = AsyncTestImpl()
        let oid = try Self.poa!.activateObject(servant)
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
        
        print("Async test completed successfully!")
    }

    /// Test bad input validation for untrusted interfaces
    /// This simulates a malicious client sending a malformed buffer with an oversized vector
    func testBadInput() throws {
        // TestBadInput servant implementation (interface marked as [trusted=false])
        class TestBadInputImpl: TestBadInputServant {
            override func in_(a: [UInt8]) {
                // This should never be called - the safety check should reject the input
                print("TestBadInputImpl: in_ called with \(a.count) bytes")
            }
        }
        
        // Create and activate servant
        let servant = TestBadInputImpl()
        let oid = try Self.poa!.activateObject(servant)
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
        data.storeBytes(of: impl.MessageId.functionCall.rawValue, toByteOffset: 4, as: Int32.self)  // msg_id: FunctionCall
        data.storeBytes(of: impl.MessageType.request.rawValue, toByteOffset: 8, as: Int32.self)    // msg_type: Request
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
        
        // Set correct size in header (total size - 4 bytes for size field itself)
        finalData.storeBytes(of: UInt32(buffer.size - 4), toByteOffset: 0, as: UInt32.self)
        
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
            print("Bad input test passed: ExceptionBadInput was correctly thrown")
        } catch {
            XCTFail("Unexpected error: \(error)")
        }
    }

    /// Test streaming RPC methods
    /// This tests the full round-trip: client sends StreamInit, servant produces stream,
    /// servant dispatch pumps chunks, client receives via AsyncThrowingStream
    func testStreams() async throws {
        // TestStreams servant implementation
        class TestStreamsImpl: TestStreamsServant {
            override func getByteStream(size: UInt64) -> AsyncStream<UInt8> {
                print("[SWIFT] getByteStream called with size: \(size)")
                return AsyncStream { continuation in
                    // Produce 'size' bytes (capped at 256 for test)
                    let count = min(size, 256)
                    for i in 0..<count {
                        print("[SWIFT] Producing byte \(i & 0xFF)")
                        continuation.yield(UInt8(i & 0xFF))
                    }
                    print("[SWIFT] Finished producing stream of \(count) bytes")
                    continuation.finish()
                }
            }
        }
        
        // Create and activate servant
        let servant = TestStreamsImpl()
        let oid = try Self.poa!.activateObject(servant, flags: .allowWebSocket)
        XCTAssertEqual(oid.class_id, "nprpc_test/nprpc.test.TestStreams")
        
        // Create client proxy
        guard let obj = NPRPCObject.fromObjectId(oid) else {
            XCTFail("Failed to create NPRPCObject from ObjectId")
            return
        }
        let client = narrow(obj, to: TestStreams.self)!
        XCTAssertEqual(client.classId, "nprpc_test/nprpc.test.TestStreams")
        
        // Test full round-trip: call streaming method and consume the stream
        let stream = try client.getByteStream(size: 5)
        var receivedValues: [UInt8] = []
        
        for try await value in stream {
            print("[SWIFT] Received byte: \(value)")
            receivedValues.append(value)
        }

        print("[SWIFT] Stream finished. Total bytes received: \(receivedValues.count)")
        
        // Verify we received all expected values
        XCTAssertEqual(receivedValues.count, 5, "Should receive 5 bytes from stream")
        for i in 0..<5 {
            XCTAssertEqual(receivedValues[i], UInt8(i), "Byte \(i) should be \(i)")
        }
        
        print("Stream test completed successfully! Received \(receivedValues.count) values")
    }
}

// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

// Swift <-> Swift IPC tests over the shared-memory transport.
//
// C++ gtests cover the *synchronous* SHM path (send_receive). Swift clients
// always go through send_receive_async, which historically deadlocked when
// on_data_received_view held mutex_ and async on_executed tried to re-lock it.
// These tests pin that path.

import XCTest
import Foundation
@testable import NPRPC

// MARK: - Servants

private final class ShmBasicServant: TestBasicServant, @unchecked Sendable {
    var in_receivedA: UInt32 = 0
    var in_receivedB: Bool = false
    var in_receivedC: [UInt8] = []

    override func returnBoolean() throws -> Bool { true }
    override func returnIdArray() throws -> IdArray { [1, 2, 3, 4, 5] }
    override func returnU32() throws -> UInt32 { 0xDEAD_BEEF }
    override func in_(a: UInt32, b: Bool, c: [UInt8]) throws -> Bool {
        in_receivedA = a
        in_receivedB = b
        in_receivedC = c
        return true
    }
    override func out() throws -> (UInt32, Bool, [UInt8]) {
        (123456789, true, [10, 20, 30, 40, 50])
    }
    override func inStruct(a: AAA) throws {}
    override func outStruct() throws -> AAA {
        AAA(a: 42, b: "shm-hello", c: "shm-world")
    }
    override func inFlatStruct(value: UInt32, a: FlatStruct) throws {}
    override func outFlatStruct(value: UInt32) throws -> FlatStruct {
        FlatStruct(a: Int32(value), b: 99, c: 3.14)
    }
    override func outArrayOfStructs() throws -> [SimpleStruct] {
        [SimpleStruct(id: 1), SimpleStruct(id: 2), SimpleStruct(id: 3)]
    }
    override func inException() throws {
        throw SimpleException(message: "shm exception", code: 7)
    }
    override func multipleExceptions(code: UInt32) throws {
        if code == 0 {
            throw SimpleException(message: "simple", code: 1)
        }
        throw AssertionFailed(message: "assert")
    }
    override func outScalarWithException(dev_addr: UInt8, addr: UInt16) throws -> UInt8 {
        UInt8(truncatingIfNeeded: Int(dev_addr) &+ Int(addr))
    }
    override func returnStringArray(count: UInt32) throws -> [String] {
        (0..<count).map { "shm-\($0)" }
    }
}

private final class ShmAsyncServant: AsyncTestServant, @unchecked Sendable {
    private let lock = NSLock()
    private var _calls: UInt32 = 0

    var calls: UInt32 {
        lock.lock(); defer { lock.unlock() }
        return _calls
    }

    override func method1(arg1: UInt32, arg2: String) {
        lock.lock(); _calls += 1; lock.unlock()
    }

    override func method2(arg1: UInt32) -> String {
        lock.lock(); _calls += 1; lock.unlock()
        return "shm-response-\(arg1)"
    }
}

// MARK: - Tests

final class SharedMemoryIPCTests: XCTestCase {
    nonisolated(unsafe) static var rpc: Rpc?
    nonisolated(unsafe) static var poa: Poa?

    override class func setUp() {
        super.setUp()
        do {
            // Shared-memory listener is always started by RpcImpl; no TCP/HTTP
            // ports needed for this transport.
            rpc = try RpcBuilder()
                .setLogLevel(.error)
                .withHostname("localhost")
                .build()
            poa = try rpc!.createPoa(maxObjects: 64)
            // Thread pool drives SHM timeout timers / any asio work.
            try rpc!.startThreadPool(2)
            Thread.sleep(forTimeInterval: 0.05)
        } catch {
            fatalError("SharedMemoryIPCTests setUp failed: \(error)")
        }
    }

    override class func tearDown() {
        rpc = nil
        poa = nil
        super.tearDown()
    }

    /// Activate a servant with only the SHM flag and return a client proxy
    /// whose selected endpoint is the shared-memory listener.
    private func makeShmClient<T: NPRPCObject>(
        _ servant: NPRPCServant,
        as type: T.Type
    ) throws -> (T, detail.ObjectId) {
        let oid = try Self.poa!.activateObject(servant, flags: .shm)
        XCTAssertTrue(
            oid.urls.contains("mem://"),
            "SHM activation should advertise mem:// URL, got: \(oid.urls)"
        )

        guard let obj = NPRPCObject.fromObjectId(oid) else {
            throw RuntimeError(message: "fromObjectId failed")
        }
        XCTAssertEqual(
            obj.endpoint.type,
            .SharedMemory,
            "Client should select SharedMemory endpoint, got \(obj.endpoint.toURL())"
        )
        guard let client = narrow(obj, to: type) else {
            throw RuntimeError(message: "narrow to \(type) failed")
        }
        return (client, oid)
    }

    /// Round-trip scalars over SHM (async client path).
    func testShmReturnScalars() async throws {
        let servant = ShmBasicServant()
        let (client, _) = try makeShmClient(servant, as: TestBasic.self)

        let b = try await client.returnBoolean()
        XCTAssertTrue(b)

        let u32 = try await client.returnU32()
        XCTAssertEqual(u32, 0xDEAD_BEEF)

        let ids = try await client.returnIdArray()
        XCTAssertEqual(ids, [1, 2, 3, 4, 5])
    }

    /// In/out argument marshalling over SHM.
    func testShmInOut() async throws {
        let servant = ShmBasicServant()
        let (client, _) = try makeShmClient(servant, as: TestBasic.self)

        let ok = try await client.in_(a: 99, b: true, c: [1, 2, 3])
        XCTAssertTrue(ok)
        XCTAssertEqual(servant.in_receivedA, 99)
        XCTAssertEqual(servant.in_receivedB, true)
        XCTAssertEqual(servant.in_receivedC, [1, 2, 3])

        let (a, b, c) = try await client.out()
        XCTAssertEqual(a, 123456789)
        XCTAssertTrue(b)
        XCTAssertEqual(c, [10, 20, 30, 40, 50])
    }

    /// Struct + string marshalling over SHM.
    func testShmStructAndStrings() async throws {
        let servant = ShmBasicServant()
        let (client, _) = try makeShmClient(servant, as: TestBasic.self)

        let s = try await client.outStruct()
        XCTAssertEqual(s.a, 42)
        XCTAssertEqual(s.b, "shm-hello")
        XCTAssertEqual(s.c, "shm-world")

        let strings = try await client.returnStringArray(count: 4)
        XCTAssertEqual(strings, ["shm-0", "shm-1", "shm-2", "shm-3"])
    }

    /// Exception propagation over SHM async path.
    func testShmExceptions() async throws {
        let servant = ShmBasicServant()
        let (client, _) = try makeShmClient(servant, as: TestBasic.self)

        do {
            try await client.inException()
            XCTFail("expected SimpleException")
        } catch let e as SimpleException {
            XCTAssertEqual(e.code, 7)
            XCTAssertEqual(e.message, "shm exception")
        }

        do {
            try await client.multipleExceptions(code: 1)
            XCTFail("expected AssertionFailed")
        } catch is AssertionFailed {
            // expected
        }
    }

    /// Sequential back-to-back async RPCs on one SHM connection.
    /// Stresses request/response pairing on the slot-ordered work queue.
    func testShmSequentialCalls() async throws {
        let servant = ShmBasicServant()
        let (client, _) = try makeShmClient(servant, as: TestBasic.self)

        for i in 0..<32 {
            let v = try await client.returnU32()
            XCTAssertEqual(v, 0xDEAD_BEEF, "iteration \(i)")
        }
    }

    /// Concurrent async RPCs over a single SHM connection.
    /// This is the path most likely to expose slot-order / mutex bugs.
    func testShmConcurrentCalls() async throws {
        let servant = ShmAsyncServant()
        let (client, _) = try makeShmClient(servant, as: AsyncTest.self)

        let count = 16
        try await withThrowingTaskGroup(of: String.self) { group in
            for i in 0..<count {
                group.addTask {
                    try await client.method2(arg1: UInt32(i))
                }
            }
            var results: [String] = []
            for try await r in group {
                results.append(r)
            }
            XCTAssertEqual(results.count, count)
            let expected = Set((0..<count).map { "shm-response-\($0)" })
            XCTAssertEqual(Set(results), expected)
        }
        XCTAssertEqual(Int(servant.calls), count)
    }

    /// Fire-and-forget async methods (no response payload) over SHM.
    func testShmFireAndForget() async throws {
        let servant = ShmAsyncServant()
        let (client, _) = try makeShmClient(servant, as: AsyncTest.self)

        try await client.method1(arg1: 1, arg2: "a")
        try await client.method1(arg1: 2, arg2: "b")
        try await client.method1(arg1: 3, arg2: "c")

        // method1 is async without outputs; wait briefly for dispatch.
        for _ in 0..<50 where servant.calls < 3 {
            try await Task.sleep(nanoseconds: 10_000_000)
        }
        XCTAssertEqual(servant.calls, 3)
    }

    /// Preferred transport: with both TCP and SHM advertised, default is SHM
    /// on the same machine. (Requires a TCP listener, so spin a second Rpc
    /// only if we already have one from IntegrationTests — here we just
    /// re-check pure SHM selection on our dedicated runtime.)
    func testShmEndpointSelected() async throws {
        let servant = ShmBasicServant()
        let oid = try Self.poa!.activateObject(servant, flags: .shm)
        guard let obj = NPRPCObject.fromObjectId(oid) else {
            XCTFail("fromObjectId failed")
            return
        }
        XCTAssertTrue(obj.selectEndpoint())
        XCTAssertEqual(obj.endpoint.type, .SharedMemory)
        XCTAssertTrue(obj.endpoint.toURL().hasPrefix("mem://"))
        // A real call must succeed, not just endpoint selection.
        let client = narrow(obj, to: TestBasic.self)!
        let value = try await client.returnU32()
        XCTAssertEqual(value, 0xDEAD_BEEF)
    }
}

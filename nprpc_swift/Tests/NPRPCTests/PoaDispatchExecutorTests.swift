// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

// SHM server dispatch via PoaDispatchExecutor (.main and custom serial queue).
// Pins ring → DispatchExecutor.post (no Asio hop) and is_running_on inline.

import XCTest
import Foundation
import Dispatch
@testable import NPRPC

// MARK: - Probe servants

/// Minimal TestBasic servant that records which queue/thread handled returnU32.
private final class QueueProbeServant: TestBasicServant, @unchecked Sendable {
    /// When non-nil, returnU32 checks DispatchQueue.getSpecific(key:).
    let queueKey: DispatchSpecificKey<UInt8>?
    /// When true, also records Thread.isMainThread.
    let checkMain: Bool

    private let lock = NSLock()
    private var _onExpectedQueue = false
    private var _onMain = false
    private var _calls: UInt32 = 0

    var onExpectedQueue: Bool {
        lock.lock(); defer { lock.unlock() }
        return _onExpectedQueue
    }
    var onMain: Bool {
        lock.lock(); defer { lock.unlock() }
        return _onMain
    }
    var calls: UInt32 {
        lock.lock(); defer { lock.unlock() }
        return _calls
    }

    init(queueKey: DispatchSpecificKey<UInt8>? = nil, checkMain: Bool = false) {
        self.queueKey = queueKey
        self.checkMain = checkMain
        super.init()
    }

    override func returnU32() throws -> UInt32 {
        let onQ: Bool
        if let key = queueKey {
            onQ = DispatchQueue.getSpecific(key: key) != nil
        } else {
            onQ = false
        }
        let main = Thread.isMainThread
        lock.lock()
        _onExpectedQueue = onQ
        _onMain = main
        _calls += 1
        lock.unlock()
        return 0xC0FFEE
    }

    // Unused by these tests — provide cheap stubs.
    override func returnBoolean() throws -> Bool { true }
    override func returnIdArray() throws -> IdArray { [1] }
    override func in_(a: UInt32, b: Bool, c: [UInt8]) throws -> Bool { true }
    override func out() throws -> (UInt32, Bool, [UInt8]) { (0, false, []) }
    override func inStruct(a: AAA) throws {}
    override func outStruct() throws -> AAA { AAA() }
    override func inFlatStruct(value: UInt32, a: FlatStruct) throws {}
    override func outFlatStruct(value: UInt32) throws -> FlatStruct {
        FlatStruct(a: 0, b: 0, c: 0)
    }
    override func outArrayOfStructs() throws -> [SimpleStruct] { [] }
    override func inException() throws {}
    override func multipleExceptions(code: UInt32) throws {}
    override func outScalarWithException(dev_addr: UInt8, addr: UInt16) throws -> UInt8 {
        0
    }
    override func returnStringArray(count: UInt32) throws -> [String] { [] }
}

// MARK: - Tests

final class PoaDispatchExecutorTests: XCTestCase {
    nonisolated(unsafe) static var rpc: Rpc?

    override class func setUp() {
        super.setUp()
        do {
            rpc = try RpcBuilder()
                .setLogLevel(.error)
                .withHostname("localhost")
                .build()
            try rpc!.startThreadPool(2)
            Thread.sleep(forTimeInterval: 0.05)
        } catch {
            fatalError("PoaDispatchExecutorTests setUp failed: \(error)")
        }
    }

    override class func tearDown() {
        rpc = nil
        super.tearDown()
    }

    /// Activate on SHM only and build a client that selects mem://.
    private func makeShmClient(
        servant: NPRPCServant,
        poa: Poa
    ) throws -> TestBasic {
        let oid = try poa.activateObject(servant, flags: .shm)
        XCTAssertTrue(oid.urls.contains("mem://"), "expected mem:// URL, got \(oid.urls)")

        guard let obj = NPRPCObject.fromObjectId(oid) else {
            throw RuntimeError(message: "fromObjectId failed")
        }
        XCTAssertEqual(obj.endpoint.type, .SharedMemory)
        guard let client = narrow(obj, to: TestBasic.self) else {
            throw RuntimeError(message: "narrow to TestBasic failed")
        }
        return client
    }

    /// Custom serial queue: servant body must run with that queue's specific key.
    func testCustomSerialQueueExecutor() async throws {
        let queue = DispatchQueue(label: "nprpc.test.poa.custom-serial")
        let key = DispatchSpecificKey<UInt8>()
        // Visible to the servant when C++ posts the whole request onto this queue.
        queue.setSpecific(key: key, value: 1)

        let poa = try Self.rpc!.createPoa(
            maxObjects: 16,
            dispatch: .queue(queue)
        )
        let servant = QueueProbeServant(queueKey: key)
        let client = try makeShmClient(servant: servant, poa: poa)

        let v = try await client.returnU32()
        XCTAssertEqual(v, 0xC0FFEE)
        XCTAssertEqual(servant.calls, 1)
        XCTAssertTrue(
            servant.onExpectedQueue,
            "servant should run on the custom serial DispatchQueue (SHM ring → post, no Asio)"
        )
    }

    /// Sequential RPCs on a custom serial executor stay ordered and on-queue.
    func testCustomSerialQueueSequentialCalls() async throws {
        let queue = DispatchQueue(label: "nprpc.test.poa.custom-seq")
        let key = DispatchSpecificKey<UInt8>()
        queue.setSpecific(key: key, value: 1)

        let poa = try Self.rpc!.createPoa(
            maxObjects: 16,
            dispatch: .queue(queue)
        )
        let servant = QueueProbeServant(queueKey: key)
        let client = try makeShmClient(servant: servant, poa: poa)

        for i in 0..<16 {
            let v = try await client.returnU32()
            XCTAssertEqual(v, 0xC0FFEE, "iteration \(i)")
            XCTAssertTrue(servant.onExpectedQueue, "iteration \(i) off-queue")
        }
        XCTAssertEqual(servant.calls, 16)
    }

    /// Main queue: servant body should see Thread.isMainThread.
    ///
    /// On Linux, `DispatchQueue.main` runs when the main run loop is active;
    /// XCTest usually leaves main free enough for libdispatch to schedule
    /// main-queue work while the async test is suspended awaiting the RPC.
    func testMainQueueExecutor() async throws {
        let poa = try Self.rpc!.createPoa(
            maxObjects: 16,
            dispatch: .main
        )
        let servant = QueueProbeServant(checkMain: true)
        let client = try makeShmClient(servant: servant, poa: poa)

        let v = try await client.returnU32()
        XCTAssertEqual(v, 0xC0FFEE)
        XCTAssertEqual(servant.calls, 1)
        XCTAssertTrue(
            servant.onMain,
            "servant should run on the main queue (ring → DispatchExecutor.post → main)"
        )
    }

    /// Default (inline) POA must still work over SHM for contrast.
    func testInlineTransportThreadStillWorks() async throws {
        let poa = try Self.rpc!.createPoa(
            maxObjects: 16,
            dispatch: .inlineOnTransportThread
        )
        let servant = QueueProbeServant(checkMain: true)
        let client = try makeShmClient(servant: servant, poa: poa)

        let v = try await client.returnU32()
        XCTAssertEqual(v, 0xC0FFEE)
        XCTAssertEqual(servant.calls, 1)
        // Ring thread is not the main thread.
        XCTAssertFalse(
            servant.onMain,
            "inline POA should dispatch on the SHM ring thread, not main"
        )
    }
}

// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

// SHM server dispatch via PoaDispatchExecutor (.main, custom serial queue, and
// a custom loop thread).
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
    /// When non-nil, returnU32 records whether it ran on exactly this thread —
    /// what a `PoaExecutor` loop promises and a DispatchQueue cannot.
    nonisolated(unsafe) var expectedThread: Thread?

    private let lock = NSLock()
    private var _onExpectedQueue = false
    private var _onMain = false
    private var _onExpectedThread = false
    private var _calls: UInt32 = 0

    var onExpectedQueue: Bool {
        lock.lock(); defer { lock.unlock() }
        return _onExpectedQueue
    }
    var onExpectedThread: Bool {
        lock.lock(); defer { lock.unlock() }
        return _onExpectedThread
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
        let onThread = expectedThread.map { $0 === Thread.current } ?? false
        lock.lock()
        _onExpectedQueue = onQ
        _onMain = main
        _onExpectedThread = onThread
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

// MARK: - A loop that is not a DispatchQueue

/// Stands in for a render loop: one thread that parks until something wakes
/// it, then drains whatever was posted, in order.
///
/// The point of the exercise is that this thread is chosen by us and never
/// migrates — which is what a `DispatchQueue` cannot promise and why
/// `PoaExecutor` exists.
private final class TestLoop: PoaExecutor, @unchecked Sendable {
    private let mutex = NSCondition()
    private var pending: [@Sendable () -> Void] = []
    private var stopped = false
    private(set) nonisolated(unsafe) var thread: Thread?

    /// Order the loop actually ran things in, so FIFO can be asserted.
    private let orderLock = NSLock()
    private var _order: [Int] = []
    var order: [Int] {
        orderLock.lock(); defer { orderLock.unlock() }
        return _order
    }
    func note(_ i: Int) {
        orderLock.lock(); _order.append(i); orderLock.unlock()
    }

    func start() {
        let started = DispatchSemaphore(value: 0)
        let t = Thread { [self] in
            thread = Thread.current
            started.signal()
            run()
        }
        t.start()
        started.wait()
    }

    private func run() {
        while true {
            mutex.lock()
            while pending.isEmpty && !stopped { mutex.wait() }
            if stopped && pending.isEmpty { mutex.unlock(); return }
            let work = pending
            pending.removeAll(keepingCapacity: true)
            mutex.unlock()
            for item in work { item() }
        }
    }

    func stop() {
        mutex.lock()
        stopped = true
        mutex.signal()
        mutex.unlock()
    }

    // MARK: PoaExecutor

    func post(_ work: @escaping @Sendable () -> Void) {
        mutex.lock()
        pending.append(work)
        // The wake is not optional: nothing else will tell this loop that a
        // request is waiting.
        mutex.signal()
        mutex.unlock()
    }

    var isRunningOnExecutor: Bool { Thread.current === thread }
}

// MARK: - Tests

final class PoaDispatchExecutorTests: XCTestCase {
    nonisolated(unsafe) static var rpc: Rpc?
    /// Shared by the three `.loop` tests: a process gets six POAs, and the
    /// suite would otherwise spend half of them proving the same wiring.
    nonisolated(unsafe) fileprivate static var loop: TestLoop?
    nonisolated(unsafe) static var loopPoa: Poa?

    override class func setUp() {
        super.setUp()
        do {
            rpc = try RpcBuilder()
                .setLogLevel(.error)
                .withHostname("localhost")
                .build()
            try rpc!.startThreadPool(2)
            Thread.sleep(forTimeInterval: 0.05)

            let l = TestLoop()
            l.start()
            loop = l
            loopPoa = try rpc!.createPoa(maxObjects: 16, dispatch: .loop(l))
        } catch {
            fatalError("PoaDispatchExecutorTests setUp failed: \(error)")
        }
    }

    override class func tearDown() {
        loopPoa = nil
        loop?.stop()
        loop = nil
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

    /// Custom loop: the servant must run on *that thread*, not merely serially.
    func testLoopExecutorRunsOnTheLoopThread() async throws {
        let loop = Self.loop!
        let servant = QueueProbeServant(checkMain: true)
        servant.expectedThread = loop.thread
        let client = try makeShmClient(servant: servant, poa: Self.loopPoa!)

        let v = try await client.returnU32()
        XCTAssertEqual(v, 0xC0FFEE)
        XCTAssertEqual(servant.calls, 1)
        XCTAssertTrue(
            servant.onExpectedThread,
            "servant should run on the loop's own thread (ring → PoaExecutor.post)"
        )
        XCTAssertFalse(servant.onMain)
    }

    /// A parked loop must be woken by `post` — the case that separates a real
    /// executor from an enqueue. Without the wake this call never returns.
    func testLoopExecutorWakesAParkedLoop() async throws {
        let servant = QueueProbeServant()
        servant.expectedThread = Self.loop!.thread
        let client = try makeShmClient(servant: servant, poa: Self.loopPoa!)

        // Long enough that the loop is definitely blocked in its wait.
        try await Task.sleep(nanoseconds: 100_000_000)
        let v = try await client.returnU32()
        XCTAssertEqual(v, 0xC0FFEE)
        XCTAssertTrue(servant.onExpectedThread)
    }

    /// Replies are matched by ring slot order, so the loop must drain FIFO.
    func testLoopExecutorSequentialCalls() async throws {
        let loop = Self.loop!
        let servant = QueueProbeServant()
        servant.expectedThread = loop.thread
        let client = try makeShmClient(servant: servant, poa: Self.loopPoa!)

        for i in 0..<16 {
            let v = try await client.returnU32()
            XCTAssertEqual(v, 0xC0FFEE, "iteration \(i)")
            XCTAssertTrue(servant.onExpectedThread, "iteration \(i) off-loop")
            loop.note(i)
        }
        XCTAssertEqual(servant.calls, 16)
        XCTAssertEqual(loop.order, Array(0..<16))
    }

    /// The executor must survive the Swift `Poa` wrapper being released.
    ///
    /// Activating and then dropping the handle is the ordinary shape — there
    /// is no `destroyPoa`, so the C++ POA lives on and dereferences the hop
    /// target on every request. When the box was owned by the wrapper this
    /// crashed on the first call, and only outside the tests, because every
    /// other test here happens to keep its `poa` in scope.
    ///
    /// (Uses the last of `max_poa_objects`; a new POA-creating test needs to
    /// share one of the above.)
    func testExecutorOutlivesThePoaWrapper() async throws {
        let loop = TestLoop()
        loop.start()
        defer { loop.stop() }

        let servant = QueueProbeServant()
        servant.expectedThread = loop.thread

        let client: TestBasic = try {
            let poa = try Self.rpc!.createPoa(maxObjects: 4, dispatch: .loop(loop))
            return try makeShmClient(servant: servant, poa: poa)
        }()

        let v = try await client.returnU32()
        XCTAssertEqual(v, 0xC0FFEE)
        XCTAssertTrue(servant.onExpectedThread)
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

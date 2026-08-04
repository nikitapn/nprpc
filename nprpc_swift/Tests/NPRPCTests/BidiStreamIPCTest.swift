// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

// Swift <-> Swift bidirectional stream IPC tests.
//
// Uses shared-memory transport so the full path (StreamInit + chunk
// marshalling + flow control) runs in-process without HTTP/TLS.  String and
// struct chunks force buffer growth on both ends — the same class of bug that
// broke plain SHM RPC when responses were written into a ring view.

import XCTest
import Foundation
@testable import NPRPC

// MARK: - Servant

private final class BidiEchoServant: TestStreamsServant, @unchecked Sendable {
    static func transformAAA(_ value: AAA, suffix: String) -> AAA {
        AAA(a: value.a + 100, b: value.b + suffix, c: value.c + suffix)
    }

    // Server → client (download)
    override func getByteStream(size: UInt64) -> AsyncStream<UInt8> {
        AsyncStream { continuation in
            let count = min(size, 256)
            for i in 0..<count {
                continuation.yield(UInt8(truncatingIfNeeded: i))
            }
            continuation.finish()
        }
    }

    override func getStringStream(count: UInt32) -> AsyncStream<String> {
        AsyncStream { continuation in
            for i in 0..<count {
                // Longer strings to exercise grow/alloc on the wire.
                continuation.yield(String(repeating: "s\(i)-", count: 8) + "end")
            }
            continuation.finish()
        }
    }

    override func getObjectStream(count: UInt32) -> AsyncStream<AAA> {
        AsyncStream { continuation in
            for i in 0..<count {
                continuation.yield(AAA(a: i, b: "name_\(i)", c: "value_\(i)"))
            }
            continuation.finish()
        }
    }

    // Client → server (upload)
    override func uploadByteStream(expected_size: UInt64, data: NPRPCStreamReader<UInt8>) async {
        var n: UInt64 = 0
        do {
            for try await _ in data { n += 1 }
            XCTAssertEqual(n, expected_size)
        } catch {
            XCTFail("uploadByteStream error: \(error)")
        }
    }

    override func uploadStringStream(expected_count: UInt64, data: NPRPCStreamReader<String>) async {
        var n: UInt64 = 0
        do {
            for try await _ in data { n += 1 }
            XCTAssertEqual(n, expected_count)
        } catch {
            XCTFail("uploadStringStream error: \(error)")
        }
    }

    // Bidi echo
    override func echoByteStream(xor_mask: UInt8, stream: NPRPCBidiStream<UInt8, UInt8>) async {
        do {
            for try await byte in stream.reader {
                try await stream.writer.write(byte ^ xor_mask)
            }
            stream.writer.close()
        } catch {
            stream.writer.abort()
        }
    }

    override func echoStringStream(suffix: String, stream: NPRPCBidiStream<String, String>) async {
        do {
            for try await value in stream.reader {
                try await stream.writer.write(value + suffix)
            }
            stream.writer.close()
        } catch {
            stream.writer.abort()
        }
    }

    override func echoBinaryStream(xor_mask: UInt8, stream: NPRPCBidiStream<[UInt8], [UInt8]>) async {
        do {
            for try await value in stream.reader {
                try await stream.writer.write(value.map { $0 ^ xor_mask })
            }
            stream.writer.close()
        } catch {
            stream.writer.abort()
        }
    }

    override func echoObjectStream(suffix: String, stream: NPRPCBidiStream<AAA, AAA>) async {
        do {
            for try await value in stream.reader {
                try await stream.writer.write(Self.transformAAA(value, suffix: suffix))
            }
            stream.writer.close()
        } catch {
            stream.writer.abort()
        }
    }

    override func echoObjectVectorStream(suffix: String, stream: NPRPCBidiStream<[AAA], [AAA]>) async {
        do {
            for try await value in stream.reader {
                try await stream.writer.write(value.map { Self.transformAAA($0, suffix: suffix) })
            }
            stream.writer.close()
        } catch {
            stream.writer.abort()
        }
    }
}

// MARK: - Tests

final class BidiStreamIPCTests: XCTestCase {
    nonisolated(unsafe) static var rpc: Rpc?
    nonisolated(unsafe) static var poa: Poa?
    nonisolated(unsafe) static var client: TestStreams?

    override class func setUp() {
        super.setUp()
        do {
            rpc = try RpcBuilder()
                .setLogLevel(.error)
                .withHostname("localhost")
                .build()
            poa = try rpc!.createPoa(maxObjects: 32)
            try rpc!.startThreadPool(2)
            Thread.sleep(forTimeInterval: 0.05)

            let servant = BidiEchoServant()
            let oid = try poa!.activateObject(servant, flags: .shm)
            XCTAssertTrue(oid.urls.contains("mem://"), "expected mem:// URL, got \(oid.urls)")
            guard let obj = NPRPCObject.fromObjectId(oid) else {
                fatalError("fromObjectId failed")
            }
            XCTAssertEqual(obj.endpoint.type, .SharedMemory)
            guard let proxy = narrow(obj, to: TestStreams.self) else {
                fatalError("narrow to TestStreams failed")
            }
            client = proxy
        } catch {
            fatalError("BidiStreamIPCTests setUp failed: \(error)")
        }
    }

    override class func tearDown() {
        client = nil
        poa = nil
        rpc = nil
        super.tearDown()
    }

    private var client: TestStreams { Self.client! }

    // MARK: Server → client

    func testDownloadByteStream() async throws {
        let stream = try client.getByteStream(size: 16)
        var values: [UInt8] = []
        for try await v in stream { values.append(v) }
        XCTAssertEqual(values, (0..<16).map { UInt8($0) })
    }

    func testDownloadStringStream() async throws {
        let stream = try client.getStringStream(count: 4)
        var values: [String] = []
        for try await v in stream { values.append(v) }
        XCTAssertEqual(values.count, 4)
        for (i, s) in values.enumerated() {
            XCTAssertTrue(s.hasPrefix("s\(i)-"), "unexpected string \(s)")
            XCTAssertTrue(s.hasSuffix("end"))
        }
    }

    func testDownloadObjectStream() async throws {
        let stream = try client.getObjectStream(count: 3)
        var values: [AAA] = []
        for try await v in stream { values.append(v) }
        XCTAssertEqual(values.count, 3)
        XCTAssertEqual(values[0].a, 0)
        XCTAssertEqual(values[0].b, "name_0")
        XCTAssertEqual(values[2].c, "value_2")
    }

    // MARK: Client → server

    func testUploadByteStream() async throws {
        let writer = try client.uploadByteStream(expected_size: 8)
        for b in 0..<UInt8(8) {
            try await writer.write(b)
        }
        writer.close()
        // Servant asserts count; give it a moment to finish.
        try await Task.sleep(nanoseconds: 50_000_000)
    }

    func testUploadStringStream() async throws {
        let writer = try client.uploadStringStream(expected_count: 3)
        try await writer.write("alpha")
        try await writer.write("beta-longer-payload")
        try await writer.write("gamma")
        writer.close()
        try await Task.sleep(nanoseconds: 50_000_000)
    }

    // MARK: Bidi echo

    func testBidiEchoBytes() async throws {
        let stream = try client.echoByteStream(xor_mask: 0x5A)
        let input: [UInt8] = [10, 11, 12, 13, 200, 255]
        for b in input {
            try await stream.writer.write(b)
        }
        stream.writer.close()

        var out: [UInt8] = []
        for try await b in stream.reader { out.append(b) }
        XCTAssertEqual(out, input.map { $0 ^ 0x5A })
    }

    func testBidiEchoStrings() async throws {
        let stream = try client.echoStringStream(suffix: "-ok")
        // Mix short and long strings (grow path on marshal).
        let input = [
            "a",
            String(repeating: "long-", count: 32),
            "mid",
            String(repeating: "x", count: 200),
        ]
        for s in input {
            try await stream.writer.write(s)
        }
        stream.writer.close()

        var out: [String] = []
        for try await s in stream.reader { out.append(s) }
        XCTAssertEqual(out, input.map { $0 + "-ok" })
    }

    func testBidiEchoBinary() async throws {
        let stream = try client.echoBinaryStream(xor_mask: 0x0F)
        let input: [[UInt8]] = [
            [0, 1, 2, 3],
            Array(repeating: 0xAA, count: 64),
            [0xFF],
        ]
        for chunk in input {
            try await stream.writer.write(chunk)
        }
        stream.writer.close()

        var out: [[UInt8]] = []
        for try await chunk in stream.reader { out.append(chunk) }
        XCTAssertEqual(out.count, input.count)
        for i in 0..<input.count {
            XCTAssertEqual(out[i], input[i].map { $0 ^ 0x0F })
        }
    }

    func testBidiEchoObjects() async throws {
        let stream = try client.echoObjectStream(suffix: "-s")
        let input = [
            AAA(a: 1, b: "alpha", c: "one"),
            AAA(a: 2, b: "beta-with-a-longer-name", c: "two-also-long"),
            AAA(a: 3, b: "g", c: "t"),
        ]
        for v in input {
            try await stream.writer.write(v)
        }
        stream.writer.close()

        var out: [AAA] = []
        for try await v in stream.reader { out.append(v) }
        XCTAssertEqual(out.count, 3)
        XCTAssertEqual(out[0].a, 101)
        XCTAssertEqual(out[0].b, "alpha-s")
        XCTAssertEqual(out[0].c, "one-s")
        XCTAssertEqual(out[1].a, 102)
        XCTAssertEqual(out[1].b, "beta-with-a-longer-name-s")
        XCTAssertEqual(out[2].a, 103)
    }

    func testBidiEchoObjectVectors() async throws {
        let stream = try client.echoObjectVectorStream(suffix: "-v")
        try await stream.writer.write([
            AAA(a: 1, b: "a", c: "x"),
            AAA(a: 2, b: "b", c: "y"),
        ])
        try await stream.writer.write([
            AAA(a: 3, b: "c", c: "z"),
        ])
        stream.writer.close()

        var out: [[AAA]] = []
        for try await v in stream.reader { out.append(v) }
        XCTAssertEqual(out.count, 2)
        XCTAssertEqual(out[0].count, 2)
        XCTAssertEqual(out[0][0].a, 101)
        XCTAssertEqual(out[0][0].b, "a-v")
        XCTAssertEqual(out[0][1].a, 102)
        XCTAssertEqual(out[1][0].a, 103)
        XCTAssertEqual(out[1][0].c, "z-v")
    }

    /// Many small bidi chunks in one stream — stress slot ordering / credits.
    ///
    /// Writes and reads concurrently so the credit window (default 32) can
    /// refill via window updates; a pure write-then-read pattern deadlocks
    /// once in-flight chunks fill the producer's window.
    func testBidiManyChunks() async throws {
        let stream = try client.echoByteStream(xor_mask: 0x01)
        let count = 64

        async let written: Void = {
            for i in 0..<count {
                try await stream.writer.write(UInt8(truncatingIfNeeded: i))
            }
            stream.writer.close()
        }()

        var out: [UInt8] = []
        for try await b in stream.reader { out.append(b) }
        try await written

        XCTAssertEqual(out.count, count)
        for i in 0..<count {
            XCTAssertEqual(out[i], UInt8(truncatingIfNeeded: i) ^ 0x01, "chunk \(i)")
        }
    }

    /// Interleaved write/read (echo as we go) — typical interactive bidi usage.
    func testBidiInterleavedWriteRead() async throws {
        let stream = try client.echoByteStream(xor_mask: 0xFF)
        for i in 0..<8 {
            try await stream.writer.write(UInt8(i))
        }
        stream.writer.close()

        var seen = 0
        for try await b in stream.reader {
            XCTAssertEqual(b, UInt8(seen) ^ 0xFF)
            seen += 1
        }
        XCTAssertEqual(seen, 8)
    }
}

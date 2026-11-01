import Foundation
import XCTest
@testable import NPRPC
import MonitorAPI

final class DecodingTests: XCTestCase {
    private func storage(_ size: Int) -> FlatBuffer {
        let buffer = FlatBuffer()
        buffer.prepare(size)
        buffer.commit(size)
        buffer.data!.initializeMemory(as: UInt8.self, repeating: 0, count: size)
        return buffer
    }

    private func currentSnapshot() -> FlatBuffer {
        let buffer = storage(112)
        var sample = TransportSample()
        sample.transport = .Quic
        sample.completed = 123
        sample.latency_buckets = [1, 2, 3, 4]
        var snapshot = Snapshot()
        snapshot.uptime_ms = 456
        snapshot.transports = [sample]
        snapshot.router_stats_json = "{\"epoch\":1}"
        marshal_Snapshot(buffer: buffer, offset: 0, data: snapshot)
        return buffer
    }

    private func decode(_ buffer: FlatBuffer, size: Int? = nil) throws -> Snapshot {
        try unmarshal_Snapshot(buffer: buffer.constData!, bufferSize: size ?? buffer.size, offset: 0)
    }

    // Before latency buckets/status counts/router stats: Snapshot was 32 bytes
    // and each TransportSample was 80 bytes, rather than 112/88 bytes.
    private func legacySnapshot(padded: Bool) -> FlatBuffer {
        let buffer = storage(padded ? 512 : 32 + 4 * 80)
        let data = buffer.data!
        data.storeBytes(of: UInt32(24), toByteOffset: 8, as: UInt32.self)
        data.storeBytes(of: UInt32(4), toByteOffset: 12, as: UInt32.self)
        for i in 0..<4 {
            data.storeBytes(of: UInt32(i), toByteOffset: 32 + i * 80, as: UInt32.self)
            data.storeBytes(of: UInt64(100 + i), toByteOffset: 40 + i * 80, as: UInt64.self)
        }
        return buffer
    }

    func testCurrentSnapshotDecodes() throws {
        let snapshot = try decode(currentSnapshot())
        XCTAssertEqual(snapshot.uptime_ms, 456)
        XCTAssertEqual(snapshot.transports.count, 1)
        XCTAssertEqual(snapshot.transports[0].transport, .Quic)
        XCTAssertEqual(snapshot.transports[0].completed, 123)
        XCTAssertEqual(snapshot.transports[0].latency_buckets, [1, 2, 3, 4])
        XCTAssertEqual(snapshot.router_stats_json, "{\"epoch\":1}")
    }

    func testLegacyLayoutIsRejected() {
        for padded in [false, true] {
            XCTAssertThrowsError(try decode(legacySnapshot(padded: padded))) {
                XCTAssertTrue($0 is BufferError)
            }
        }
    }

    func testLegacySnapshotBecomesAnAsyncStreamError() async throws {
        let reader = NPRPCStreamReader<Snapshot>(streamId: 1, buffer: FlatBuffer()) { data, size in
            try unmarshal_Snapshot(buffer: data, bufferSize: size, offset: 0)
        }
        var cancels = 0
        reader.sendCancelFn = { cancels += 1 }
        let buffer = legacySnapshot(padded: true)
        reader.onChunkReceived(data: buffer.constData!, size: buffer.size)
        var iterator = reader.makeAsyncIterator()
        do {
            _ = try await iterator.next()
            XCTFail("An incompatible snapshot must fail the stream")
        } catch let error as BufferError {
            XCTAssertTrue(error.message.contains("incompatible IDL contract"))
        }
        XCTAssertEqual(cancels, 1)
    }

    func testAllTruncatedPrefixesAreRejected() {
        let buffer = currentSnapshot()
        for size in 0..<buffer.size {
            XCTAssertThrowsError(try decode(buffer, size: size), "Accepted truncated size \(size)")
        }
    }

    func testInvalidEnumIsRejected() {
        let buffer = currentSnapshot()
        let data = buffer.data!
        let row = 8 + Int(data.load(fromByteOffset: 8, as: UInt32.self))
        data.storeBytes(of: UInt32.max, toByteOffset: row, as: UInt32.self)
        XCTAssertThrowsError(try decode(buffer)) { XCTAssertTrue($0 is BufferError) }
    }

    func testForgedVectorBoundsAndAlignmentAreRejected() {
        for (offset, value) in [(8, UInt32.max), (12, UInt32.max), (8, UInt32(105))] {
            let buffer = currentSnapshot()
            buffer.data!.storeBytes(of: value, toByteOffset: offset, as: UInt32.self)
            XCTAssertThrowsError(try decode(buffer))
        }
    }

    func testUnalignedRootIsRejected() {
        let buffer = currentSnapshot()
        XCTAssertThrowsError(try unmarshal_Snapshot(buffer: buffer.constData!.advanced(by: 1),
            bufferSize: buffer.size - 1, offset: 0))
    }
}

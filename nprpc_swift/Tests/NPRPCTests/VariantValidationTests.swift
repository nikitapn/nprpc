// Copyright (c) 2026, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

import XCTest
@testable import NPRPC

/// Exercise the generated untrusted dispatch path without a transport or nameserver.
final class VariantValidationTests: XCTestCase {
    private final class Servant: TestVariantRpcServant, @unchecked Sendable {
        var calls = 0

        override func echo(value: TestVariant) throws -> TestVariant {
            calls += 1
            return value
        }

        override func echoPlates(plates: [EffectPlate]) throws -> [EffectPlate] {
            calls += 1
            return plates
        }
    }

    /// Payload-relative offsets keep the malformed cases independent of RPC headers.
    private func request(vector: Bool = true, edits: [(Int, UInt32)] = [],
                         payloadSize: Int = 64) -> FlatBuffer {
        let buffer = FlatBuffer()
        buffer.prepare(96)
        buffer.commit(32 + payloadSize)
        let data = buffer.data!
        data.initializeMemory(as: UInt8.self, repeating: 0, count: buffer.size)
        data.storeBytes(of: UInt32(buffer.size), toByteOffset: 0, as: UInt32.self)
        data.storeBytes(of: impl.MessageId.FunctionCall.rawValue, toByteOffset: 4, as: UInt32.self)
        data.storeBytes(of: impl.MessageType.Request.rawValue, toByteOffset: 8, as: UInt32.self)
        let functionOffset = 16 + MemoryLayout<impl.CallHeader>.offset(of: \impl.CallHeader.function_idx)!
        data.storeBytes(of: UInt8(vector ? 1 : 0), toByteOffset: functionOffset, as: UInt8.self)
        // One plate, one texture arm, and a three-byte string; or one standalone payloadA.
        let fields: [(Int, UInt32)] = vector
            ? [(0, 8), (4, 1), (24, 8), (28, 1), (32, 2), (36, 8), (48, 8), (52, 3)]
            : [(0, 0), (4, 8), (16, 8), (20, 3)]
        for (offset, value) in fields + edits where offset + 4 <= payloadSize {
            data.storeBytes(of: value, toByteOffset: 32 + offset, as: UInt32.self)
        }
        return buffer
    }

    private func assertRejected(_ buffer: FlatBuffer, file: StaticString = #filePath, line: UInt = #line) {
        let servant = Servant()
        servant.dispatch(buffer: buffer, remoteEndpoint: NPRPCEndpoint())
        XCTAssertEqual(servant.calls, 0, "Malformed input reached servant", file: file, line: line)
        XCTAssertThrowsError(try handleStandardReply(buffer: buffer), file: file, line: line) { error in
            XCTAssertTrue(error is ExceptionBadInput, "Unexpected error: \(error)", file: file, line: line)
        }
    }

    func testAcceptsValidArmsAndEmptyVectors() {
        for edits: [(Int, UInt32)] in [[], [(32, 0)], [(32, 1)], [(4, 0)], [(28, 0)]] {
            let servant = Servant()
            let buffer = request(edits: edits)
            servant.dispatch(buffer: buffer, remoteEndpoint: NPRPCEndpoint())
            XCTAssertEqual(servant.calls, 1)
            XCTAssertNoThrow(try handleStandardReply(buffer: buffer))
        }
        let servant = Servant()
        let buffer = request(vector: false)
        servant.dispatch(buffer: buffer, remoteEndpoint: NPRPCEndpoint())
        XCTAssertEqual(servant.calls, 1)
        XCTAssertNoThrow(try handleStandardReply(buffer: buffer))
    }

    func testRejectsMalformedVariantVectors() {
        let cases: [[(Int, UInt32)]] = [
            [(32, 3)], [(32, .max)], // unknown discriminant
            [(36, 0)], [(36, .max)], // missing / out-of-bounds arm
            [(36, 7)], [(36, 24)], // misaligned / truncated arm
            [(48, .max)], [(52, .max)], // nested string offset / length
            [(24, 9)], [(28, .max)], // variant vector alignment / count
            [(0, 9)], [(4, .max)], // outer vector alignment / count
            [(28, 0x20000000)], [(4, 0x20000000)] // counts wrapping 32-bit extents
        ]
        for edits in cases {
            assertRejected(request(edits: edits))
        }
        assertRejected(request(payloadSize: 39)) // truncated variant slot
    }

    func testRejectsMalformedStandaloneVariants() {
        for edits: [(Int, UInt32)] in [[(0, 2)], [(0, .max)], [(4, 0)], [(4, .max)],
                                       [(4, 7)], [(4, 56)], [(16, .max)], [(20, .max)]] {
            assertRejected(request(vector: false, edits: edits))
        }
        assertRejected(request(vector: false, payloadSize: 7))
    }
}

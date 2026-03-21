// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

import XCTest
import Foundation
@testable import NPRPC

final class StreamMarshallingStressTests: XCTestCase {
    private func decode<T>(_ data: Data, using body: (UnsafeRawPointer) -> T) -> T {
        data.withUnsafeBytes { rawBuffer in
            body(rawBuffer.baseAddress!)
        }
    }

    private func makeAsyncWriter<T: Sendable>(
        initialPayloadCapacity: Int,
        serializer: @escaping (FlatBuffer, Int, T) -> Void,
        sink: @escaping (UInt64, Data) -> Void
    ) -> NPRPCStreamWriter<T> {
        NPRPCStreamWriter(
            streamId: 1,
            initialPayloadCapacity: initialPayloadCapacity,
            serializer: serializer,
            sendChunk: { _, _ in
                XCTFail("Unexpected synchronous stream send path")
            },
            sendComplete: { _, _ in },
            sendError: { _, _ in },
            asyncSendChunk: { buffer, sequence, callback in
                guard let data = buffer.constData else {
                    XCTFail("Async writer produced an empty buffer")
                    callback()
                    return
                }
                sink(sequence, Data(bytes: data, count: buffer.size))
                callback()
            }
        )
    }

    private func makeAAA(seed: Int, repeatCount: Int) -> AAA {
        AAA(
            a: UInt32(seed),
            b: String(repeating: "left_\(seed)_", count: repeatCount),
            c: String(repeating: "right_\(seed)_payload_", count: repeatCount + 1)
        )
    }

    private func makeAliasOptionalPayload(seed: Int, repeatCount: Int) -> AliasOptionalStreamPayload {
        let base = UInt32(seed * 10)
        let ids = (0..<(8 + repeatCount)).map { UInt32($0) + base }
        let payload = (0..<(96 + repeatCount * 13)).map { UInt8(($0 + seed) & 0xFF) }
        let maybePayload = (0..<(48 + repeatCount * 7)).map { UInt8((seed * 3 + $0) & 0xFF) }

        return AliasOptionalStreamPayload(
            id: base + 1,
            ids: ids,
            payload: payload,
            label: seed.isMultiple(of: 3) ? nil : String(repeating: "label_\(seed)_", count: repeatCount),
            item: seed.isMultiple(of: 4) ? nil : makeAAA(seed: seed + 1000, repeatCount: repeatCount + 1),
            maybe_id: seed.isMultiple(of: 5) ? nil : base + 99,
            maybe_ids: seed.isMultiple(of: 2) ? ids.reversed() : nil,
            maybe_payload: seed.isMultiple(of: 6) ? nil : maybePayload
        )
    }

    private func assertEqual(_ lhs: AAA, _ rhs: AAA, file: StaticString = #filePath, line: UInt = #line) {
        XCTAssertEqual(lhs.a, rhs.a, file: file, line: line)
        XCTAssertEqual(lhs.b, rhs.b, file: file, line: line)
        XCTAssertEqual(lhs.c, rhs.c, file: file, line: line)
    }

    private func assertEqual(_ lhs: AliasOptionalStreamPayload, _ rhs: AliasOptionalStreamPayload, file: StaticString = #filePath, line: UInt = #line) {
        XCTAssertEqual(lhs.id, rhs.id, file: file, line: line)
        XCTAssertEqual(lhs.ids, rhs.ids, file: file, line: line)
        XCTAssertEqual(lhs.payload, rhs.payload, file: file, line: line)
        XCTAssertEqual(lhs.label, rhs.label, file: file, line: line)
        XCTAssertEqual(lhs.maybe_id, rhs.maybe_id, file: file, line: line)
        XCTAssertEqual(lhs.maybe_ids, rhs.maybe_ids, file: file, line: line)
        XCTAssertEqual(lhs.maybe_payload, rhs.maybe_payload, file: file, line: line)

        switch (lhs.item, rhs.item) {
        case let (.some(left), .some(right)):
            assertEqual(left, right, file: file, line: line)
        case (.none, .none):
            break
        default:
            XCTFail("AliasOptionalStreamPayload.item mismatch", file: file, line: line)
        }
    }

    func testAsyncWriterStressForObjectStreamPayload() async {
        var encodedChunks: [(UInt64, Data)] = []
        var expectedValues: [AAA] = []

        let writer: NPRPCStreamWriter<AAA> = makeAsyncWriter(
            initialPayloadCapacity: 148,
            serializer: { buffer, offset, value in
                NPRPC.marshal_stream_struct(buffer: buffer, offset: offset, rootSize: 20, value: value) { buf, off, elem in
                    marshal_AAA(buffer: buf, offset: off, data: elem)
                }
            },
            sink: { sequence, data in
                encodedChunks.append((sequence, data))
            }
        )

        for index in 0..<256 {
            let value = makeAAA(seed: index, repeatCount: 6 + (index % 11))
            expectedValues.append(value)
            await writer.write(value)
        }

        XCTAssertEqual(encodedChunks.count, expectedValues.count)
        for (index, entry) in encodedChunks.enumerated() {
            XCTAssertEqual(entry.0, UInt64(index))
            let decoded = decode(entry.1) { unmarshal_AAA(buffer: $0, offset: 0) }
            assertEqual(decoded, expectedValues[index])
        }
    }

    func testAsyncWriterStressForObjectArrayStreamPayload() async {
        var encodedChunks: [(UInt64, Data)] = []
        var expectedValues: [[AAA]] = []

        let writer: NPRPCStreamWriter<[AAA]> = makeAsyncWriter(
            initialPayloadCapacity: 128,
            serializer: { buffer, offset, value in
                precondition(value.count == 2, "Invalid fixed array length")
                NPRPC.marshal_stream_struct(buffer: buffer, offset: offset, rootSize: 40, extraCapacity: 0, value: value) { buf, off, elems in
                    for i in 0..<2 {
                        marshal_AAA(buffer: buf, offset: off + i * 20, data: elems[i])
                    }
                }
            },
            sink: { sequence, data in
                encodedChunks.append((sequence, data))
            }
        )

        for index in 0..<192 {
            let value = [
                makeAAA(seed: index * 2, repeatCount: 8 + (index % 9)),
                makeAAA(seed: index * 2 + 1, repeatCount: 9 + (index % 7))
            ]
            expectedValues.append(value)
            await writer.write(value)
        }

        XCTAssertEqual(encodedChunks.count, expectedValues.count)
        for (index, entry) in encodedChunks.enumerated() {
            XCTAssertEqual(entry.0, UInt64(index))
            let decoded = decode(entry.1) { raw in
                (0..<2).map { i in
                    unmarshal_AAA(buffer: raw, offset: i * 20)
                }
            }
            XCTAssertEqual(decoded.count, 2)
            assertEqual(decoded[0], expectedValues[index][0])
            assertEqual(decoded[1], expectedValues[index][1])
        }
    }

    func testAsyncWriterStressForAliasOptionalStreamPayload() async {
        var encodedChunks: [(UInt64, Data)] = []
        var expectedValues: [AliasOptionalStreamPayload] = []

        let writer: NPRPCStreamWriter<AliasOptionalStreamPayload> = makeAsyncWriter(
            initialPayloadCapacity: 168,
            serializer: { buffer, offset, value in
                NPRPC.marshal_stream_struct(buffer: buffer, offset: offset, rootSize: 40, value: value) { buf, off, elem in
                    marshal_AliasOptionalStreamPayload(buffer: buf, offset: off, data: elem)
                }
            },
            sink: { sequence, data in
                encodedChunks.append((sequence, data))
            }
        )

        for index in 0..<224 {
            let value = makeAliasOptionalPayload(seed: index, repeatCount: 4 + (index % 10))
            expectedValues.append(value)
            await writer.write(value)
        }

        XCTAssertEqual(encodedChunks.count, expectedValues.count)
        for (index, entry) in encodedChunks.enumerated() {
            XCTAssertEqual(entry.0, UInt64(index))
            let decoded = decode(entry.1) { unmarshal_AliasOptionalStreamPayload(buffer: $0, offset: 0) }
            assertEqual(decoded, expectedValues[index])
        }
    }

    func testRuntimeStreamChunkRoundTripStress() {
        for index in 0..<256 {
            let chunk = impl.StreamChunk(
                stream_id: UInt64(index + 1),
                sequence: UInt64(index * 3),
                data: (0..<(128 + index * 5)).map { UInt8(($0 + index) & 0xFF) },
                window_size: UInt32(1024 + index)
            )

            let buffer = FlatBuffer()
            buffer.prepare(200)
            NPRPC.marshal_stream_struct(buffer: buffer, offset: 0, rootSize: 28, value: chunk) { buf, off, elem in
                impl.marshal_StreamChunk(buffer: buf, offset: off, data: elem)
            }

            guard let raw = buffer.constData else {
                XCTFail("StreamChunk serialization produced no bytes")
                return
            }

            let decoded = impl.unmarshal_StreamChunk(buffer: raw, offset: 0)
            XCTAssertEqual(decoded.stream_id, chunk.stream_id)
            XCTAssertEqual(decoded.sequence, chunk.sequence)
            XCTAssertEqual(decoded.data, chunk.data)
            XCTAssertEqual(decoded.window_size, chunk.window_size)
        }
    }
}
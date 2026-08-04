// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

import XCTest
@testable import NPRPC

/// Unit tests for optimized FlatBuffer marshalling helpers (no RPC).
final class MarshallingHelpersTests: XCTestCase {
    func testStringRoundtrip() {
        let cases = ["", "hello", "hello-мир", String(repeating: "x", count: 100), String(repeating: "🚀", count: 50)]
        for s in cases {
            let buf = FlatBuffer()
            buf.prepare(16)
            buf.commit(8)
            marshal_string(buffer: buf, offset: 0, string: s)
            guard let p = buf.constData else {
                XCTFail("nil buffer data")
                return
            }
            let out = unmarshal_string(buffer: p, offset: 0)
            XCTAssertEqual(out, s, "string roundtrip failed")
        }
    }

    func testFundamentalVectorRoundtrip() {
        let small: [UInt8] = [1, 2, 3, 4, 5]
        let patterned: [UInt8] = (0..<10_000).map { UInt8(truncatingIfNeeded: $0) }
        let meg = [UInt8](repeating: 0xAB, count: 1 << 20)

        for v in [small, patterned, meg] {
            let buf = FlatBuffer()
            buf.prepare(16)
            buf.commit(8)
            marshal_fundamental_vector(buffer: buf, offset: 0, vector: v)
            guard let p = buf.constData else {
                XCTFail("nil buffer data")
                return
            }
            let out: [UInt8] = unmarshal_fundamental_vector(buffer: p, offset: 0)
            XCTAssertEqual(out, v)
        }
    }

    func testUInt32VectorRoundtrip() {
        let v: [UInt32] = [0, 1, 0xDEAD_BEEF, UInt32.max]
        let buf = FlatBuffer()
        buf.prepare(16)
        buf.commit(8)
        marshal_fundamental_vector(buffer: buf, offset: 0, vector: v)
        guard let p = buf.constData else {
            XCTFail("nil buffer data")
            return
        }
        let out: [UInt32] = unmarshal_fundamental_vector(buffer: p, offset: 0)
        XCTAssertEqual(out, v)
    }

    func testStringVectorRoundtrip() {
        let v = ["a", "", "hello", "мир"]
        let buf = FlatBuffer()
        buf.prepare(16)
        buf.commit(8)
        marshal_string_vector(buffer: buf, offset: 0, vector: v)
        guard let p = buf.constData else {
            XCTFail("nil buffer data")
            return
        }
        let out = unmarshal_string_vector(buffer: p, offset: 0)
        XCTAssertEqual(out, v)
    }
}

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
    // MARK: - A variant nested in a vector inside a message

    /// The value the cross-language fixtures encode. Keep in step with
    /// `sample_plate()` in test/src/test_variant.cpp.
    static func samplePlate() -> EffectPlate {
        EffectPlate(
            x: 1.5, y: 2, w: 300, h: 40,
            effects: [
                .blur(EffectBlur(radius: 12)),
                .glass(EffectGlass(refraction: 10, rim: 0.8, magnification: 1.35)),
                .texture(EffectTexture(strength: 0.5, name: "grain")),
                .texture(EffectTexture(strength: 0, name: "")),
            ]
        )
    }

    static func assertSame(_ a: EffectPlate, _ b: EffectPlate, file: StaticString = #filePath, line: UInt = #line) {
        XCTAssertEqual(a.x, b.x, file: file, line: line)
        XCTAssertEqual(a.y, b.y, file: file, line: line)
        XCTAssertEqual(a.w, b.w, file: file, line: line)
        XCTAssertEqual(a.h, b.h, file: file, line: line)
        XCTAssertEqual(a.effects.count, b.effects.count, file: file, line: line)
        for (ea, eb) in zip(a.effects, b.effects) {
            switch (ea, eb) {
            case let (.blur(x), .blur(y)):
                XCTAssertEqual(x.radius, y.radius, file: file, line: line)
            case let (.glass(x), .glass(y)):
                XCTAssertEqual(x.refraction, y.refraction, file: file, line: line)
                XCTAssertEqual(x.rim, y.rim, file: file, line: line)
                XCTAssertEqual(x.magnification, y.magnification, file: file, line: line)
            case let (.texture(x), .texture(y)):
                XCTAssertEqual(x.strength, y.strength, file: file, line: line)
                XCTAssertEqual(x.name, y.name, file: file, line: line)
            default:
                XCTFail("arm differs: \(ea) vs \(eb)", file: file, line: line)
            }
        }
    }

    static func encode(_ plate: EffectPlate) -> [UInt8] {
        let buf = FlatBuffer()
        buf.prepare(24)
        buf.commit(24)
        marshal_EffectPlate(buffer: buf, offset: 0, data: plate)
        return Array(UnsafeRawBufferPointer(start: buf.constData!, count: buf.size))
    }

    static func decode(_ bytes: [UInt8]) -> EffectPlate {
        bytes.withUnsafeBytes { unmarshal_EffectPlate(buffer: $0.baseAddress!, offset: 0) }
    }

    func testNestedVectorOfVariantsRoundtrip() {
        let plate = Self.samplePlate()
        Self.assertSame(Self.decode(Self.encode(plate)), plate)
    }

    func testNestedVectorOfVariantsEmpty() {
        let plate = EffectPlate(x: 0, y: 0, w: 10, h: 0, effects: [])
        let out = Self.decode(Self.encode(plate))
        XCTAssertEqual(out.w, 10)
        XCTAssertTrue(out.effects.isEmpty)
    }

    /// What the C++ generated `to_flat` writes for `sample_plate()` — see
    /// `NestedVectorOfVariants_DecodesSwiftEncoding` in test_variant.cpp for
    /// why each side decodes the other's bytes rather than comparing them.
    static let cppSamplePlateHex =
        "0000c03f0000004000009643000020420800000004000000000000001c000000" +
        "0100000018000000020000001c00000002000000280000000000404100002041" +
        "cdcc4c3fcdccac3f0000003f0800000005000000677261696e00000000000000" +
        "0000000000000000"

    func testNestedVectorOfVariantsDecodesCppEncoding() {
        let hex = Array(Self.cppSamplePlateHex.utf8)
        var bytes: [UInt8] = []
        for i in stride(from: 0, to: hex.count, by: 2) {
            bytes.append(UInt8(String(decoding: hex[i..<i + 2], as: UTF8.self), radix: 16)!)
        }
        Self.assertSame(Self.decode(bytes), Self.samplePlate())
    }
}

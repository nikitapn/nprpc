import Foundation
import XCTest
@testable import NPRPC

final class StreamDecodingTests: XCTestCase {
    func testBoundedDecoderChecksFlatEnumsAndBooleans() throws {
        let buffer = FlatBuffer()
        buffer.prepare(8)
        buffer.commit(8)
        marshal_FlatEnumPayload(buffer: buffer, offset: 0, data: FlatEnumPayload(color: .Green, flag: true))
        let value = try unmarshal_FlatEnumPayload(buffer: buffer.constData!, bufferSize: buffer.size, offset: 0)
        XCTAssertEqual(value.color, .Green)
        XCTAssertTrue(value.flag)
        buffer.data!.storeBytes(of: UInt32.max, as: UInt32.self)
        XCTAssertThrowsError(try unmarshal_FlatEnumPayload(buffer: buffer.constData!, bufferSize: buffer.size, offset: 0))
        buffer.data!.storeBytes(of: Color.Red.rawValue, as: UInt32.self)
        buffer.data!.storeBytes(of: UInt8(2), toByteOffset: 4, as: UInt8.self)
        XCTAssertThrowsError(try unmarshal_FlatEnumPayload(buffer: buffer.constData!, bufferSize: buffer.size, offset: 0))
    }

    func testOptionalEnumUsesItsRelativePointerAndValidatesMembership() throws {
        let colors: [Color?] = [nil, .Blue]
        for color in colors {
            let buffer = FlatBuffer()
            buffer.prepare(16)
            buffer.commit(16)
            marshal_OptionalEnumPayload(buffer: buffer, offset: 0, data: OptionalEnumPayload(color: color))
            let value = try unmarshal_OptionalEnumPayload(buffer: buffer.constData!, bufferSize: buffer.size, offset: 0)
            XCTAssertEqual(value.color, color)
            if color != nil {
                let offset = Int(buffer.data!.load(as: UInt32.self))
                buffer.data!.storeBytes(of: UInt32.max, toByteOffset: offset, as: UInt32.self)
                XCTAssertThrowsError(try unmarshal_OptionalEnumPayload(buffer: buffer.constData!, bufferSize: buffer.size, offset: 0))
            }
        }
    }

    func testMalformedChunkFinishesWithErrorAndCancelsOnlyOnce() async throws {
        var calls = 0
        var cancels = 0
        let reader = NPRPCStreamReader<UInt32>(streamId: 1, buffer: FlatBuffer()) { _, _ in
            calls += 1
            throw BufferError(message: "Incompatible test payload")
        }
        reader.sendCancelFn = { cancels += 1 }
        var byte: UInt8 = 0
        withUnsafePointer(to: &byte) {
            reader.onChunkReceived(data: $0, size: 1)
            reader.onChunkReceived(data: $0, size: 1)
        }
        var iterator = reader.makeAsyncIterator()
        do {
            _ = try await iterator.next()
            XCTFail("Malformed payload must fail the async stream")
        } catch let error as BufferError {
            XCTAssertEqual(error.message, "Incompatible test payload")
        }
        XCTAssertEqual(calls, 1)
        XCTAssertEqual(cancels, 1)
        reader.cancel()
        XCTAssertEqual(cancels, 1)
    }

    func testEmptyChunkFailsBeforeCallingDeserializer() async throws {
        let reader = NPRPCStreamReader<UInt8>(streamId: 1, buffer: FlatBuffer()) { _, _ in
            XCTFail("An empty chunk cannot be dereferenced")
            return 0
        }
        var byte: UInt8 = 0
        withUnsafePointer(to: &byte) { reader.onChunkReceived(data: $0, size: 0) }
        var iterator = reader.makeAsyncIterator()
        do {
            _ = try await iterator.next()
            XCTFail("Expected an empty-payload error")
        } catch is BufferError {}
    }

    func testValidationHelpersRejectOverflowAndMisalignment() {
        XCTAssertFalse(check_struct_bounds(bufferSize: Int.max, offset: Int.max, structSize: 1))
        XCTAssertFalse(check_struct_bounds(bufferSize: 8, offset: -1, structSize: 1))
        let buffer = FlatBuffer()
        buffer.prepare(32)
        buffer.commit(32)
        let data = buffer.data!
        data.initializeMemory(as: UInt8.self, repeating: 0, count: 32)
        data.storeBytes(of: UInt32(8), as: UInt32.self)
        data.storeBytes(of: UInt32.max, toByteOffset: 4, as: UInt32.self)
        XCTAssertFalse(check_vector_bounds(buffer: data, bufferSize: 32, offset: 0, elementSize: Int.max))
        XCTAssertFalse(check_vector_bounds(buffer: data, bufferSize: 32, offset: 0, elementSize: 0))
        XCTAssertFalse(check_vector_bounds(buffer: data, bufferSize: Int.max, offset: Int.max, elementSize: 8))
        XCTAssertFalse(check_vector_bounds(buffer: data, bufferSize: 32, offset: 1, elementSize: 8))
        XCTAssertFalse(check_optional_bounds(buffer: data, bufferSize: 32, offset: Int.max, valueSize: 8))
    }
}

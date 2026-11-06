import CNprpc
import XCTest
@testable import NPRPC

final class RpcErrorTests: XCTestCase {
    func testBridgeErrorKindsMatchSwiftKinds() {
        let mappings: [(NprpcBridgeErrorKind, RpcError.Kind)] = [
            (NPRPC_BRIDGE_INVALID_ARGUMENT, .invalidArgument),
            (NPRPC_BRIDGE_NO_ENDPOINT, .noEndpoint),
            (NPRPC_BRIDGE_NOT_INITIALIZED, .notInitialized),
            (NPRPC_BRIDGE_TIMEOUT, .timeout),
            (NPRPC_BRIDGE_CANCELLED, .cancelled),
            (NPRPC_BRIDGE_CONNECTION_FAILURE, .connectionFailure),
            (NPRPC_BRIDGE_OUT_OF_MEMORY, .outOfMemory),
            (NPRPC_BRIDGE_INTERNAL_ERROR, .internalError),
            (NPRPC_BRIDGE_UNSUPPORTED_TRANSPORT, .unsupportedTransport),
        ]
        for (bridgeKind, swiftKind) in mappings {
            var bridge = NprpcBridgeError()
            bridge.kind = bridgeKind
            XCTAssertEqual(RpcError(bridge: bridge).kind, swiftKind)
        }
    }

    func testBridgeErrorCopiesBorrowedStringsAndNativeDetails() {
        var message = Array("connection refused".utf8CString)
        var category = Array("system".utf8CString)
        let error = message.withUnsafeMutableBufferPointer { message in
            category.withUnsafeMutableBufferPointer { category in
                var bridge = NprpcBridgeError()
                bridge.kind = NPRPC_BRIDGE_CONNECTION_FAILURE
                bridge.native_code = 111
                bridge.message = UnsafePointer(message.baseAddress!)
                bridge.native_category = UnsafePointer(category.baseAddress!)
                let copied = RpcError(bridge: bridge)
                message[0] = 0
                category[0] = 0
                return copied
            }
        }
        XCTAssertEqual(error.message, "connection refused")
        XCTAssertEqual(error.nativeCategory, "system")
        XCTAssertEqual(error.nativeCode, 111)
    }

    func testImmediateRejectionResumesSwiftContinuation() async {
        do {
            try await withCheckedThrowingContinuation { (continuation: CheckedContinuation<Void, Error>) in
                let context = Unmanaged.passRetained(ErrorContinuation(continuation)).toOpaque()
                nprpc_object_send_async_result(nil, nil, context, { context, error, response in
                    let box = Unmanaged<ErrorContinuation>.fromOpaque(context!).takeRetainedValue()
                    if let error {
                        box.continuation.resume(throwing: RpcError(bridge: error.pointee))
                    } else {
                        box.continuation.resume()
                    }
                }, 1, true, nil)
            }
            XCTFail("Invalid arguments must fail")
        } catch let error as RpcError {
            XCTAssertEqual(error.kind, .invalidArgument)
            XCTAssertEqual(error.nativeCode, 0)
            XCTAssertNil(error.nativeCategory)
        } catch {
            XCTFail("Unexpected error: \(error)")
        }
    }
}

private final class ErrorContinuation {
    let continuation: CheckedContinuation<Void, Error>
    init(_ continuation: CheckedContinuation<Void, Error>) { self.continuation = continuation }
}

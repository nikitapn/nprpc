import XCTest
@testable import NPRPC

final class NPRPCTests: XCTestCase {
    
    func testBasicInterop() {
        XCTAssertEqual(testAdd(1, 2), 3)
        XCTAssertEqual(testAdd(-5, 10), 5)
        XCTAssertEqual(testAdd(0, 0), 0)
    }
    
    func testStringPassing() {
        let result = testGreet("World")
        XCTAssertTrue(result.contains("World"))
        XCTAssertTrue(result.contains("NPRPC"))
    }
    
    func testArrayReturn() {
        let arr = testArray()
        XCTAssertEqual(arr.count, 5)
        XCTAssertEqual(arr, [1, 2, 3, 4, 5])
    }
    
    func testEndPointParsing() {
        // TCP
        if let ep = EndPoint.parse("tcp://127.0.0.1:5000") {
            XCTAssertEqual(ep.type, .tcp)
            XCTAssertEqual(ep.hostname, "127.0.0.1")
            XCTAssertEqual(ep.port, 5000)
        } else {
            XCTFail("Failed to parse TCP endpoint")
        }
        
        // WebSocket
        if let ep = EndPoint.parse("ws://localhost:8080/nprpc") {
            XCTAssertEqual(ep.type, .webSocket)
            XCTAssertEqual(ep.hostname, "localhost")
            XCTAssertEqual(ep.port, 8080)
            XCTAssertEqual(ep.path, "/nprpc")
        } else {
            XCTFail("Failed to parse WebSocket endpoint")
        }
        
        // QUIC
        if let ep = EndPoint.parse("quic://server.example.com:443") {
            XCTAssertEqual(ep.type, .quic)
            XCTAssertEqual(ep.hostname, "server.example.com")
            XCTAssertEqual(ep.port, 443)
        } else {
            XCTFail("Failed to parse QUIC endpoint")
        }
    }
    
    func testEndPointToURL() {
        var ep = EndPoint()
        ep.type = .webSocket
        ep.hostname = "localhost"
        ep.port = 8080
        ep.path = "/nprpc"
        
        let url = ep.toURL()
        XCTAssertEqual(url, "ws://localhost:8080/nprpc")
    }
    
    func testRpcConfiguration() {
        var config = RpcConfiguration()
        config.nameserverIP = "192.168.1.100"
        config.nameserverPort = 15000
        config.listenWSPort = 8080
        
        XCTAssertEqual(config.nameserverIP, "192.168.1.100")
        XCTAssertEqual(config.nameserverPort, 15000)
        XCTAssertEqual(config.listenWSPort, 8080)
    }
    
    func testRpcHandleCreation() {
        let rpc = Rpc()
        XCTAssertFalse(rpc.isInitialized)
        
        var config = RpcConfiguration()
        config.nameserverIP = "127.0.0.1"
        
        do {
            try rpc.initialize(config)
            XCTAssertTrue(rpc.isInitialized)
        } catch {
            XCTFail("Failed to initialize RPC: \(error)")
        }
    }
}

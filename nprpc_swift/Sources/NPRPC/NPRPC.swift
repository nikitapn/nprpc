// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

// Main Swift module for NPRPC
// Re-exports C++ types with Swift-friendly wrappers

import CNprpc

// ============================================================================
// MARK: - Version Info
// ============================================================================

/// Get the NPRPC library version
public func version() -> String {
    return String(cString: nprpc_swift.get_version())
}

// ============================================================================
// MARK: - Basic Interop Tests
// ============================================================================

/// Test basic C++ interop - add two numbers
public func testAdd(_ a: Int32, _ b: Int32) -> Int32 {
    return nprpc_swift.add_numbers(a, b)
}

/// Test string passing to C++
public func testGreet(_ name: String) -> String {
    // Convert Swift String to std::string for C++ interop
    let cxxName = std.string(name)
    let result = nprpc_swift.greet(cxxName)
    return String(result)
}

/// Test array return from C++
public func testArray() -> [Int32] {
    let vec = nprpc_swift.get_test_array()
    var result: [Int32] = []
    for i in 0..<vec.size() {
        result.append(vec[i])
    }
    return result
}

// ============================================================================
// MARK: - EndPoint
// ============================================================================

/// Transport type for endpoints
public enum TransportType: UInt8 {
    case unknown = 0
    case tcp = 1
    case webSocket = 2
    case http = 3
    case quic = 4
    case udp = 5
    case sharedMemory = 6
    
    init(from cxxType: nprpc_swift.EndPointType) {
        self = TransportType(rawValue: cxxType.rawValue) ?? .unknown
    }
    
    var cxxType: nprpc_swift.EndPointType {
        return nprpc_swift.EndPointType(rawValue: self.rawValue) ?? .Unknown
    }
}

/// Network endpoint
public struct EndPoint {
    public var type: TransportType
    public var hostname: String
    public var port: UInt16
    public var path: String
    
    public init(type: TransportType = .unknown, hostname: String = "", port: UInt16 = 0, path: String = "") {
        self.type = type
        self.hostname = hostname
        self.port = port
        self.path = path
    }
    
    /// Parse endpoint from URL string
    public static func parse(_ url: String) -> EndPoint? {
        let cxxUrl = std.string(url)
        let maybeInfo = nprpc_swift.EndPointInfo.parse(cxxUrl)
        guard let info = maybeInfo.value else {
            return nil
        }
        return EndPoint(
            type: TransportType(from: info.type),
            hostname: String(info.hostname),
            port: info.port,
            path: String(info.path)
        )
    }
    
    /// Convert to URL string
    public func toURL() -> String {
        var info = nprpc_swift.EndPointInfo()
        info.type = type.cxxType
        info.hostname = std.string(hostname)
        info.port = port
        info.path = std.string(path)
        return String(info.to_url())
    }
}

// ============================================================================
// MARK: - RPC Configuration
// ============================================================================

/// Configuration for initializing the RPC system
public struct RpcConfiguration {
    public var nameserverIP: String = "127.0.0.1"
    public var nameserverPort: UInt16 = 15000
    public var listenTCPPort: UInt16 = 0
    public var listenWSPort: UInt16 = 0
    public var listenHTTPPort: UInt16 = 0
    public var listenQUICPort: UInt16 = 0
    public var listenUDPPort: UInt16 = 0
    public var httpRootDir: String = ""
    public var sslCertFile: String = ""
    public var sslKeyFile: String = ""
    public var quicCertFile: String = ""
    public var quicKeyFile: String = ""
    public var threadPoolSize: UInt16 = 4
    
    public init() {}
    
    /// Convert to C++ config struct
    func toCxxConfig() -> nprpc_swift.RpcConfig {
        var config = nprpc_swift.RpcConfig()
        config.nameserver_ip = std.string(nameserverIP)
        config.nameserver_port = nameserverPort
        config.listen_tcp_port = listenTCPPort
        config.listen_ws_port = listenWSPort
        config.listen_http_port = listenHTTPPort
        config.listen_quic_port = listenQUICPort
        config.listen_udp_port = listenUDPPort
        config.http_root_dir = std.string(httpRootDir)
        config.ssl_cert_file = std.string(sslCertFile)
        config.ssl_key_file = std.string(sslKeyFile)
        config.quic_cert_file = std.string(quicCertFile)
        config.quic_key_file = std.string(quicKeyFile)
        config.thread_pool_size = threadPoolSize
        return config
    }
}

// ============================================================================
// MARK: - Rpc (Main Entry Point)
// ============================================================================

/// Main RPC system handle
/// 
/// This is a Swift wrapper around the nprpc::Rpc singleton.
/// Use this to initialize and control the NPRPC runtime.
///
/// Example:
/// ```swift
/// let rpc = try Rpc()
/// var config = RpcConfiguration()
/// config.listenWSPort = 8080
/// try rpc.initialize(config)
/// 
/// // In a background task:
/// rpc.run()
/// ```
public final class Rpc {
    private var handle: nprpc_swift.RpcHandle
    
    /// Create a new RPC instance
    public init() {
        self.handle = nprpc_swift.RpcHandle()
    }
    
    /// Initialize the RPC system with the given configuration
    public func initialize(_ config: RpcConfiguration) throws {
        let cxxConfig = config.toCxxConfig()
        let success = handle.initialize(cxxConfig)
        if !success {
            throw RpcError.initializationFailed
        }
    }
    
    /// Run the RPC event loop (blocks until stopped)
    public func run() {
        handle.run()
    }
    
    /// Stop the RPC event loop
    public func stop() {
        handle.stop()
    }
    
    /// Check if the RPC system is initialized
    public var isInitialized: Bool {
        return handle.is_initialized()
    }
    
    /// Get debug information
    public var debugInfo: String {
        return String(handle.get_debug_info())
    }
}

/// Errors that can occur during RPC operations
public enum RpcError: Error {
    case initializationFailed
    case notInitialized
    case connectionFailed(String)
    case timeout
    case invalidEndpoint
}

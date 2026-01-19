// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

// Swift wrapper for NPRPC Rpc singleton and configuration

import CNprpc

/// Configuration for NPRPC runtime
public struct RpcConfiguration: Sendable {
    /// Nameserver address
    public var nameserverHost: String
    public var nameserverPort: UInt16
    
    /// Listen ports (0 = don't listen on this transport)
    public var listenTcpPort: UInt16
    public var listenWsPort: UInt16
    public var listenHttpPort: UInt16
    public var listenQuicPort: UInt16
    public var listenUdpPort: UInt16
    
    /// HTTP/WebSocket settings
    public var httpRootDir: String
    
    /// TLS/SSL settings
    public var sslCertFile: String
    public var sslKeyFile: String
    public var quicCertFile: String
    public var quicKeyFile: String
    
    /// Thread pool size for io_context
    public var threadPoolSize: UInt16
    
    /// Default configuration (nameserver only)
    public init(
        nameserverHost: String = "127.0.0.1",
        nameserverPort: UInt16 = 15000,
        listenTcpPort: UInt16 = 0,
        listenWsPort: UInt16 = 0,
        listenHttpPort: UInt16 = 0,
        listenQuicPort: UInt16 = 0,
        listenUdpPort: UInt16 = 0,
        httpRootDir: String = "",
        sslCertFile: String = "",
        sslKeyFile: String = "",
        quicCertFile: String = "",
        quicKeyFile: String = "",
        threadPoolSize: UInt16 = 4
    ) {
        self.nameserverHost = nameserverHost
        self.nameserverPort = nameserverPort
        self.listenTcpPort = listenTcpPort
        self.listenWsPort = listenWsPort
        self.listenHttpPort = listenHttpPort
        self.listenQuicPort = listenQuicPort
        self.listenUdpPort = listenUdpPort
        self.httpRootDir = httpRootDir
        self.sslCertFile = sslCertFile
        self.sslKeyFile = sslKeyFile
        self.quicCertFile = quicCertFile
        self.quicKeyFile = quicKeyFile
        self.threadPoolSize = threadPoolSize
    }
    
    /// Convert to C++ RpcBuildConfig (matches nprpc::impl::BuildConfig)
    func toCxxConfig() -> nprpc_swift.RpcBuildConfig {
        var config = nprpc_swift.RpcBuildConfig()
        // Note: RpcBuildConfig matches nprpc::impl::BuildConfig structure
        config.hostname = std.string(nameserverHost)
        config.tcp_port = listenTcpPort
        config.udp_port = listenUdpPort
        config.http_port = listenHttpPort
        config.quic_port = listenQuicPort
        config.http_root_dir = std.string(httpRootDir)
        config.http_cert_file = std.string(sslCertFile)
        config.http_key_file = std.string(sslKeyFile)
        config.quic_cert_file = std.string(quicCertFile)
        config.quic_key_file = std.string(quicKeyFile)
        return config
    }
}

/// NPRPC runtime singleton
/// 
/// This is the main entry point for NPRPC applications. It manages:
/// - Network transport listeners (TCP, WebSocket, HTTP, QUIC, UDP)
/// - Connection to nameserver
/// - io_context for async operations
/// - POA management
///
/// Usage:
/// ```swift
/// let config = RpcConfiguration(
///     nameserverHost: "127.0.0.1",
///     nameserverPort: 15000,
///     listenTcpPort: 8080
/// )
/// 
/// let rpc = try Rpc.initialize(config)
/// 
/// // Create POAs, activate servants...
/// 
/// // Run event loop (blocks until stopped)
/// try rpc.run()
/// ```
public final class Rpc {
    var handle: UnsafeMutablePointer<nprpc_swift.RpcHandle>?
    private var isRunning: Bool = false
    
    /// Private initializer - use `initialize()` instead
    private init() {
        self.handle = nil
    }
    
    /// Internal initializer for builder access
    internal init(handle: UnsafeMutablePointer<nprpc_swift.RpcHandle>?) {
        self.handle = handle
    }
    
    deinit {
        if let handle = handle {
            handle.deinitialize(count: 1)
            handle.deallocate()
        }
    }
    
    /// Initialize the NPRPC runtime with configuration
    /// - Parameter config: Runtime configuration
    /// - Returns: Initialized Rpc instance
    /// - Throws: RuntimeError if initialization fails
    public static func initialize(_ config: RpcConfiguration) throws -> Rpc {
        let rpc = Rpc()
        
        // Allocate handle
        rpc.handle = UnsafeMutablePointer<nprpc_swift.RpcHandle>.allocate(capacity: 1)
        rpc.handle!.initialize(to: nprpc_swift.RpcHandle())
        
        // Convert Swift config to C++
        var cxxConfig = config.toCxxConfig()
        
        // Initialize (pass pointer to config)
        guard rpc.handle!.pointee.initialize(&cxxConfig) else {
            rpc.handle!.deinitialize(count: 1)
            rpc.handle!.deallocate()
            rpc.handle = nil
            throw RuntimeError(message: "Failed to initialize NPRPC runtime")
        }
        
        return rpc
    }
    
    /// Run the io_context event loop (blocks until stopped)
    /// - Throws: RuntimeError if not initialized
    public func run() throws {
        guard let handle = handle else {
            throw RuntimeError(message: "Rpc not initialized")
        }
        
        guard !isRunning else {
            throw RuntimeError(message: "Rpc is already running")
        }
        
        isRunning = true
        handle.pointee.run()
        isRunning = false
    }
    
    /// Stop the io_context event loop
    public func stop() {
        guard let handle = handle else { return }
        handle.pointee.stop()
    }
    
    /// Check if runtime is initialized
    public var isInitialized: Bool {
        return handle?.pointee.is_initialized() ?? false
    }
    
    /// Get debug information
    public func debugInfo() -> String {
        guard let handle = handle else {
            return "Rpc { not initialized }"
        }
        let cxxStr = handle.pointee.get_debug_info()
        return String(cString: nprpc_swift.string_to_cstr(cxxStr))
    }
}

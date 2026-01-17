// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

// Swift fluent builder for Rpc - matches C++ API pattern

import CNprpc

/// Internal configuration state shared across all builders
class BuildConfig {
    var logLevel: LogLevel = .info
    var hostname: String = ""
    
    // TCP
    var tcpPort: UInt16 = 0
    
    // UDP
    var udpPort: UInt16 = 0
    
    // HTTP/WebSocket
    var httpPort: UInt16 = 0
    var httpSslEnabled: Bool = false
    var http3Enabled: Bool = false
    var ssrEnabled: Bool = false
    var httpCertFile: String = ""
    var httpKeyFile: String = ""
    var httpDhparamsFile: String = ""
    var httpRootDir: String = ""
    var ssrHandlerDir: String = ""
    
    // QUIC
    var quicPort: UInt16 = 0
    var quicCertFile: String = ""
    var quicKeyFile: String = ""
    
    // SSL client settings
    var sslClientSelfSignedCertPath: String = ""
    var httpSslClientDisableVerification: Bool = false
}

/// Log levels matching C++ nprpc::LogLevel
public enum LogLevel: Int32 {
    case trace = 0
    case debug = 1
    case info = 2
    case warning = 3
    case error = 4
    case critical = 5
    case off = 6
}

/// Base protocol for all RPC builders providing common configuration
public protocol RpcBuilderProtocol {
    /// Set log level
    @discardableResult
    func setLogLevel(_ level: LogLevel) -> Self
    
    /// Set hostname for advertised URLs
    @discardableResult
    func setHostname(_ hostname: String) -> Self
    
    /// Enable client to accept self-signed server certificates
    @discardableResult
    func enableSslClientSelfSignedCert(_ certPath: String) -> Self
    
    /// Disable SSL certificate verification for client connections
    @discardableResult
    func disableSslClientVerification() -> Self
    
    /// Add TCP transport on specified port
    func withTcp(_ port: UInt16) -> RpcBuilderTcp
    
    /// Add HTTP/WebSocket transport on specified port
    func withHttp(_ port: UInt16) -> RpcBuilderHttp
    
    /// Add UDP transport on specified port
    func withUdp(_ port: UInt16) -> RpcBuilderUdp
    
    /// Add QUIC transport on specified port
    func withQuic(_ port: UInt16) -> RpcBuilderQuic
    
    /// Build and initialize the Rpc instance
    func build() throws -> Rpc
}

/// Internal protocol for accessing configuration (not exposed publicly)
protocol RpcBuilderInternal: RpcBuilderProtocol {
    var config: BuildConfig { get }
}

/// Extension providing default implementations for protocol
extension RpcBuilderInternal {
    @discardableResult
    public func setLogLevel(_ level: LogLevel) -> Self {
        config.logLevel = level
        return self
    }
    
    @discardableResult
    public func setHostname(_ hostname: String) -> Self {
        config.hostname = hostname
        return self
    }
    
    @discardableResult
    public func enableSslClientSelfSignedCert(_ certPath: String) -> Self {
        config.sslClientSelfSignedCertPath = certPath
        return self
    }
    
    @discardableResult
    public func disableSslClientVerification() -> Self {
        config.httpSslClientDisableVerification = true
        return self
    }
    
    public func withTcp(_ port: UInt16) -> RpcBuilderTcp {
        config.tcpPort = port
        return RpcBuilderTcp(config: config)
    }
    
    public func withHttp(_ port: UInt16) -> RpcBuilderHttp {
        config.httpPort = port
        return RpcBuilderHttp(config: config)
    }
    
    public func withUdp(_ port: UInt16) -> RpcBuilderUdp {
        config.udpPort = port
        return RpcBuilderUdp(config: config)
    }
    
    public func withQuic(_ port: UInt16) -> RpcBuilderQuic {
        config.quicPort = port
        return RpcBuilderQuic(config: config)
    }
    
    public func build() throws -> Rpc {
        // Convert to C++ config and build
        // TODO: When we link against libnprpc, call actual C++ RpcBuilder
        // For now, use the POC RpcHandle approach
        var cxxConfig = nprpc_swift.RpcConfig()
        cxxConfig.nameserver_ip = std.string(config.hostname)
        cxxConfig.listen_tcp_port = config.tcpPort
        cxxConfig.listen_ws_port = config.httpPort
        cxxConfig.listen_http_port = config.httpPort
        cxxConfig.listen_quic_port = config.quicPort
        cxxConfig.listen_udp_port = config.udpPort
        cxxConfig.http_root_dir = std.string(config.httpRootDir)
        cxxConfig.ssl_cert_file = std.string(config.httpCertFile)
        cxxConfig.ssl_key_file = std.string(config.httpKeyFile)
        cxxConfig.quic_cert_file = std.string(config.quicCertFile)
        cxxConfig.quic_key_file = std.string(config.quicKeyFile)
        
        let handle = UnsafeMutablePointer<nprpc_swift.RpcHandle>.allocate(capacity: 1)
        handle.initialize(to: nprpc_swift.RpcHandle())
        
        guard handle.pointee.initialize(cxxConfig) else {
            handle.deinitialize(count: 1)
            handle.deallocate()
            throw RuntimeError(message: "Failed to initialize NPRPC runtime")
        }
        
        return Rpc(handle: handle)
    }
}

/// Root builder - entry point for fluent API
public final class RpcBuilder: RpcBuilderInternal {
    internal let config: BuildConfig
    
    public init() {
        self.config = BuildConfig()
    }
}

/// TCP-specific builder
public final class RpcBuilderTcp: RpcBuilderInternal {
    internal let config: BuildConfig
    
    init(config: BuildConfig) {
        self.config = config
    }
}

/// UDP-specific builder
public final class RpcBuilderUdp: RpcBuilderInternal {
    internal let config: BuildConfig
    
    init(config: BuildConfig) {
        self.config = config
    }
}

/// HTTP/WebSocket-specific builder with SSL and SSR options
public final class RpcBuilderHttp: RpcBuilderInternal {
    internal let config: BuildConfig
    
    init(config: BuildConfig) {
        self.config = config
    }
    
    /// Enable SSL/TLS for HTTP and WebSocket
    @discardableResult
    public func ssl(certFile: String, keyFile: String, dhparamsFile: String = "") -> RpcBuilderHttp {
        config.httpSslEnabled = true
        config.httpCertFile = certFile
        config.httpKeyFile = keyFile
        config.httpDhparamsFile = dhparamsFile
        return self
    }
    
    /// Enable HTTP/3 support
    @discardableResult
    public func enableHttp3() -> RpcBuilderHttp {
        config.http3Enabled = true
        return self
    }
    
    /// Enable server-side rendering
    @discardableResult
    public func enableSsr(handlerDir: String = "") -> RpcBuilderHttp {
        config.ssrEnabled = true
        if !handlerDir.isEmpty {
            config.ssrHandlerDir = handlerDir
        }
        return self
    }
    
    /// Set HTTP document root directory
    @discardableResult
    public func rootDir(_ path: String) -> RpcBuilderHttp {
        config.httpRootDir = path
        return self
    }
}

/// QUIC-specific builder with TLS configuration
public final class RpcBuilderQuic: RpcBuilderInternal {
    internal let config: BuildConfig
    
    init(config: BuildConfig) {
        self.config = config
    }
    
    /// Set QUIC port (alternative to withQuic)
    @discardableResult
    public func port(_ port: UInt16) -> RpcBuilderQuic {
        config.quicPort = port
        return self
    }
    
    /// Configure TLS certificates for QUIC
    @discardableResult
    public func ssl(certFile: String, keyFile: String) -> RpcBuilderQuic {
        config.quicCertFile = certFile
        config.quicKeyFile = keyFile
        return self
    }
}

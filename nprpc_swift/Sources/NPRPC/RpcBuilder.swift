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

/// Base protocol for all RPC builders providing common configuration
public protocol RpcBuilderProtocol {
    /// Set log level (uses LogLevel from Generated/nprpc.swift)
    @discardableResult
    func setLogLevel(_ level: LogLevel) -> Self
    
    /// Set hostname for advertised URLs
    @discardableResult
    func withHostname(_ hostname: String) -> Self

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
    public func withHostname(_ hostname: String) -> Self {
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
        // Build RpcBuildConfig (Swift-visible struct matching nprpc::impl::BuildConfig)
        var cxxConfig = nprpc_swift.RpcBuildConfig()
        
        // Log level - convert Swift enum to uint32_t matching nprpc::LogLevel order
        // nprpc::LogLevel: off=0, critical=1, error=2, warn=3, info=4, debug=5, trace=6
        cxxConfig.log_level = UInt32(config.logLevel.rawValue)
        
        // Hostname
        cxxConfig.hostname = std.string(config.hostname)
        
        // Transport ports
        cxxConfig.tcp_port = config.tcpPort
        cxxConfig.udp_port = config.udpPort
        cxxConfig.http_port = config.httpPort
        cxxConfig.quic_port = config.quicPort
        
        // HTTP/WebSocket settings
        cxxConfig.http_ssl_enabled = config.httpSslEnabled
        cxxConfig.http3_enabled = config.http3Enabled
        cxxConfig.ssr_enabled = config.ssrEnabled
        cxxConfig.http_ssl_client_disable_verification = config.httpSslClientDisableVerification
        cxxConfig.http_cert_file = std.string(config.httpCertFile)
        cxxConfig.http_key_file = std.string(config.httpKeyFile)
        cxxConfig.http_dhparams_file = std.string(config.httpDhparamsFile)
        cxxConfig.http_root_dir = std.string(config.httpRootDir)
        cxxConfig.ssr_handler_dir = std.string(config.ssrHandlerDir)
        
        // QUIC settings
        cxxConfig.quic_cert_file = std.string(config.quicCertFile)
        cxxConfig.quic_key_file = std.string(config.quicKeyFile)
        
        // SSL client settings
        cxxConfig.ssl_client_self_signed_cert_path = std.string(config.sslClientSelfSignedCertPath)
        
        // Create RpcHandle and initialize with config
        let handle = UnsafeMutablePointer<nprpc_swift.RpcHandle>.allocate(capacity: 1)
        handle.initialize(to: nprpc_swift.RpcHandle())
        
        guard handle.pointee.initialize(&cxxConfig) else {
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

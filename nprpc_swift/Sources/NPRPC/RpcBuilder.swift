// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

// Swift fluent builder for Rpc - matches C++ API pattern

import CNprpc
import CxxStdlib

/// Internal configuration state shared across all builders
class BuildConfig {
    var logLevel: LogLevel = .warn
    var hostname: String = ""

    // TCP
    var tcpPort: UInt16 = 0

    // HTTP/WebSocket
    var httpPort: UInt16 = 0
    var httpSslEnabled: Bool = false
    var http3Enabled: Bool = false
    var ssrEnabled: Bool = false
    var httpCertFile: String = ""
    var httpKeyFile: String = ""
    var httpDhparamsFile: String = ""
    var httpRootDir: String = ""
    var httpAllowedOrigins: [String] = []
    var httpMaxRequestBodySize: UInt = 10_000
    var httpWebSocketCompressionEnabled: Bool = true
    var httpWebSocketMaxMessageSize: UInt = 2 * 1024 * 1024
    var httpWebTransportMaxMessageSize: UInt = 2 * 1024 * 1024
    var httpWebSocketMaxActiveSessionsPerIp: UInt = 0
    var httpWebSocketUpgradesPerIpPerSecond: UInt = 0
    var httpWebSocketUpgradesBurst: UInt = 0
    var httpWebSocketRequestsPerSessionPerSecond: UInt = 0
    var httpWebSocketRequestsBurst: UInt = 0
    var http3WorkerCount: UInt = 4
    var http3MaxActiveConnectionsPerIp: UInt = 0
    var http3MaxNewConnectionsPerIpPerSecond: UInt = 0
    var http3MaxNewConnectionsBurst: UInt = 0
    var httpRpcMaxRequestsPerIpPerSecond: UInt = 0
    var httpRpcMaxRequestsBurst: UInt = 0
    var httpWebTransportConnectsPerIpPerSecond: UInt = 0
    var httpWebTransportConnectsBurst: UInt = 0
    var httpWebTransportRequestsPerSessionPerSecond: UInt = 0
    var httpWebTransportRequestsBurst: UInt = 0
    var httpWebTransportStreamOpensPerSessionPerSecond: UInt = 0
    var httpWebTransportStreamOpensBurst: UInt = 0
    var ssrHandlerDir: String = ""
    var watchFiles: Bool = false

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
        cxxConfig.http_allowed_origins = std.string(config.httpAllowedOrigins.joined(separator: "\n"))
        cxxConfig.http_max_request_body_size = numericCast(config.httpMaxRequestBodySize)
        cxxConfig.http_websocket_compression_enabled = config.httpWebSocketCompressionEnabled
        cxxConfig.http_websocket_max_message_size = numericCast(config.httpWebSocketMaxMessageSize)
        cxxConfig.http_webtransport_max_message_size = numericCast(config.httpWebTransportMaxMessageSize)
        cxxConfig.http_websocket_max_active_sessions_per_ip = numericCast(config.httpWebSocketMaxActiveSessionsPerIp)
        cxxConfig.http_websocket_upgrades_per_ip_per_second = numericCast(config.httpWebSocketUpgradesPerIpPerSecond)
        cxxConfig.http_websocket_upgrades_burst = numericCast(config.httpWebSocketUpgradesBurst)
        cxxConfig.http_websocket_requests_per_session_per_second = numericCast(config.httpWebSocketRequestsPerSessionPerSecond)
        cxxConfig.http_websocket_requests_burst = numericCast(config.httpWebSocketRequestsBurst)
        cxxConfig.http3_worker_count = numericCast(config.http3WorkerCount)
        cxxConfig.http3_max_active_connections_per_ip = numericCast(config.http3MaxActiveConnectionsPerIp)
        cxxConfig.http3_max_new_connections_per_ip_per_second = numericCast(config.http3MaxNewConnectionsPerIpPerSecond)
        cxxConfig.http3_max_new_connections_burst = numericCast(config.http3MaxNewConnectionsBurst)
        cxxConfig.http_rpc_max_requests_per_ip_per_second = numericCast(config.httpRpcMaxRequestsPerIpPerSecond)
        cxxConfig.http_rpc_max_requests_burst = numericCast(config.httpRpcMaxRequestsBurst)
        cxxConfig.http_webtransport_connects_per_ip_per_second = numericCast(config.httpWebTransportConnectsPerIpPerSecond)
        cxxConfig.http_webtransport_connects_burst = numericCast(config.httpWebTransportConnectsBurst)
        cxxConfig.http_webtransport_requests_per_session_per_second = numericCast(config.httpWebTransportRequestsPerSessionPerSecond)
        cxxConfig.http_webtransport_requests_burst = numericCast(config.httpWebTransportRequestsBurst)
        cxxConfig.http_webtransport_stream_opens_per_session_per_second = numericCast(config.httpWebTransportStreamOpensPerSessionPerSecond)
        cxxConfig.http_webtransport_stream_opens_burst = numericCast(config.httpWebTransportStreamOpensBurst)
        cxxConfig.ssr_handler_dir = std.string(config.ssrHandlerDir)
        cxxConfig.watch_files = config.watchFiles

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

    /// Allow browser cross-origin requests from the provided HTTPS origins.
    @discardableResult
    public func allowOrigins(_ origins: [String]) -> RpcBuilderHttp {
        config.httpAllowedOrigins = origins
        return self
    }

    /// Cap buffered HTTP request bodies before dispatch.
    @discardableResult
    public func maxRequestBodySize(_ bytes: UInt) -> RpcBuilderHttp {
        config.httpMaxRequestBodySize = bytes
        return self
    }

    /// Enable or disable WebSocket permessage-deflate negotiation.
    @discardableResult
    public func enableWebSocketCompression(_ enabled: Bool = true) -> RpcBuilderHttp {
        config.httpWebSocketCompressionEnabled = enabled
        return self
    }

    /// Cap inbound WebSocket message size.
    @discardableResult
    public func maxWebSocketMessageSize(_ bytes: UInt) -> RpcBuilderHttp {
        config.httpWebSocketMaxMessageSize = bytes
        return self
    }

    /// Cap inbound WebTransport control/native payload size.
    @discardableResult
    public func maxWebTransportMessageSize(_ bytes: UInt) -> RpcBuilderHttp {
        config.httpWebTransportMaxMessageSize = bytes
        return self
    }

    /// Set the number of dedicated HTTP/3 worker sockets/threads.
    /// Pass 0 to auto-size from hardware concurrency; default: 4.
    @discardableResult
    public func http3Workers(_ count: UInt) -> RpcBuilderHttp {
        config.http3WorkerCount = count
        return self
    }

    /// Cap concurrently active WebSocket sessions per client IP.
    @discardableResult
    public func maxWebSocketSessionsPerIp(_ count: UInt) -> RpcBuilderHttp {
        config.httpWebSocketMaxActiveSessionsPerIp = count
        return self
    }

    /// Limit WebSocket upgrade attempts per client IP.
    @discardableResult
    public func maxWebSocketUpgradesPerIpPerSecond(_ rate: UInt, burst: UInt = 0) -> RpcBuilderHttp {
        config.httpWebSocketUpgradesPerIpPerSecond = rate
        config.httpWebSocketUpgradesBurst = burst
        return self
    }

    /// Limit request messages received on a single WebSocket session.
    @discardableResult
    public func maxWebSocketRequestsPerSessionPerSecond(_ rate: UInt, burst: UInt = 0) -> RpcBuilderHttp {
        config.httpWebSocketRequestsPerSessionPerSecond = rate
        config.httpWebSocketRequestsBurst = burst
        return self
    }

    /// Cap concurrently active HTTP/3 connections per client IP.
    @discardableResult
    public func maxHttp3ConnectionsPerIp(_ count: UInt) -> RpcBuilderHttp {
        config.http3MaxActiveConnectionsPerIp = count
        return self
    }

    /// Limit new HTTP/3 connection attempts per client IP.
    @discardableResult
    public func maxHttp3NewConnectionsPerIpPerSecond(_ rate: UInt, burst: UInt = 0) -> RpcBuilderHttp {
        config.http3MaxNewConnectionsPerIpPerSecond = rate
        config.http3MaxNewConnectionsBurst = burst
        return self
    }

    /// Limit HTTP RPC requests per client IP across HTTP/1.1 and HTTP/3.
    @discardableResult
    public func maxHttpRpcRequestsPerIpPerSecond(_ rate: UInt, burst: UInt = 0) -> RpcBuilderHttp {
        config.httpRpcMaxRequestsPerIpPerSecond = rate
        config.httpRpcMaxRequestsBurst = burst
        return self
    }

    /// Limit WebTransport CONNECT requests per client IP.
    @discardableResult
    public func maxWebTransportConnectsPerIpPerSecond(_ rate: UInt, burst: UInt = 0) -> RpcBuilderHttp {
        config.httpWebTransportConnectsPerIpPerSecond = rate
        config.httpWebTransportConnectsBurst = burst
        return self
    }

    /// Limit WebTransport framed requests per session.
    @discardableResult
    public func maxWebTransportRequestsPerSessionPerSecond(_ rate: UInt, burst: UInt = 0) -> RpcBuilderHttp {
        config.httpWebTransportRequestsPerSessionPerSecond = rate
        config.httpWebTransportRequestsBurst = burst
        return self
    }

    /// Limit WebTransport child stream opens per session.
    @discardableResult
    public func maxWebTransportStreamOpensPerSessionPerSecond(_ rate: UInt, burst: UInt = 0) -> RpcBuilderHttp {
        config.httpWebTransportStreamOpensPerSessionPerSecond = rate
        config.httpWebTransportStreamOpensBurst = burst
        return self
    }

    /// Enable inotify-based cache invalidation on the HTTP root directory.
    /// Any file written or renamed under rootDir() is immediately evicted
    /// from the file cache so the next request reloads it from disk.
    /// Useful during development — no effect on non-Linux platforms.
    @discardableResult
    public func watchFiles() -> RpcBuilderHttp {
        config.watchFiles = true
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

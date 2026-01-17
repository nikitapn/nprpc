// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

// Example demonstrating fluent builder API matching C++ pattern

import NPRPC

/*
 * C++ equivalent:
 * 
 * auto rpc = nprpc::RpcBuilder()
 *     .set_log_level(nprpc::LogLevel::debug)
 *     .set_hostname("example.com")
 *     .with_tcp(8080)
 *     .with_http(8081)
 *         .ssl("cert.pem", "key.pem")
 *         .root_dir("/var/www")
 *         .enable_http3()
 *     .with_quic(8443)
 *         .ssl("quic_cert.pem", "quic_key.pem")
 *     .with_udp(8082)
 *     .build(io_context);
 */

func exampleFluentBuilder() throws {
    // Swift version with identical API structure
    let rpc = try RpcBuilder()
        .setLogLevel(.debug)
        .setHostname("example.com")
        .withTcp(8080)
        .withHttp(8081)
            .ssl(certFile: "cert.pem", keyFile: "key.pem")
            .rootDir("/var/www")
            .enableHttp3()
        .withQuic(8443)
            .ssl(certFile: "quic_cert.pem", keyFile: "quic_key.pem")
        .withUdp(8082)
        .build()
    
    // Use the RPC instance
    print("Rpc initialized: \(rpc.isInitialized)")
}

// Simpler examples:

func exampleTcpOnly() throws {
    let rpc = try RpcBuilder()
        .withTcp(8080)
        .build()
}

func exampleHttpWithSsl() throws {
    let rpc = try RpcBuilder()
        .setHostname("api.example.com")
        .withHttp(443)
            .ssl(certFile: "/etc/ssl/cert.pem", 
                 keyFile: "/etc/ssl/key.pem")
            .rootDir("/var/www/html")
        .build()
}

func exampleQuicOnly() throws {
    let rpc = try RpcBuilder()
        .withQuic(4433)
            .ssl(certFile: "certs/cert.pem", 
                 keyFile: "certs/key.pem")
        .build()
}

func exampleMultiTransport() throws {
    // Listen on multiple transports simultaneously
    let rpc = try RpcBuilder()
        .setLogLevel(.info)
        .setHostname("myserver.local")
        .withTcp(8080)        // TCP on 8080
        .withHttp(8081)       // HTTP/WS on 8081
            .rootDir("/static")
        .withQuic(8443)       // QUIC on 8443
            .ssl(certFile: "cert.pem", keyFile: "key.pem")
        .withUdp(8090)        // UDP on 8090
        .build()
    
    // Now server listens on all configured transports
}

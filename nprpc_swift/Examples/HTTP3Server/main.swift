// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

// Swift HTTP3 server example - matches C++ version from examples/ssr-svelte-app/server

import NPRPC
import Foundation

print("╔══════════════════════════════════════════════════════════╗")
print("║         NPRPC Swift HTTP3 Server Example                ║")
print("╚══════════════════════════════════════════════════════════╝")
print()

do {
    // Build Rpc configuration using fluent API (matches C++ RpcBuilder pattern)
    let rpc = try RpcBuilder()
        .setLogLevel(.trace)
        .withHostname("localhost")
        .withHttp(3000)
            .ssl(
                certFile: "/workspace/certs/out/localhost.crt",
                keyFile: "/workspace/certs/out/localhost.key"
            )
            .enableHttp3()
            .rootDir("/tmp/nprpc_http3_test")
        .build()

    print("✓ NPRPC configured successfully")
    print("  - Protocol: HTTP/3 (QUIC)")
    print("  - Address: https://localhost:3000")
    print("  - TLS: Enabled")
    print("  - Root: /tmp/nprpc_http3_test")
    print()

    // Create test HTML file in root directory
    let testHtml = """
    <!DOCTYPE html>
    <html>
    <head>
        <meta charset="UTF-8">
        <title>NPRPC Swift HTTP3</title>
        <style>
            body {
                font-family: system-ui, -apple-system, sans-serif;
                max-width: 800px;
                margin: 50px auto;
                padding: 20px;
                background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
                color: white;
            }
            .card {
                background: rgba(255, 255, 255, 0.1);
                border-radius: 10px;
                padding: 30px;
                backdrop-filter: blur(10px);
            }
            h1 { margin: 0 0 20px 0; }
            .badge {
                display: inline-block;
                background: rgba(255, 255, 255, 0.2);
                padding: 5px 15px;
                border-radius: 20px;
                font-size: 14px;
                margin-right: 10px;
            }
        </style>
    </head>
    <body>
        <div class="card">
            <h1>🚀 NPRPC Swift HTTP3 Server</h1>
            <p>
                <span class="badge">Swift 6.2.3</span>
                <span class="badge">HTTP/3</span>
                <span class="badge">C++ Interop</span>
            </p>
            <p>
                This page is served by an NPRPC HTTP/3 server written in Swift,
                powered by the NPRPC C++ RPC framework through Swift's C++ interop.
            </p>
            <p>
                <strong>Features:</strong>
            </p>
            <ul>
                <li>HTTP/3 (QUIC) protocol support</li>
                <li>TLS 1.3 encryption</li>
                <li>Zero-copy C++ interop</li>
                <li>Fluent builder API</li>
                <li>Production-ready RPC framework</li>
            </ul>
        </div>
    </body>
    </html>
    """

    // Ensure root directory exists
    let rootDir = "/tmp/nprpc_http3_test"
    try? FileManager.default.createDirectory(
        atPath: rootDir,
        withIntermediateDirectories: true
    )
    try testHtml.write(
        toFile: "\(rootDir)/index.html",
        atomically: true,
        encoding: .utf8
    )

    print("✓ Created test content at \(rootDir)/index.html")
    print()
    print("Server is running in background (4 worker threads)...")
    print("Press Ctrl+C to stop")
    print()

    // Set up signal handling for graceful shutdown
    let signalSource = DispatchSource.makeSignalSource(signal: SIGINT, queue: .main)
    signalSource.setEventHandler {
        print("\n")
        print("Received SIGINT, shutting down...")
        rpc.stop()
        exit(0)
    }
    signal(SIGINT, SIG_IGN)
    signalSource.resume()

    // Start 4 worker threads for handling requests
    try rpc.startThreadPool(4) 
    // Keep the process alive while server runs in background thread pool
    dispatchMain()

} catch {
    print("❌ Error: \(error)")
    exit(1)
}

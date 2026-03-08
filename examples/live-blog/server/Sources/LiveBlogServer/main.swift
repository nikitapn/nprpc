import Foundation
import NPRPC

struct BlogPost: Sendable {
    let id: UInt64
    let slug: String
    let title: String
    let excerpt: String
    let publishedAt: String
}

let mockPosts: [BlogPost] = (1...18).map { index in
    BlogPost(
        id: UInt64(index),
        slug: "post-\(index)",
        title: "Post \(index): Building a hybrid NPRPC app",
        excerpt: "Skeleton data for the future BlogService.listPosts(page, pageSize) implementation.",
        publishedAt: "2026-03-\(String(format: "%02d", index))"
    )
}

print("╔══════════════════════════════════════════════════════════╗")
print("║                  Live Blog Swift Server                  ║")
print("╚══════════════════════════════════════════════════════════╝")
print()
print("This is the scaffold for the examples/live-blog Swift backend.")
print("The intended architecture is:")
print("  - SvelteKit renders route-aware shells")
print("  - Swift owns all business logic and data")
print("  - browser hydration performs real NPRPC calls")
print()

do {
    let certFile = "/project/certs/out/localhost.crt"
    let keyFile = "/project/certs/out/localhost.key"
    let httpPort: UInt16 = 8443

    let runtimeRoot = FileManager.default.currentDirectoryPath + "/.runtime-www"
    try FileManager.default.createDirectory(atPath: runtimeRoot, withIntermediateDirectories: true)

    let indexHtml = """
    <!doctype html>
    <html>
      <head>
        <meta charset="utf-8" />
        <meta name="viewport" content="width=device-width, initial-scale=1" />
        <title>Live Blog Swift Backend</title>
        <style>
          body { font-family: system-ui, sans-serif; margin: 3rem auto; max-width: 56rem; padding: 0 1.5rem; color: #1c1917; }
          code { background: #f5f5f4; padding: 0.15rem 0.35rem; border-radius: 0.35rem; }
          .card { border: 1px solid #e7e5e4; border-radius: 1rem; padding: 1.5rem; margin-top: 1rem; }
        </style>
      </head>
      <body>
        <h1>Live Blog Swift Backend Scaffold</h1>
        <p>The Swift backend is running. The Svelte client is expected to be developed and served separately.</p>
        <div class="card">
          <p><strong>Next implementation step:</strong> replace this placeholder with real BlogService and ChatService NPRPC objects.</p>
          <p>Mock posts available in memory: \(mockPosts.count)</p>
          <p>Planned blog root route: <code>/blog?page=1</code></p>
        </div>
      </body>
    </html>
    """

    try indexHtml.write(toFile: runtimeRoot + "/index.html", atomically: true, encoding: .utf8)

    let rpc = try RpcBuilder()
        .setLogLevel(.trace)
        .withHostname("localhost")
        .withHttp(httpPort)
            .ssl(certFile: certFile, keyFile: keyFile)
            .enableHttp3()
            .rootDir(runtimeRoot)
        .build()

    print("HTTP root: \(runtimeRoot)")
    print("HTTPS/WebTransport port: \(httpPort)")
    print("Mock post count: \(mockPosts.count)")
    print()
    print("TODO:")
    print("  - generate and implement BlogService")
    print("  - generate and implement ChatService")
    print("  - emit host/bootstrap data for browser hydration")
    print()

    let signalSource = DispatchSource.makeSignalSource(signal: SIGINT, queue: .main)
    signalSource.setEventHandler {
        print("\nReceived SIGINT, shutting down...")
        rpc.stop()
        exit(0)
    }
    signal(SIGINT, SIG_IGN)
    signalSource.resume()

    try rpc.startThreadPool(4)
    dispatchMain()
} catch {
    var standardError = FileHandle.standardError
    let msg = "Fatal: \(error)\n"
    standardError.write(Data(msg.utf8))
    exit(1)
}
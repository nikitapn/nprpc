// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

// The NPRPC documentation site. Pages come from the page handler; CSS and
// htmx from the static root. Configuration is by environment:
//
//   DOCS_API        api.json from `just docs-api`
//                   (default ../../.build_relwith_debinfo/docs/api.json)
//   DOCS_ROOT       directory holding templates/ and web/ (default: .)
//   DOCS_PORT       HTTP port (default 8080)
//   DOCS_TLS_CERT   with DOCS_TLS_KEY, serve HTTPS instead
//   DOCS_TEMPLATE_RELOAD=1  re-read templates on every request

import Dispatch
import DocsWeb
import Foundation
import NPRPC

let env = ProcessInfo.processInfo.environment
let root = env["DOCS_ROOT"] ?? FileManager.default.currentDirectoryPath
let apiPath = env["DOCS_API"] ?? root + "/../../.build_relwith_debinfo/docs/api.json"
let port = UInt16(env["DOCS_PORT"] ?? "") ?? 8080

do {
    let store = try DocsStore(path: apiPath)
    let site = try DocsSite(store: store,
                            templateDirectory: root + "/templates",
                            hotReload: env["DOCS_TEMPLATE_RELOAD"] == "1")

    var http = RpcBuilder()
        .setLogLevel(.warn)
        .withHostname("localhost")
        .withHttp(port)
    if let cert = env["DOCS_TLS_CERT"], let key = env["DOCS_TLS_KEY"] {
        http = http.ssl(certFile: cert, keyFile: key)
    }
    let rpc = try http
        .withPageHandler { site.handle($0) }
        .rootDir(root + "/web")
        .build()

    let scheme = env["DOCS_TLS_CERT"] == nil ? "http" : "https"
    print("NPRPC docs: \(scheme)://localhost:\(port)/")
    print("  api.json:  \(apiPath) (reloaded when it changes)")
    print("  templates: \(site.templateNames.joined(separator: ", "))")

    let signalSource = DispatchSource.makeSignalSource(signal: SIGINT, queue: .main)
    signalSource.setEventHandler {
        rpc.stop()
        exit(0)
    }
    signal(SIGINT, SIG_IGN)
    signalSource.resume()

    try rpc.startThreadPool(2)
    dispatchMain()
} catch {
    FileHandle.standardError.write(Data("docs-server: \(error)\n".utf8))
    exit(1)
}

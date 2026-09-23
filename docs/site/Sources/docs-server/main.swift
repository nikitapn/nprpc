// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

// The NPRPC documentation site. Pages come from the page handler; CSS and
// htmx from the static root. Configuration is by environment:
//
//   DOCS_API        api.json from `just docs-api`
//                   (default ../../.build_relwith_debinfo/docs/api.json)
//   DOCS_ROOT       directory holding templates/ and web/ (default: .)
//   DOCS_PORT       HTTP port (default 8080)
//   DOCS_HOSTNAME   public hostname (default localhost)
//   DOCS_TLS_CERT   with DOCS_TLS_KEY, serve HTTPS instead; SIGHUP re-reads
//                   them, so a certbot renewal needs no restart
//   DOCS_HTTP3=1    also serve HTTP/3 on the same port (needs TLS)
//   DOCS_SHM_CHANNEL  take HTTP/3 through npquicrouter's shared-memory rings
//                   /nprpc_<name>_c2s and _s2c instead of loopback UDP
//   DOCS_TEMPLATE_RELOAD=1  re-read templates on every request
//
// SIGINT and SIGTERM stop the server.

import Dispatch
import DocsWeb
import Foundation
import NPRPC

// Unbuffered, so `docker logs` shows each line as it happens (print buffers
// when stdout is not a terminal).
func log(_ line: String) {
    FileHandle.standardOutput.write(Data((line + "\n").utf8))
}

let env = ProcessInfo.processInfo.environment
let root = env["DOCS_ROOT"] ?? FileManager.default.currentDirectoryPath
let apiPath = env["DOCS_API"] ?? root + "/../../.build_relwith_debinfo/docs/api.json"
let port = UInt16(env["DOCS_PORT"] ?? "") ?? 8080
let hostname = env["DOCS_HOSTNAME"] ?? "localhost"

do {
    let store = try DocsStore(path: apiPath)
    let site = try DocsSite(store: store,
                            templateDirectory: root + "/templates",
                            hotReload: env["DOCS_TEMPLATE_RELOAD"] == "1")

    var http = RpcBuilder()
        .setLogLevel(.warn)
        .withHostname(hostname)
        .withHttp(port)
    let tls = env["DOCS_TLS_CERT"] != nil && env["DOCS_TLS_KEY"] != nil
    if tls {
        http = http.ssl(certFile: env["DOCS_TLS_CERT"]!, keyFile: env["DOCS_TLS_KEY"]!)
        if env["DOCS_HTTP3"] == "1" {
            http = http.enableHttp3().http3Workers(1)
            // One name for both rings: the router's route uses it as this
            // site's ingress and egress channel alike.
            if let channel = env["DOCS_SHM_CHANNEL"], !channel.isEmpty {
                http = http.http3ShmChannels(egress: channel, ingress: channel)
            }
        }
    }
    let rpc = try http
        .withPageHandler { site.handle($0) }
        .rootDir(root + "/web")
        .build()

    let scheme = tls ? "https" : "http"
    log("NPRPC docs: \(scheme)://\(hostname):\(port)/")
    log("  api.json:  \(apiPath) (reloaded when it changes)")
    log("  templates: \(site.templateNames.joined(separator: ", "))")

    // Signal sources only fire once the default action is disabled.
    var signalSources: [DispatchSourceSignal] = []
    for sig in [SIGINT, SIGTERM] {
        signal(sig, SIG_IGN)
        let source = DispatchSource.makeSignalSource(signal: sig, queue: .main)
        source.setEventHandler {
            rpc.stop()
            exit(0)
        }
        source.resume()
        signalSources.append(source)
    }
    if tls {
        signal(SIGHUP, SIG_IGN)
        let source = DispatchSource.makeSignalSource(signal: SIGHUP, queue: .main)
        source.setEventHandler {
            log(rpc.reloadCertificates()
                ? "SIGHUP: TLS certificates reloaded"
                : "SIGHUP: certificate reload failed; still serving the previous certificate")
        }
        source.resume()
        signalSources.append(source)
    }

    try rpc.startThreadPool(2)
    dispatchMain()
} catch {
    FileHandle.standardError.write(Data("docs-server: \(error)\n".utf8))
    exit(1)
}

import Foundation
import NPRPC
let args = CommandLine.arguments
guard [3, 4].contains(args.count) else {
    print("Usage: MonitorDemo certificate.pem key.pem [trusted-clients.pem]")
    exit(1)
}
let builder = RpcBuilder().withHostname("localhost")
    .withHttp(8443).rootDir(Bundle.module.url(forResource: "Public", withExtension: nil)!.path).ssl(certFile: args[1], keyFile: args[2]).enableHttp3().http3Workers(1)
    .withQuic(8444).ssl(certFile: args[1], keyFile: args[2])
if args.count == 4 { try builder.requireClientCertificates(caFile: args[3]) } else { builder.allowAnonymousTelemetry() }
let rpc = try builder.enableTelemetry().build()
FileHandle.standardOutput.write(Data("Monitor reference: \(rpc.telemetryReference!)\n".utf8))
try rpc.run()

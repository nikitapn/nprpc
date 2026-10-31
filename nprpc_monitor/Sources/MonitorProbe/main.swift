import Foundation
import NPRPC
import MonitorAPI
@main struct Probe {
    static func main() async throws {
        guard [2, 3, 5].contains(CommandLine.arguments.count) else {
            print("Usage: MonitorProbe <object-reference> [server-ca.pem [client.pem client-key.pem]]"); exit(1)
        }
        let rpc = try Rpc.initialize(RpcConfiguration())
        let args = CommandLine.arguments
        if args.count >= 3 {
            try rpc.configureQuicClientTls(caFile: args[2],
                certFile: args.count == 5 ? args[3] : "", keyFile: args.count == 5 ? args[4] : "")
        }
        try rpc.startThreadPool(2)
        defer { rpc.stop() }
        guard let object = NPRPCObject.fromString(CommandLine.arguments[1]),
              let monitor = narrow(object, to: Monitor.self) else {
            fatalError("Invalid Monitor reference")
        }
        let stream = try monitor.watch(interval_ms: 250)
        defer { stream.cancel() }
        var count = 0
        for try await snapshot in stream {
            print(String(decoding: try JSONEncoder().encode(snapshot), as: UTF8.self))
            count += 1
            if count == 3 { break }
        }
        guard count == 3 else { fatalError("Stream ended early") }
    }
}

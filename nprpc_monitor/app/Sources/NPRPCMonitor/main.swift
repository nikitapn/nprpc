import Foundation
import Observation
import LavaUI
import LavaHost
import NPRPC
import MonitorAPI

struct Row: Identifiable {
    var id: UInt32
    var rate: Double = 0
    var bytes: Double = 0
    var dispatch: Double = 0
    var total: UInt64 = 0
    var errors: UInt64 = 0
    var history: [Double] = []
    var name: String {
        switch Transport(rawValue: UInt32(id)) {
        case .Http3: "HTTP/3"
        case .WebSocket: "WebSocket"
        case .WebTransport: "WebTransport"
        case nil: "Unknown"
        }
    }
}

@Observable final class Session: @unchecked Sendable {
    var reference = CommandLine.arguments.dropFirst().first ?? ""
    var status = "Disconnected — enter a Monitor object reference"
    var rows = (0..<3).map { Row(id: UInt32($0)) }
    private var previous: Snapshot?
    private var generation = 0
    private var task: Task<Void, Never>?
    private var reader: NPRPCStreamReader<Snapshot>?

    func disconnect() {
        generation += 1
        task?.cancel()
        reader?.cancel()
        reader = nil
        previous = nil
        status = "Disconnected"
    }
    func connect() {
        disconnect()
        let token = generation
        let address = reference
        status = "Connecting…"
        rows = (0..<3).map { Row(id: UInt32($0)) }
        task = Task.detached { [weak self] in
            do {
                guard let object = NPRPCObject.fromString(address),
                      let monitor = narrow(object, to: Monitor.self) else {
                    throw NSError(domain: "Invalid Monitor object reference", code: 1)
                }
                let stream = try monitor.watch(interval_ms: 1000)
                MainQueue.async { [weak self] in
                    guard let self, self.generation == token else { stream.cancel(); return }
                    self.reader = stream
                }
                for try await snapshot in stream {
                    if Task.isCancelled { stream.cancel(); break }
                    MainQueue.async { [weak self] in
                        guard let self, self.generation == token else { return }
                        self.receive(snapshot)
                    }
                }
                MainQueue.async { [weak self] in
                    guard let self, self.generation == token else { return }
                    self.status = "Stream ended — reconnect to resume"
                }
            } catch {
                let message = String(describing: error)
                MainQueue.async { [weak self] in
                    guard let self, self.generation == token else { return }
                    self.status = "Connection failed: \(message)"
                }
            }
        }
    }
    private func receive(_ value: Snapshot) {
        defer { previous = value }
        status = "Live • server telemetry uptime \(value.uptime_ms / 1000)s"
        for sample in value.transports where rows.indices.contains(Int(sample.transport.rawValue)) {
            let index = Int(sample.transport.rawValue)
            rows[index].total = sample.completed
            rows[index].errors = sample.errors
            guard let previous, value.uptime_ms > previous.uptime_ms,
                  let old = previous.transports.first(where: { $0.transport == sample.transport }),
                  sample.completed >= old.completed, sample.sent_bytes >= old.sent_bytes,
                  sample.received_bytes >= old.received_bytes, sample.dispatch_ns >= old.dispatch_ns else { continue }
            let seconds = Double(value.uptime_ms - previous.uptime_ms) / 1000
            let calls = sample.completed - old.completed
            rows[index].rate = Double(calls) / seconds
            rows[index].bytes = (Double(sample.received_bytes - old.received_bytes) + Double(sample.sent_bytes - old.sent_bytes)) / seconds
            rows[index].dispatch = calls == 0 ? 0 : Double(sample.dispatch_ns - old.dispatch_ns) / Double(calls) / 1_000_000
            rows[index].history.append(rows[index].rate)
            if rows[index].history.count > 120 { rows[index].history.removeFirst() }
        }
    }
}

struct Dashboard: View {
    @Bindable var session: Session
    var body: some View {
        VStack(padding: 24, spacing: 12) {
            HStack(spacing: 16) {
                Text("NPRPC / Live transport monitor", color: .accent)
                Spacer()
                Button("Connect") { session.connect() }
                Button("Disconnect") { session.disconnect() }
            }
            TextField(text: $session.reference, placeholder: "Monitor object reference")
            Text(session.status)
            ForEach(session.rows) { row in
                VStack(padding: 12, spacing: 8) {
                    HStack(spacing: 24) {
                        Text(row.name, color: .accent)
                        Text(String(format: "%.1f ops/s", row.rate))
                        Text(String(format: "%.1f KiB/s", row.bytes / 1024))
                        Text(String(format: "%.3f ms mean handler", row.dispatch))
                        Spacer()
                        Text("\(row.total) ops / \(row.errors) errors")
                    }
                    Canvas(label: row.name + " operations per second", height: .pt(70), flexGrow: 1) { draw, frame in
                        draw.line(x1: frame.x, y1: frame.y + frame.h, x2: frame.x + frame.w, y2: frame.y + frame.h, color: .accent)
                        let top = max(1, row.history.max() ?? 1)
                        let points = row.history.enumerated().map { index, value in
                            (x: frame.x + Float(index) / 119 * frame.w,
                             y: frame.y + frame.h - Float(value / top) * (frame.h - 4))
                        }
                        draw.polyline(points, color: .accent)
                    }
                }
            }
            Text("120 samples • charts scale independently • payload bytes, not wire bytes")
            Text("HTTP/3: response preparation. WS / WT: synchronous RPC dispatch. Streaming data frames excluded.")
            Spacer()
        }
    }
}

guard let editor = LavaHost.open(title: "NPRPC Monitor", width: 1120, height: 720) else { exit(1) }
// Client mode borrows the host's runtime. Only a windowed monitor owns one.
let ownedRuntime: Rpc?
if LavaHost.isClient {
    guard LavaHost.rpcRuntime != nil else { fatalError("LavaHost did not start its RPC runtime") }
    ownedRuntime = nil
} else {
    let rpc = try Rpc.initialize(RpcConfiguration())
    try rpc.startThreadPool(2)
    ownedRuntime = rpc
}
let tlsEnvironment = ProcessInfo.processInfo.environment
let tlsRuntime = ownedRuntime ?? LavaHost.rpcRuntime!
if ["NPRPC_QUIC_CA_FILE", "NPRPC_QUIC_CLIENT_CERT_FILE", "NPRPC_QUIC_CLIENT_KEY_FILE"].contains(where: { tlsEnvironment[$0] != nil }) {
    try tlsRuntime.configureQuicClientTls(
        caFile: tlsEnvironment["NPRPC_QUIC_CA_FILE"] ?? "",
        certFile: tlsEnvironment["NPRPC_QUIC_CLIENT_CERT_FILE"] ?? "",
        keyFile: tlsEnvironment["NPRPC_QUIC_CLIENT_KEY_FILE"] ?? "")
}
let session = Session()
LavaHost.run(editor: editor) { Dashboard(session: session) }
session.disconnect()
ownedRuntime?.stop()

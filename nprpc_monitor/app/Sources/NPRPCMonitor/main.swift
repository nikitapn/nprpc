import Foundation
import Observation
import LavaUI
import LavaHost
import NPRPC
import MonitorAPI

private let historyLength = 120

struct Row: Identifiable {
    var id: UInt32
    /// False until two samples give the first rates; "—" is shown until then.
    var hasRates = false
    var rate: Double = 0
    var errorRate: Double = 0
    var received: Double = 0
    var sent: Double = 0
    /// Mean handler time in ms over the last sample; nil when no call finished in it.
    var dispatch: Double?
    var total: UInt64 = 0
    var errors: UInt64 = 0
    var handshakeFailures: UInt64 = 0
    var throttled: UInt64 = 0
    var connections: UInt64 = 0
    var streams: UInt64 = 0
    var rateHistory: [Double] = []
    var receivedHistory: [Double] = []
    var sentHistory: [Double] = []
    var name: String {
        switch Transport(rawValue: UInt32(id)) {
        case .Http3: "HTTP/3"
        case .WebSocket: "WebSocket"
        case .WebTransport: "WebTransport"
        case .Quic: "QUIC"
        case nil: "Unknown"
        }
    }
    var isIdle: Bool { connections == 0 && streams == 0 && rate == 0 && received == 0 && sent == 0 }
    var hasProblems: Bool { errors > 0 || handshakeFailures > 0 || throttled > 0 }
}

enum LinkState: Equatable {
    case disconnected, connecting, live, stale, ended, failed
}

@Observable final class Session: @unchecked Sendable {
    var reference = CommandLine.arguments.dropFirst().first ?? ""
    var http1HandshakeFailures: UInt64 = 0
    var http1Throttled: UInt64 = 0
    var uptimeMs: UInt64 = 0
    var state = LinkState.disconnected
    var status = "Not connected"
    var rows = (0..<4).map { Row(id: UInt32($0)) }
    private var previous: Snapshot?
    private var generation = 0
    private var task: Task<Void, Never>?
    private var reader: NPRPCStreamReader<Snapshot>?
    private var watchdog: Task<Void, Never>?
    private var lastSample = Date()

    func disconnect() {
        generation += 1
        task?.cancel()
        watchdog?.cancel()
        reader?.cancel()
        reader = nil
        previous = nil
        clearLiveValues()
        state = .disconnected
        status = "Disconnected"
    }
    func connect() {
        disconnect()
        let token = generation
        let address = reference
        state = .connecting
        status = "Connecting…"
        rows = (0..<4).map { Row(id: UInt32($0)) }
        // A dead server can take a while to end the stream, and until then
        // nothing would change on screen.
        watchdog = Task.detached { [weak self] in
            while !Task.isCancelled {
                try? await Task.sleep(for: .seconds(1))
                MainQueue.async { [weak self] in
                    guard let self, self.generation == token else { return }
                    self.checkForSilence()
                }
            }
        }
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
                    self.clearLiveValues()
                    self.state = .ended
                    self.status = "Stream ended, reconnect to resume"
                }
            } catch {
                let message = String(describing: error)
                MainQueue.async { [weak self] in
                    guard let self, self.generation == token else { return }
                    self.clearLiveValues()
                    self.state = .failed
                    self.status = "Connection failed: \(message)"
                }
            }
        }
    }
    private func checkForSilence() {
        guard state == .live || state == .stale else { return }
        let silence = Int(Date().timeIntervalSince(lastSample))
        guard silence >= 5 else { return }
        state = .stale
        status = "No data for \(silence)s, the server may be gone"
    }

    /// Rates and open connections describe now, so they can't outlive the link.
    /// Totals and charts stay, as the last known history.
    private func clearLiveValues() {
        uptimeMs = 0
        for index in rows.indices {
            rows[index].hasRates = false
            rows[index].rate = 0
            rows[index].errorRate = 0
            rows[index].received = 0
            rows[index].sent = 0
            rows[index].dispatch = nil
            rows[index].connections = 0
            rows[index].streams = 0
        }
    }

    private func receive(_ value: Snapshot) {
        defer { previous = value }
        http1HandshakeFailures = value.http1_handshake_failures
        http1Throttled = value.http1_throttled
        uptimeMs = value.uptime_ms
        lastSample = Date()
        state = .live
        status = "Live"
        for sample in value.transports where rows.indices.contains(Int(sample.transport.rawValue)) {
            let index = Int(sample.transport.rawValue)
            rows[index].total = sample.completed
            rows[index].errors = sample.errors
            rows[index].handshakeFailures = sample.handshake_failures
            rows[index].throttled = sample.throttled
            rows[index].connections = sample.active_connections
            rows[index].streams = sample.active_streams
            guard let previous, value.uptime_ms > previous.uptime_ms,
                  let old = previous.transports.first(where: { $0.transport == sample.transport }),
                  sample.completed >= old.completed, sample.errors >= old.errors,
                  sample.sent_bytes >= old.sent_bytes, sample.received_bytes >= old.received_bytes,
                  sample.dispatch_ns >= old.dispatch_ns else { continue }
            let seconds = Double(value.uptime_ms - previous.uptime_ms) / 1000
            let calls = sample.completed - old.completed
            rows[index].hasRates = true
            rows[index].rate = Double(calls) / seconds
            rows[index].errorRate = Double(sample.errors - old.errors) / seconds
            rows[index].received = Double(sample.received_bytes - old.received_bytes) / seconds
            rows[index].sent = Double(sample.sent_bytes - old.sent_bytes) / seconds
            rows[index].dispatch = calls == 0 ? nil : Double(sample.dispatch_ns - old.dispatch_ns) / Double(calls) / 1_000_000
            Self.append(rows[index].rate, to: &rows[index].rateHistory)
            Self.append(rows[index].received, to: &rows[index].receivedHistory)
            Self.append(rows[index].sent, to: &rows[index].sentHistory)
        }
    }

    private static func append(_ value: Double, to history: inout [Double]) {
        history.append(value)
        if history.count > historyLength { history.removeFirst() }
    }
}

// MARK: - Formatting

enum Format {
    static func count(_ value: UInt64) -> String { scaled(Double(value), decimals: 0) }

    static func rate(_ value: Double) -> String { scaled(value, decimals: 1) }

    static func plural(_ value: UInt64, _ noun: String) -> String {
        "\(count(value)) \(noun)\(value == 1 ? "" : "s")"
    }

    private static func scaled(_ value: Double, decimals: Int) -> String {
        switch value {
        case 1_000_000_000...: String(format: "%.2fG", value / 1_000_000_000)
        case 1_000_000...: String(format: "%.2fM", value / 1_000_000)
        case 10_000...: String(format: "%.1fk", value / 1000)
        default: String(format: "%.\(decimals)f", value)
        }
    }

    static func bytes(_ value: Double) -> String {
        switch value {
        case 1_073_741_824...: String(format: "%.2f GiB/s", value / 1_073_741_824)
        case 1_048_576...: String(format: "%.2f MiB/s", value / 1_048_576)
        case 1024...: String(format: "%.1f KiB/s", value / 1024)
        default: String(format: "%.0f B/s", value)
        }
    }

    static func milliseconds(_ value: Double) -> String {
        switch value {
        case 1000...: String(format: "%.2f s", value / 1000)
        case 1...: String(format: "%.2f ms", value)
        default: String(format: "%.0f µs", value * 1000)
        }
    }

    static func uptime(_ milliseconds: UInt64) -> String {
        let total = milliseconds / 1000
        let days = total / 86400, hours = total % 86400 / 3600, minutes = total % 3600 / 60, seconds = total % 60
        if days > 0 { return "\(days)d \(hours)h \(minutes)m" }
        if hours > 0 { return "\(hours)h \(minutes)m \(seconds)s" }
        if minutes > 0 { return "\(minutes)m \(seconds)s" }
        return "\(seconds)s"
    }
}

// MARK: - Views

enum Palette {
    static let good = Color(r: 0.36, g: 0.76, b: 0.50)
    static let warning = Color(r: 0.91, g: 0.68, b: 0.28)
    static let bad = Color(r: 0.90, g: 0.38, b: 0.36)
    static let idle = Color(r: 0.50, g: 0.52, b: 0.56)
    static let operations = Color(r: 0.45, g: 0.65, b: 0.95)
    static let received = Color(r: 0.35, g: 0.76, b: 0.76)
    static let sent = Color(r: 0.78, g: 0.56, b: 0.92)
}

enum Fonts {
    nonisolated(unsafe) static var small: UIFont?
    nonisolated(unsafe) static var value: UIFont?
    nonisolated(unsafe) static var title: UIFont?
}

struct Dot: View {
    let color: Color
    var body: some View {
        Canvas(label: "Status", width: .point(10), height: .point(10)) { draw, frame in
            draw.circle(cx: frame.x + 5, cy: frame.y + 5, radius: 4, color: color)
        }
    }
}

/// A caption over a value, with an optional note under it.
struct Metric: View {
    let label: String
    let value: String
    var note: String = ""
    var color: Color = .primary
    var body: some View {
        VStack(flexGrow: 1, width: .point(0), spacing: 2) {
            Text(label, color: .secondary, font: Fonts.small, lineLimit: 1)
            Text(value, color: color, font: Fonts.value, lineLimit: 1)
            if !note.isEmpty { Text(note, color: .secondary, font: Fonts.small, lineLimit: 1) }
        }
    }
}

struct Chart: View {
    struct Series {
        var name: String
        var values: [Double]
        var color: Color
    }
    let title: String
    let series: [Series]
    let format: (Double) -> String

    var body: some View {
        let theme = Environment.current.theme
        let textColor = theme.textSecondary
        Canvas(label: title, width: .point(0), height: .point(92), flexGrow: 1) { draw, frame in
            draw.roundedRect(x: frame.x, y: frame.y, w: frame.w, h: frame.h, color: theme.inset, radius: 4)
            let font = Fonts.small
            let line = font?.lineHeight ?? 16
            let peak = series.flatMap(\.values).max() ?? 0
            let top = max(peak, 1)
            let plotTop = frame.y + line + 6, plotBottom = frame.y + frame.h - 6
            let plotHeight = plotBottom - plotTop
            let left = frame.x + 6, width = frame.w - 12

            for step in 0...2 {
                let y = plotBottom - plotHeight * Float(step) / 2
                draw.line(x1: left, y1: y, x2: left + width, y2: y, color: theme.border, width: 1)
            }
            for item in series where !item.values.isEmpty {
                // The newest sample sits on the right edge, so a young chart fills from there.
                let oldest = item.values.count - 1
                let points = item.values.enumerated().map { index, value in
                    (x: left + width - Float(oldest - index) / Float(historyLength - 1) * width,
                     y: plotBottom - Float(value / top) * plotHeight)
                }
                draw.polyline(points, color: item.color)
                if let last = points.last { draw.circle(cx: last.x, cy: last.y, radius: 2.5, color: item.color) }
            }

            var x = frame.x + 2
            draw.text(title, x: x, y: frame.y + 3, w: frame.w, h: line, color: textColor, font: font)
            x += (font?.measure(title).width ?? 0) + 14
            if series.count > 1 {
                for item in series {
                    draw.text(item.name, x: x, y: frame.y + 3, w: frame.w, h: line, color: item.color, font: font)
                    x += (font?.measure(item.name).width ?? 0) + 12
                }
            }
            let peakText = "peak " + format(peak)
            let peakWidth = font?.measure(peakText).width ?? 0
            draw.text(peakText, x: frame.x + frame.w - peakWidth - 12, y: frame.y + 3, w: peakWidth + 8, h: line,
                      color: textColor, font: font)
        }
    }
}

struct TransportCard: View {
    let row: Row
    var body: some View {
        let dash = "—"
        VStack(flexGrow: 1, width: .point(0), padding: 14, spacing: 10) {
            HStack(alignment: .center, spacing: 8) {
                Dot(color: row.isIdle ? Palette.idle : Palette.good)
                Text(row.name, color: .primary, lineLimit: 1)
                Spacer()
                Text(Format.plural(row.handshakeFailures, "handshake failure"),
                     color: row.handshakeFailures > 0 ? Palette.warning : .secondary, font: Fonts.small, lineLimit: 1)
                Text(Format.count(row.throttled) + " throttled",
                     color: row.throttled > 0 ? Palette.warning : .secondary, font: Fonts.small, lineLimit: 1)
                Text(Format.plural(row.connections, "connection"), color: .secondary, font: Fonts.small, lineLimit: 1)
                Text(Format.plural(row.streams, "stream"), color: .secondary, font: Fonts.small, lineLimit: 1)
            }
            HStack(spacing: 10) {
                Metric(label: "Calls", value: row.hasRates ? Format.rate(row.rate) + "/s" : dash,
                       note: Format.count(row.total) + " total")
                Metric(label: "Errors", value: row.hasRates ? Format.rate(row.errorRate) + "/s" : dash,
                       note: Format.count(row.errors) + " total",
                       color: row.errorRate > 0 ? Palette.bad : .primary)
                Metric(label: "Mean handler", value: row.dispatch.map(Format.milliseconds) ?? dash)
                Metric(label: "Received", value: row.hasRates ? Format.bytes(row.received) : dash)
                Metric(label: "Sent", value: row.hasRates ? Format.bytes(row.sent) : dash)
            }
            HStack(spacing: 8) {
                Chart(title: "Calls/s", series: [.init(name: "Calls", values: row.rateHistory, color: Palette.operations)],
                      format: Format.rate)
                Chart(title: "Bytes", series: [
                    .init(name: "in", values: row.receivedHistory, color: Palette.received),
                    .init(name: "out", values: row.sentHistory, color: Palette.sent)
                ], format: Format.bytes)
            }
        }
        .background(Environment.current.theme.panel)
        .cornerRadius(8)
    }
}

struct Dashboard: View {
    @Bindable var session: Session

    private var stateColor: Color {
        switch session.state {
        case .live: Palette.good
        case .connecting, .stale: Palette.warning
        case .ended: Palette.warning
        case .failed: Palette.bad
        case .disconnected: Palette.idle
        }
    }

    var body: some View {
        let rows = session.rows
        let active = session.state == .live || session.state == .stale || session.state == .connecting
        let handshakes = rows.reduce(session.http1HandshakeFailures) { $0 + $1.handshakeFailures }
        let throttled = rows.reduce(session.http1Throttled) { $0 + $1.throttled }
        let errors = rows.reduce(0) { $0 + $1.errors }

        VStack(padding: 8) {
            HStack(alignment: .center, spacing: 12) {
                if WindowBridge.drawsOwnChrome {
                    WindowControls().windowChrome()
                }
                Text("NPRPC Monitor", color: .primary, font: Fonts.title, lineLimit: 1)
                Dot(color: stateColor)
                Text(session.status, color: session.state == .failed ? Palette.bad : .secondary, lineLimit: 1)
                Spacer()
                Button(active ? "Reconnect" : "Connect", isEnabled: !session.reference.isEmpty) { session.connect() }
                Button("Disconnect", isEnabled: active) { session.disconnect() }
            }
            ScrollView {
                VStack(padding: 16, spacing: 12) {
                    TextField(text: $session.reference, placeholder: "Monitor object reference", font: Fonts.small)
                        .padding(8)
                        .background(Environment.current.theme.inset)
                        .cornerRadius(6)
                    HStack(spacing: 12) {
                        Metric(label: "Uptime", value: session.uptimeMs > 0 ? Format.uptime(session.uptimeMs) : "—")
                        Metric(label: "Calls", value: Format.rate(rows.reduce(0) { $0 + $1.rate }) + "/s")
                        Metric(label: "Errors", value: Format.count(errors), color: errors > 0 ? Palette.bad : .primary)
                        Metric(label: "Received", value: Format.bytes(rows.reduce(0) { $0 + $1.received }))
                        Metric(label: "Sent", value: Format.bytes(rows.reduce(0) { $0 + $1.sent }))
                        Metric(label: "Connections", value: Format.count(rows.reduce(0) { $0 + $1.connections }))
                        Metric(label: "Streams", value: Format.count(rows.reduce(0) { $0 + $1.streams }))
                        Metric(label: "Handshake failures", value: Format.count(handshakes), color: handshakes > 0 ? Palette.warning : .primary)
                        Metric(label: "Throttled", value: Format.count(throttled), color: throttled > 0 ? Palette.warning : .primary)
                    }
                    .padding(14)
                    .background(Environment.current.theme.panel)
                    .cornerRadius(8)
                    HStack(spacing: 12) {
                        TransportCard(row: rows[0])
                        TransportCard(row: rows[1])
                    }
                    HStack(spacing: 12) {
                        TransportCard(row: rows[2])
                        TransportCard(row: rows[3])
                    }
                    Text("HTTP/1.1: \(session.http1HandshakeFailures) handshake failures, \(session.http1Throttled) throttled. Totals above include these.",
                        color: .secondary, font: Fonts.small)
                    Text("Payload bytes, not wire bytes. Streaming data frames are excluded. Charts cover the last \(historyLength) samples and scale independently.",
                        color: .secondary, font: Fonts.small)
                }
            }
        }
        .windowDrag()
    }
}

guard let editor = LavaHost.open(title: "NPRPC Monitor", width: 1240, height: 860) else { exit(1) }
LavaHost.setMinimumSize(editor: editor, width: 900, height: 500)
// A face not registered with the engine draws from the wrong atlas.
func loadFont(pixelSize: Float) -> UIFont? {
    guard let font = UIFont.loadUI(assetsRoot: LavaResources.root, pixelSize: pixelSize),
          font.registerWithEngine(editor) else { return nil }
    return font
}
Fonts.small = loadFont(pixelSize: 12)
Fonts.value = loadFont(pixelSize: 17)
Fonts.title = loadFont(pixelSize: 20)
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

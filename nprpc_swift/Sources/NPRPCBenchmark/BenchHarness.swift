// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

import Foundation
import NPRPC

// MARK: - Result model

struct BenchResult: Codable, Sendable {
    var name: String
    var transport: String
    var iterations: Int
    /// Wall-clock nanoseconds per iteration (mean).
    var nsPerOp: Double
    var opsPerSec: Double
    /// Payload bytes counted per iteration (request side, matching C++ SetBytesProcessed intent).
    var bytesPerOp: Int
    /// MB/s using 1024-based MiB when bytesPerOp > 0.
    var mbPerSec: Double?
    var totalSeconds: Double
}

struct BenchReport: Codable, Sendable {
    var meta: Meta
    var benchmarks: [BenchResult]

    struct Meta: Codable, Sendable {
        var date: String
        var language: String
        var client: String
        var server: String
        var commit: String
        var minTimeSeconds: Double
        var warmupIterations: Int
        var note: String
    }
}

// MARK: - Timing

enum BenchHarness {
    /// Run `body` until `minTime` wall time elapses (after warmup).
    static func measure(
        name: String,
        transport: String,
        warmup: Int,
        minTime: Duration,
        maxIterations: Int,
        bytesPerOp: Int,
        body: () async throws -> Void
    ) async throws -> BenchResult {
        for _ in 0..<warmup {
            try await body()
        }

        var iterations = 0
        let clock = ContinuousClock()
        let start = clock.now
        var elapsed: Duration = .zero

        // Batch-check elapsed to keep timer overhead out of the hot path.
        let checkEvery = max(1, min(256, maxIterations / 64))

        while iterations < maxIterations {
            try await body()
            iterations += 1
            if iterations % checkEvery == 0 {
                elapsed = clock.now - start
                if elapsed >= minTime { break }
            }
        }
        elapsed = clock.now - start

        let totalSeconds = durationSeconds(elapsed)
        let nsPerOp = totalSeconds * 1e9 / Double(max(iterations, 1))
        let opsPerSec = Double(iterations) / max(totalSeconds, 1e-12)
        let mbPerSec: Double? = bytesPerOp > 0
            ? (Double(iterations) * Double(bytesPerOp) / totalSeconds) / (1024.0 * 1024.0)
            : nil

        return BenchResult(
            name: name,
            transport: transport,
            iterations: iterations,
            nsPerOp: nsPerOp,
            opsPerSec: opsPerSec,
            bytesPerOp: bytesPerOp,
            mbPerSec: mbPerSec,
            totalSeconds: totalSeconds
        )
    }

    private static func durationSeconds(_ d: Duration) -> Double {
        let comps = d.components
        return Double(comps.seconds) + Double(comps.attoseconds) * 1e-18
    }

    // MARK: - Output

    static func printTable(_ results: [BenchResult]) {
        func pad(_ s: String, _ w: Int, right: Bool = false) -> String {
            if s.count >= w { return String(s.prefix(w)) }
            let spaces = String(repeating: " ", count: w - s.count)
            return right ? spaces + s : s + spaces
        }

        print()
        print(String(repeating: "-", count: 100))
        print(
            pad("Benchmark", 40)
                + pad("Time", 12, right: true)
                + pad("Iterations", 14, right: true)
                + pad("calls/sec", 14, right: true)
                + pad("MB/sec", 12, right: true)
        )
        print(String(repeating: "-", count: 100))

        for r in results {
            let timeStr: String
            if r.nsPerOp >= 1e6 {
                timeStr = String(format: "%.2f ms", r.nsPerOp / 1e6)
            } else if r.nsPerOp >= 1e3 {
                timeStr = String(format: "%.2f us", r.nsPerOp / 1e3)
            } else {
                timeStr = String(format: "%.1f ns", r.nsPerOp)
            }
            let rateStr: String
            if r.opsPerSec >= 1e6 {
                rateStr = String(format: "%.3fM/s", r.opsPerSec / 1e6)
            } else if r.opsPerSec >= 1e3 {
                rateStr = String(format: "%.2fk/s", r.opsPerSec / 1e3)
            } else {
                rateStr = String(format: "%.1f/s", r.opsPerSec)
            }
            let mbStr = r.mbPerSec.map { String(format: "%.2f", $0) } ?? "-"
            let label = "\(r.name)/\(r.transport)"
            print(
                pad(label, 40)
                    + pad(timeStr, 12, right: true)
                    + pad(String(r.iterations), 14, right: true)
                    + pad(rateStr, 14, right: true)
                    + pad(mbStr, 12, right: true)
            )
        }
        print(String(repeating: "-", count: 100))
        print()
    }

    static func writeJSON(_ report: BenchReport, to path: String) throws {
        let enc = JSONEncoder()
        enc.outputFormatting = [.prettyPrinted, .sortedKeys]
        enc.nonConformingFloatEncodingStrategy = .convertToString(
            positiveInfinity: "inf",
            negativeInfinity: "-inf",
            nan: "nan"
        )
        let data = try enc.encode(report)
        try data.write(to: URL(fileURLWithPath: path), options: .atomic)
        print("Wrote JSON results to \(path)")
    }

    static func writeCSV(_ results: [BenchResult], to path: String) throws {
        var lines = ["name,transport,iterations,ns_per_op,ops_per_sec,bytes_per_op,mb_per_sec,total_seconds"]
        for r in results {
            let mb = r.mbPerSec.map { String($0) } ?? ""
            lines.append([
                r.name,
                r.transport,
                String(r.iterations),
                String(r.nsPerOp),
                String(r.opsPerSec),
                String(r.bytesPerOp),
                mb,
                String(r.totalSeconds),
            ].joined(separator: ","))
        }
        try lines.joined(separator: "\n").write(
            to: URL(fileURLWithPath: path),
            atomically: true,
            encoding: .utf8
        )
        print("Wrote CSV results to \(path)")
    }
}

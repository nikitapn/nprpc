// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT
//
// Swift↔Swift NPRPC microbenchmarks (baseline for FlatBuffer bridge work).
//
// Mirrors C++ benchmark/src/benchmark_latency.cpp cases against the same
// nprpc_benchmark.npidl interface. Default transport is shared memory
// (in-process client + servant) so results isolate the Swift bridge + SHM path.
//
// Usage:
//   swift run -c release nprpc-swift-benchmark -- --json ../benchmark/results/swift/baseline.json
//   swift run -c release nprpc-swift-benchmark -- --filter EmptyCall --min-time 2
//
// Compare with C++:
//   ./build/benchmark/nprpc_benchmarks --benchmark_filter=Latency --benchmark_format=json

import Foundation
import NPRPC

// MARK: - CLI

struct Options {
    var transport: String = "shm" // shm | tcp
    var filter: String? = nil
    var minTimeSeconds: Double = 1.0
    var warmup: Int = 200
    var maxIterations: Int = 2_000_000
    var jsonPath: String? = nil
    var csvPath: String? = nil
    var help: Bool = false
}

func parseArgs(_ args: [String]) -> Options {
    var o = Options()
    var i = 0
    while i < args.count {
        let a = args[i]
        switch a {
        case "--help", "-h":
            o.help = true
        case "--transport":
            i += 1
            if i < args.count { o.transport = args[i] }
        case "--filter":
            i += 1
            if i < args.count { o.filter = args[i] }
        case "--min-time":
            i += 1
            if i < args.count { o.minTimeSeconds = Double(args[i]) ?? o.minTimeSeconds }
        case "--warmup":
            i += 1
            if i < args.count { o.warmup = Int(args[i]) ?? o.warmup }
        case "--max-iterations":
            i += 1
            if i < args.count { o.maxIterations = Int(args[i]) ?? o.maxIterations }
        case "--json":
            i += 1
            if i < args.count { o.jsonPath = args[i] }
        case "--csv":
            i += 1
            if i < args.count { o.csvPath = args[i] }
        default:
            FileHandle.standardError.write(Data("Unknown argument: \(a)\n".utf8))
        }
        i += 1
    }
    return o
}

func printHelp() {
    print(
        """
        nprpc-swift-benchmark — Swift↔Swift NPRPC latency/bandwidth baseline

        Options:
          --transport shm|tcp   Transport (default: shm). tcp needs free ports.
          --filter SUBSTR       Only run benchmarks whose name contains SUBSTR
          --min-time SECONDS    Target measure wall time per case (default: 1.0)
          --warmup N            Warmup iterations (default: 200)
          --max-iterations N    Cap iterations (default: 2000000)
          --json PATH           Write JSON report
          --csv PATH            Write CSV report
          --help                This help

        Example (record baseline):
          swift run -c release nprpc-swift-benchmark -- \\
            --json ../benchmark/results/swift/baseline.json
        """
    )
}

// MARK: - Setup

func makeRpc(transport: String) throws -> (Rpc, Poa, ObjectActivationFlags) {
    let flags: ObjectActivationFlags
    let rpc: Rpc
    switch transport {
    case "shm":
        // SHM listener always starts with Rpc; no TCP/HTTP needed.
        flags = .shm
        rpc = try RpcBuilder()
            .setLogLevel(.error)
            .withHostname("localhost")
            .build()
    case "tcp":
        flags = .tcp
        rpc = try RpcBuilder()
            .setLogLevel(.error)
            .withHostname("localhost")
            .withTcp(23222)
            .build()
    default:
        throw RuntimeError(message: "Unsupported transport: \(transport) (use shm or tcp)")
    }

    let poa = try rpc.createPoa(maxObjects: 64)
    try rpc.startThreadPool(2)
    // Brief settle for accept ring / listeners.
    Thread.sleep(forTimeInterval: 0.05)
    return (rpc, poa, flags)
}

func makeClient(poa: Poa, servant: NPRPCServant, flags: ObjectActivationFlags) throws -> Benchmark {
    let oid = try poa.activateObject(servant, flags: flags)
    guard let obj = NPRPCObject.fromObjectId(oid) else {
        throw RuntimeError(message: "fromObjectId failed")
    }
    if flags.contains(.shm) && !flags.contains(.tcp) {
        guard obj.endpoint.type == .SharedMemory else {
            throw RuntimeError(message: "Expected SharedMemory endpoint, got \(obj.endpoint.toURL())")
        }
    }
    guard let client = narrow(obj, to: Benchmark.self) else {
        throw RuntimeError(message: "narrow to Benchmark failed")
    }
    return client
}

func transportLabel(_ o: Options) -> String {
    switch o.transport {
    case "shm": return "SharedMemory"
    case "tcp": return "TCP"
    default: return o.transport
    }
}

func matchesFilter(_ name: String, _ filter: String?) -> Bool {
    guard let filter, !filter.isEmpty else { return true }
    return name.range(of: filter, options: .caseInsensitive) != nil
}

func gitCommit() -> String {
    let p = Process()
    p.executableURL = URL(fileURLWithPath: "/usr/bin/git")
    p.arguments = ["rev-parse", "--short", "HEAD"]
    p.currentDirectoryURL = URL(fileURLWithPath: #filePath)
        .deletingLastPathComponent()
        .deletingLastPathComponent()
        .deletingLastPathComponent()
        .deletingLastPathComponent()
    let pipe = Pipe()
    p.standardOutput = pipe
    p.standardError = FileHandle.nullDevice
    do {
        try p.run()
        p.waitUntilExit()
        let data = pipe.fileHandleForReading.readDataToEndOfFile()
        return String(data: data, encoding: .utf8)?
            .trimmingCharacters(in: .whitespacesAndNewlines) ?? "unknown"
    } catch {
        return "unknown"
    }
}

// MARK: - Cases (aligned with C++ LatencyFixture)

@main
struct NPRPCSwiftBenchmark {
    static func main() async {
        do {
            try await run()
        } catch {
            FileHandle.standardError.write(Data("Benchmark failed: \(error)\n".utf8))
            Foundation.exit(1)
        }
    }

    static func run() async throws {
        var args = Array(CommandLine.arguments.dropFirst())
        // SPM may leave a "--" separator.
        if let idx = args.firstIndex(of: "--") {
            args.remove(at: idx)
        }
        let opts = parseArgs(args)
        if opts.help {
            printHelp()
            return
        }

        let tLabel = transportLabel(opts)
        print("nprpc-swift-benchmark")
        print("  transport: \(tLabel)")
        print("  min-time:  \(opts.minTimeSeconds)s  warmup: \(opts.warmup)")
        print("  client:    async (send_receive_async)  server: Swift servant")
        print()

        let (rpc, poa, flags) = try makeRpc(transport: opts.transport)
        defer { _ = rpc /* keep alive for process lifetime */ }

        let servant = BenchmarkServantImpl()
        let client = try makeClient(poa: poa, servant: servant, flags: flags)
        client.timeout = 30_000

        let minTime = Duration.seconds(opts.minTimeSeconds)
        var results: [BenchResult] = []

        func runCase(
            _ name: String,
            bytesPerOp: Int = 0,
            maxIter: Int? = nil,
            body: @escaping () async throws -> Void
        ) async throws {
            guard matchesFilter(name, opts.filter) else { return }
            print("  running \(name)...")
            let r = try await BenchHarness.measure(
                name: name,
                transport: tLabel,
                warmup: opts.warmup,
                minTime: minTime,
                maxIterations: maxIter ?? opts.maxIterations,
                bytesPerOp: bytesPerOp,
                body: body
            )
            results.append(r)
            let us = r.nsPerOp / 1e3
            print(String(format: "    -> %.2f us/op  (%.2fk calls/s, n=%d)", us, r.opsPerSec / 1e3, r.iterations))
        }

        // Latency/EmptyCall
        try await runCase("Latency/EmptyCall") {
            try await client.ping()
        }

        // Latency/CallWithReturn
        try await runCase("Latency/CallWithReturn") {
            let v = try await client.func1(a: 42, b: 24)
            blackHole(v)
        }

        // Latency/SmallStringCall — 100 bytes
        let smallString = String(repeating: "x", count: 100)
        try await runCase("Latency/SmallStringCall", bytesPerOp: 100) {
            try await client.func2(data: smallString)
        }

        // Latency/NestedDataCall
        let employee = Employee(
            person: Person(name: "John Doe", age: 30, email: "j@example.com"),
            address: Address(street: "123 Main St", city: "Springfield", country: "US", zipCode: 12345),
            employeeId: 1001,
            salary: 75000.50,
            skills: ["C++", "Python", "JavaScript", "Rust", "Go"]
        )
        try await runCase("Latency/NestedDataCall") {
            let r = try await client.processEmployee(employee: employee)
            blackHole(r.employeeId)
        }

        // Latency/LargeData — sizes matching C++ 1MB (+ smaller for quick bandwidth curve)
        for size in [1 << 10, 64 << 10, 1 << 20] {
            let payload = [UInt8](repeating: 0x42, count: size)
            // Request + response both carry `size` bytes of payload.
            let name = size >= 1 << 20
                ? "Latency/LargeData1MB"
                : "Bandwidth/PayloadSize/\(size)"
            // Cap iterations for large payloads so a 1s min-time stays practical.
            let maxIter = size >= 1 << 20 ? 5_000 : opts.maxIterations
            try await runCase(name, bytesPerOp: size * 2, maxIter: maxIter) {
                let out = try await client.processLargeData(data: payload)
                blackHole(out.count)
            }
        }

        BenchHarness.printTable(results)

        let iso = ISO8601DateFormatter()
        iso.formatOptions = [.withInternetDateTime]
        let report = BenchReport(
            meta: .init(
                date: iso.string(from: Date()),
                language: "swift",
                client: "async",
                server: "swift",
                commit: gitCommit(),
                minTimeSeconds: opts.minTimeSeconds,
                warmupIterations: opts.warmup,
                note: "Swift client uses send_receive_async; C++ latency benches are mostly sync. Compare SHM first."
            ),
            benchmarks: results
        )

        if let path = opts.jsonPath {
            try FileManager.default.createDirectory(
                at: URL(fileURLWithPath: path).deletingLastPathComponent(),
                withIntermediateDirectories: true
            )
            try BenchHarness.writeJSON(report, to: path)
        }
        if let path = opts.csvPath {
            try FileManager.default.createDirectory(
                at: URL(fileURLWithPath: path).deletingLastPathComponent(),
                withIntermediateDirectories: true
            )
            try BenchHarness.writeCSV(results, to: path)
        }

        if opts.jsonPath == nil && opts.csvPath == nil {
            print("Tip: pass --json PATH to record a baseline for later comparison.")
        }
    }
}

/// Prevent the optimizer from eliding the call result.
@inline(never)
func blackHole<T>(_ value: T) {
    withUnsafePointer(to: value) { _ = $0.pointee }
}

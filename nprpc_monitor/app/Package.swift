// swift-tools-version: 6.3
import Foundation
import PackageDescription

// This executable needs networking even when the desktop itself uses SHM only.
// Diagnose a direct SwiftPM invocation that silently selects Lava's small build.
let repo = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
    .deletingLastPathComponent().deletingLastPathComponent()
let env = ProcessInfo.processInfo.environment
let selected = env["NPRPC_BUILD_DIR"] ??
    (FileManager.default.fileExists(atPath: repo.appendingPathComponent(".build_lava_shm/libnprpc.so").path)
        ? ".build_lava_shm" : ".build_relwith_debinfo")
let build = selected.hasPrefix("/") ? URL(fileURLWithPath: selected) : repo.appendingPathComponent(selected)
let cache = (try? String(contentsOf: build.appendingPathComponent("CMakeCache.txt"), encoding: .utf8)) ?? ""
for feature in ["TCP", "HTTP", "WEBSOCKET", "SSL", "QUIC", "HTTP3"] {
    precondition(cache.contains("NPRPC_ENABLE_\(feature):BOOL=ON"),
        "NPRPCMonitor requires a full transport build (missing \(feature) at \(build.path)). Use nprpc_monitor/run.sh or set NPRPC_BUILD_DIR to a full build.")
}
let package = Package(
    name: "NPRPCMonitorApp",
    products: [.executable(name: "NPRPCMonitor", targets: ["NPRPCMonitor"])],
    dependencies: [
        .package(path: ".."),
        .package(path: "../../nprpc_swift"),
        .package(path: "../../../HelloWorld")
    ],
    targets: [.executableTarget(name: "NPRPCMonitor", dependencies: [
        .product(name: "MonitorAPI", package: "nprpc_monitor"),
        .product(name: "NPRPC", package: "nprpc_swift"),
        .product(name: "LavaUI", package: "HelloWorld"),
        .product(name: "LavaHost", package: "HelloWorld")
    ], swiftSettings: [.interoperabilityMode(.Cxx)])],
    cxxLanguageStandard: .gnucxx2b)

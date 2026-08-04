// swift-tools-version: 6.3
// The swift-tools-version declares the minimum version of Swift required to build this package.
//
// Consumer / GitHub mode (default): no unsafeFlags. Link against a system-wide
// libnprpc (headers on the default include path, libnprpc.so on the linker path).
//
// Monorepo / developer mode: set NPRPC_ROOT (and optionally NPRPC_BUILD_DIR) so
// the package can pick up an in-tree CMake build. That path uses unsafeFlags and
// is only intended for local development — not for packages that depend on NPRPC
// via SPM.

import Foundation
import PackageDescription

// MARK: - Path resolution (dev only)

let env = ProcessInfo.processInfo.environment

/// Absolute path to the nprpc monorepo root, if building against an in-tree tree.
/// Precedence:
///   1. NPRPC_SYSTEM_ONLY=1 → force system install (no monorepo paths / no unsafeFlags)
///   2. NPRPC_ROOT env
///   3. Auto-detect parent of this package when it looks like a monorepo checkout
///      (contains include/nprpc/nprpc.hpp)
let nprpcRoot: String? = {
    if env["NPRPC_SYSTEM_ONLY"] == "1" {
        return nil
    }
    if let root = env["NPRPC_ROOT"], !root.isEmpty {
        return root
    }
    let packageDir = URL(fileURLWithPath: #filePath)
        .deletingLastPathComponent()
        .path
    let parent = URL(fileURLWithPath: packageDir)
        .deletingLastPathComponent()
        .path
    let marker = (parent as NSString).appendingPathComponent("include/nprpc/nprpc.hpp")
    if FileManager.default.fileExists(atPath: marker) {
        return parent
    }
    return nil
}()

/// CMake build directory name or absolute path (only used when nprpcRoot != nil).
let nprpcBuildDir = env["NPRPC_BUILD_DIR"] ?? ".build_relwith_debinfo"

let buildPath: String? = {
    guard let root = nprpcRoot else { return nil }
    if nprpcBuildDir.hasPrefix("/") {
        return nprpcBuildDir
    }
    return (root as NSString).appendingPathComponent(nprpcBuildDir)
}()

// MARK: - Target settings

var cNprpcCxxSettings: [CXXSetting] = []
var cNprpcLinkerSettings: [LinkerSetting] = [
    .linkedLibrary("nprpc"),
]

if let root = nprpcRoot, let buildPath {
    // In-tree development against a CMake build. Not used for published SPM consumers.
    let boostInstall = (buildPath as NSString).appendingPathComponent("boost_install")
    cNprpcCxxSettings += [
        .unsafeFlags(["-I", (root as NSString).appendingPathComponent("include")]),
        .unsafeFlags(["-I", (buildPath as NSString).appendingPathComponent("include")]),
        // Docker-only vendored Boost; missing path is a no-op for the linker when empty.
        .unsafeFlags(["-I", (boostInstall as NSString).appendingPathComponent("include")]),
    ]
    cNprpcLinkerSettings += [
        .unsafeFlags(["-L", buildPath]),
        .unsafeFlags(["-L", (boostInstall as NSString).appendingPathComponent("lib")]),
        // Absolute rpaths — $ORIGIN is unreliable with Swift's injected rpath order on Linux.
        .unsafeFlags(["-Xlinker", "-rpath", "-Xlinker", buildPath]),
        .unsafeFlags([
            "-Xlinker", "-rpath", "-Xlinker",
            (boostInstall as NSString).appendingPathComponent("lib"),
        ]),
    ]
}

// MARK: - Package

let package = Package(
    name: "NPRPC",
    platforms: [
        .macOS(.v13),
    ],
    products: [
        .library(
            name: "NPRPC",
            targets: ["NPRPC"]
        ),
        .executable(
            name: "nprpc-swift-benchmark",
            targets: ["NPRPCBenchmark"]
        ),
    ],
    targets: [
        .target(
            name: "CNprpc",
            dependencies: [],
            path: "Sources/CNprpc",
            publicHeadersPath: "include",
            cxxSettings: cNprpcCxxSettings,
            linkerSettings: cNprpcLinkerSettings
        ),
        .target(
            name: "NPRPC",
            dependencies: ["CNprpc"],
            path: "Sources/NPRPC",
            swiftSettings: [
                .interoperabilityMode(.Cxx),
            ]
        ),
        .testTarget(
            name: "NPRPCTests",
            dependencies: ["NPRPC"],
            swiftSettings: [
                .interoperabilityMode(.Cxx),
            ]
        ),
        .executableTarget(
            name: "NPRPCBenchmark",
            dependencies: ["NPRPC"],
            path: "Sources/NPRPCBenchmark",
            swiftSettings: [
                .interoperabilityMode(.Cxx),
            ]
        ),
    ],
    cxxLanguageStandard: .cxx2b
)

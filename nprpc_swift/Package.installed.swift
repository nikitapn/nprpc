// swift-tools-version: 6.2
// This file is copied over Package.swift in the final nprpc-dev Docker image.
// It declares CNprpc as a .systemLibrary so downstream packages never recompile
// nprpc_bridge.cpp — they link directly against the pre-built libnprpc.so.
// Compiler/linker flags are supplied by the nprpc-swift.pc pkg-config file.
//
// The source Package.swift (with .target / nprpc_bridge.cpp) is still used
// during the Docker image build (swift-builder stage) to produce the artifacts.

import PackageDescription

let package = Package(
    name: "NPRPC",
    platforms: [
        .macOS(.v13)
    ],
    products: [
        .library(
            name: "NPRPC",
            targets: ["NPRPC"]),
    ],
    targets: [
        // C++ bridge — pre-built into libnprpc.so.
        // Flags (includes, libs, rpath) come from nprpc-swift.pc.
        .systemLibrary(
            name: "CNprpc",
            path: "Sources/CNprpc",
            pkgConfig: "nprpc-swift"
        ),

        .target(
            name: "NPRPC",
            dependencies: ["CNprpc"],
            path: "Sources/NPRPC",
            exclude: [],
            swiftSettings: [
                .interoperabilityMode(.Cxx)
            ]
        ),

        .testTarget(
            name: "NPRPCTests",
            dependencies: ["NPRPC"],
            exclude: [],
            swiftSettings: [
                .interoperabilityMode(.Cxx)
            ]
        ),
    ],
    cxxLanguageStandard: .cxx20
)

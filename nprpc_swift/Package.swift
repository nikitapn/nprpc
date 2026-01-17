// swift-tools-version: 5.9
// The swift-tools-version declares the minimum version of Swift required to build this package.

import PackageDescription

// Get the absolute path to nprpc root (parent of nprpc_swift)
// For local development, we need to reference headers outside the package

let package = Package(
    name: "NPRPC",
    platforms: [
        .macOS(.v13)  // For Linux, this is ignored
    ],
    products: [
        .library(
            name: "NPRPC",
            targets: ["NPRPC"]),
        .executable(
            name: "nprpc-poc",
            targets: ["NPRPCPoC"]),
    ],
    targets: [
        // C++ bridge module - exposes nprpc headers to Swift
        // For POC, this is self-contained and doesn't depend on libnprpc
        .target(
            name: "CNprpc",
            dependencies: [],
            path: "Sources/CNprpc",
            publicHeadersPath: "include",
            cxxSettings: [
                .unsafeFlags(["-std=c++20"])
            ]
        ),
        
        // Main Swift wrapper library
        .target(
            name: "NPRPC",
            dependencies: ["CNprpc"],
            path: "Sources/NPRPC",
            swiftSettings: [
                .interoperabilityMode(.Cxx)
            ]
        ),
        
        // Proof of Concept executable
        .executableTarget(
            name: "NPRPCPoC",
            dependencies: ["NPRPC"],
            path: "Sources/NPRPCPoC",
            swiftSettings: [
                .interoperabilityMode(.Cxx)
            ]
        ),
        
        // Tests
        .testTarget(
            name: "NPRPCTests",
            dependencies: ["NPRPC"],
            swiftSettings: [
                .interoperabilityMode(.Cxx)
            ]
        ),
    ],
    cxxLanguageStandard: .cxx20
)

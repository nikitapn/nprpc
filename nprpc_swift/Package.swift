// swift-tools-version: 6.2
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
        .executable(
            name: "http3-server",
            targets: ["HTTP3Server"]),
    ],
    targets: [
        // C++ bridge module - exposes nprpc headers to Swift
        .target(
            name: "CNprpc",
            dependencies: [],
            path: "Sources/CNprpc",
            publicHeadersPath: "include",
            cxxSettings: [
                .unsafeFlags(["-std=c++23"]),
                // Include nprpc headers from the parent build
                .unsafeFlags(["-I", "../include"]),
                .unsafeFlags(["-I", "../.build_ubuntu_swift/include"]),
                // Include Clang 17-built Boost (not system Boost)
                .unsafeFlags(["-I", "../.build_ubuntu_swift/boost_install/include"])
            ],
            linkerSettings: [
                // Link against libnprpc.so
                .linkedLibrary("nprpc"),
                // Link against OpenSSL (required by NPRPC)
                .linkedLibrary("ssl"),
                .linkedLibrary("crypto"),
                // Add library search path for local development
                .unsafeFlags(["-L", "../.build_ubuntu_swift"]),
                // Add rpath for runtime
                .unsafeFlags(["-Xlinker", "-rpath", "-Xlinker", "$ORIGIN/../../.build_ubuntu_swift"])
            ]
        ),
        
        // Main Swift wrapper library
        .target(
            name: "NPRPC",
            dependencies: ["CNprpc"],
            path: "Sources/NPRPC",
            exclude: [
                "Generated/nprpc_base.cpp",
                "Generated/nprpc_base.hpp",
                "Generated/nprpc_nameserver.cpp",
                "Generated/nprpc_nameserver.hpp"
            ],
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
        
        // HTTP3 Server example
        .executableTarget(
            name: "HTTP3Server",
            dependencies: ["NPRPC"],
            path: "Sources/HTTP3Server",
            swiftSettings: [
                .interoperabilityMode(.Cxx)
            ]
        ),
        
        // Tests
        .testTarget(
            name: "NPRPCTests",
            dependencies: ["NPRPC"],
            exclude: [
                "Generated/basic_test.cpp",
                "Generated/basic_test.hpp"
            ],
            swiftSettings: [
                .interoperabilityMode(.Cxx)
            ]
        ),
    ],
    cxxLanguageStandard: .cxx20
)

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
            targets: ["NPRPC"])
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
                .unsafeFlags(["-I", "../.build_ubuntu_swift/boost_install/include"]),
                .unsafeFlags(["-I", "../third_party/boringssl/include"])
            ],
            linkerSettings: [
                // Link against libnprpc.so (BoringSSL is statically absorbed inside it)
                .linkedLibrary("nprpc"),
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
            exclude: [ ],
            swiftSettings: [
                .interoperabilityMode(.Cxx)
            ]
        ),

        // Tests
        .testTarget(
            name: "NPRPCTests",
            dependencies: ["NPRPC"],
            exclude: [ ],
            swiftSettings: [
                .interoperabilityMode(.Cxx)
            ],
            linkerSettings: [
                // BoringSSL is built with -fvisibility=hidden so ERR_* symbols
                // are not exported from libnprpc.so. Link the static archives
                // directly here so the test binary resolves them at link time.
                // This only affects the test binary — consumer packages (which
                // link libnprpc.so and don't include ssl/impl/error.ipp directly)
                // are unaffected since test targets don't propagate linker settings.
                .unsafeFlags(["-L", "../.build_ubuntu_swift/third_party/boringssl"]),
                .linkedLibrary("ssl"),
                .linkedLibrary("crypto"),
            ]
        ),
    ],
    cxxLanguageStandard: .cxx20
)

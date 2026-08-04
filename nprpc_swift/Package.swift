// swift-tools-version: 6.3
// The swift-tools-version declares the minimum version of Swift required to build this package.

import Foundation
import PackageDescription

// Resolve the nprpc repo root from this manifest's own location (parent of
// nprpc_swift), rather than hardcoding a path. That makes the same
// Package.swift work unmodified on any host checkout as well as inside the
// Docker container, where the repo is bind-mounted at /workspace.
let nprpcRoot = URL(fileURLWithPath: #filePath)
    .deletingLastPathComponent()
    .deletingLastPathComponent()
    .path

// Which CMake build directory (under nprpcRoot) to link nprpc against.
// Defaults to the RelWithDebInfo build used for day-to-day host development.
// Override to switch build flavors or environments, e.g.:
//   NPRPC_BUILD_DIR=.build_release swift build        # host, Release
//   NPRPC_BUILD_DIR=.build_ubuntu_swift swift build   # Docker (Clang 17 + in-tree Boost)
let nprpcBuildDir = ProcessInfo.processInfo.environment["NPRPC_BUILD_DIR"] ?? ".build_relwith_debinfo"
let buildPath = "\(nprpcRoot)/\(nprpcBuildDir)"

// Docker builds vendor their own Clang-built Boost under the build dir
// (see docker-build-boost.sh); host builds link system Boost instead, so
// this directory simply won't exist there. Missing -I/-L/rpath entries are
// silently ignored by the compiler/linker/loader, so it's safe to always
// pass them.
let boostInstallPath = "\(buildPath)/boost_install"

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
                .unsafeFlags(["-I", "\(nprpcRoot)/include"]),
                .unsafeFlags(["-I", "\(buildPath)/include"]),
                // Docker-only: Clang 17-built Boost (host builds use system Boost instead)
                .unsafeFlags(["-I", "\(boostInstallPath)/include"]),
            ],
            linkerSettings: [
                .linkedLibrary("nprpc"),
                // Library search paths: nprpc build dir, plus Docker's in-tree Boost (no-op on host)
                .unsafeFlags(["-L", buildPath]),
                .unsafeFlags(["-L", "\(boostInstallPath)/lib"]),
                // Runtime rpaths, same two locations.
                // NOTE: must be absolute paths, not $ORIGIN-relative. The Swift toolchain
                // injects its own rpath entry (/usr/lib/swift/lib/swift/linux, which is also
                // registered in /etc/ld.so.conf.d/swift.conf) ahead of ours, and once the
                // dynamic linker misses on that entry it silently skips any subsequent
                // $ORIGIN-relative rpath entries instead of continuing to search them.
                .unsafeFlags(["-Xlinker", "-rpath", "-Xlinker", buildPath]),
                .unsafeFlags(["-Xlinker", "-rpath", "-Xlinker", "\(boostInstallPath)/lib"])
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
        // Integration tests
        .testTarget(
            name: "NPRPCTests",
            dependencies: ["NPRPC"],
            exclude: [ ],
            swiftSettings: [
                .interoperabilityMode(.Cxx)
            ]
        ),
    ],
    cxxLanguageStandard: .cxx2b
)

// swift-tools-version: 6.3
import PackageDescription

let package = Package(
    name: "NPRPCMonitor",
    products: [
        .library(name: "MonitorAPI", targets: ["MonitorAPI"])
        ],
    dependencies: [
        .package(path: "../nprpc_swift")
        ],
    targets: [
        .executableTarget(name: "MonitorProbe",
            dependencies: ["MonitorAPI"],
            swiftSettings: [.interoperabilityMode(.Cxx)]),
        .executableTarget(name: "MonitorDemo",
            dependencies: [.product(name: "NPRPC", package: "nprpc_swift")],
            resources: [.copy("Public")],
            swiftSettings: [.interoperabilityMode(.Cxx)]),
        .target(name: "MonitorAPI",
            dependencies: [.product(name: "NPRPC", package: "nprpc_swift")],
            swiftSettings: [.interoperabilityMode(.Cxx)]),


    ],
    cxxLanguageStandard: .gnucxx2b)

// swift-tools-version: 6.2
// The NPRPC documentation site: pages rendered in-process by an NPRPC page
// handler from the api.json that `just docs-api` produces.
import PackageDescription

let package = Package(
  name: "docs-site",
  platforms: [.macOS(.v13)],
  dependencies: [
    .package(path: "../../nprpc_swift"),
  ],
  targets: [
    // api.json -> an index of pages, links and search. No NPRPC, no
    // templates, so it is testable on its own.
    .target(name: "DocsModel", path: "Sources/DocsModel"),
    .target(
      name: "DocsWeb",
      dependencies: [
        "DocsModel",
        .product(name: "NPRPC", package: "nprpc_swift"),
        .product(name: "NPRPCWeb", package: "nprpc_swift"),
      ],
      path: "Sources/DocsWeb",
      swiftSettings: [.interoperabilityMode(.Cxx)]),
    .executableTarget(
      name: "docs-server",
      dependencies: ["DocsWeb", .product(name: "NPRPC", package: "nprpc_swift")],
      path: "Sources/docs-server",
      swiftSettings: [.interoperabilityMode(.Cxx)]),
    .testTarget(
      name: "DocsModelTests",
      dependencies: ["DocsModel"],
      path: "Tests/DocsModelTests"),
  ]
)

// swift-tools-version: 6.2

import PackageDescription

let package = Package(
  name: "LiveBlogServer",
  platforms: [
    .macOS(.v13)
  ],
  dependencies: [
    // NPRPC Swift bindings — pre-installed in the Docker image
    .package(path: "/opt/nprpc_swift"),
  ],
  targets: [
    .target(
      name: "LiveBlogAPI",
      dependencies: [
        .product(name: "NPRPC", package: "nprpc_swift")
      ],
      path: "Sources/LiveBlogAPI",
      swiftSettings: [
        .interoperabilityMode(.Cxx)
      ]
    ),
    .executableTarget(
      name: "LiveBlogServer",
      dependencies: [
        "LiveBlogAPI",
        .product(name: "NPRPC", package: "nprpc_swift")
      ],
      path: "Sources/LiveBlogServer",
      swiftSettings: [
        .interoperabilityMode(.Cxx)
      ]
    ),
  ]
)

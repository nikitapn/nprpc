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
    // swift-mustache — mounted from third_party/ by scripts/build-swift-server.sh
    .package(path: "/opt/swift-mustache"),
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
    // Server-rendered pages: Mustache templates over the generated types.
    .target(
      name: "LiveBlogWeb",
      dependencies: [
        "LiveBlogAPI",
        .product(name: "NPRPC", package: "nprpc_swift"),
        .product(name: "Mustache", package: "swift-mustache")
      ],
      path: "Sources/LiveBlogWeb",
      swiftSettings: [
        .interoperabilityMode(.Cxx)
      ]
    ),
    .executableTarget(
      name: "LiveBlogServer",
      dependencies: [
        "LiveBlogAPI",
        "LiveBlogWeb",
        .product(name: "NPRPC", package: "nprpc_swift")
      ],
      path: "Sources/LiveBlogServer",
      swiftSettings: [
        .interoperabilityMode(.Cxx)
      ]
    ),
  ]
)

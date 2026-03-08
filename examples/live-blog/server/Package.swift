// swift-tools-version: 6.2

import PackageDescription

let package = Package(
	name: "LiveBlogServer",
	platforms: [
		.macOS(.v13)
	],
	dependencies: [
		.package(path: "../../../nprpc_swift")
	],
	targets: [
		.executableTarget(
			name: "LiveBlogServer",
			dependencies: [
				.product(name: "NPRPC", package: "nprpc_swift")
			],
			swiftSettings: [
				.interoperabilityMode(.Cxx)
			]
		)
	]
)
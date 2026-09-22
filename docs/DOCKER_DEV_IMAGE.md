# NPRPC Development Docker Image

A development image with NPRPC's C++ library, Swift package and tools
pre-built, so a Swift or C++ project can build against NPRPC without building
it first. Use it for building; see the multi-stage example below for a small
runtime image.

## What's Included

- **Swift 6.3.0** toolchain (the image is based on `swift:6.3.0`)
- **NPRPC C++ library** (`libnprpc.so`), headers and CMake config
- **npidl** (IDL compiler) and **npnameserver**
- **Swift package** `nprpc_swift`, pre-built: the `NPRPC` library, and
  `NPRPCWeb` for server-rendered pages
- **swift-mustache**, which `NPRPCWeb` depends on
- **Boost 1.89.0**
- CMake, Ninja, pkg-config, Node.js, liburing, and ffmpeg/gpac for media
  examples

## Quick Start

### Build the Image

```bash
just build-dev-image
```

This creates `nprpc-dev:latest` with everything under `/opt/`:
- `/opt/nprpc`: C++ library, headers, tools, CMake config
- `/opt/nprpc_swift`: Swift package (source + compiled)
- `/opt/swift-mustache`: template engine used by `NPRPCWeb`
- `/opt/boost`: Boost libraries

The image runs as the `ubuntu` user (UID/GID 1000). Pass
`--user $(id -u):$(id -g)` so files written to a mounted directory belong to
you.

### Use in Your Project

#### 1. As Base Image (Dockerfile)

```dockerfile
FROM nprpc-dev:latest

WORKDIR /app
COPY Package.swift .
COPY Sources ./Sources

# Swift projects can reference NPRPC package
RUN swift build -c release
```

#### 2. Mount Project Directory

```bash
# Build your Swift project
docker run --rm --user $(id -u):$(id -g) \
  -v $(pwd):/project \
  -w /project \
  nprpc-dev:latest \
  swift build

# Run tests
docker run --rm \
  -v $(pwd):/project \
  -w /project \
  nprpc-dev:latest \
  swift test
```

#### 3. Interactive Development

```bash
docker run -it --rm \
  -v $(pwd):/project \
  -w /project \
  nprpc-dev:latest \
  /bin/bash

# Inside container:
npidl --version          # IDL compiler available
npnameserver &           # Start nameserver
swift build              # Build your project
```

## Using NPRPC in Your Swift Package

### Package.swift

```swift
// swift-tools-version: 6.0
import PackageDescription

let package = Package(
    name: "MyRpcService",
    dependencies: [
        // Reference installed NPRPC Swift package
        .package(path: "/opt/nprpc_swift")
    ],
    targets: [
        .executableTarget(
            name: "MyRpcService",
            dependencies: [
                .product(name: "NPRPC", package: "nprpc_swift"),
                // Only if you render pages with Mustache templates:
                .product(name: "NPRPCWeb", package: "nprpc_swift"),
            ],
            swiftSettings: [.interoperabilityMode(.Cxx)]
        )
    ]
)
```

Targets that import `NPRPC` need C++ interoperability enabled, as above.

## Using NPRPC in CMake Projects

The image includes CMake config files for easy integration:

```cmake
cmake_minimum_required(VERSION 3.15)
project(my_rpc_client)

# Include NPRPC toolchain hints
include(/opt/nprpc-toolchain.cmake)

# Find NPRPC
find_package(nprpc REQUIRED)

add_executable(client client.cpp)
target_link_libraries(client PRIVATE nprpc::nprpc)
```

Build:
```bash
docker run --rm -v $(pwd):/project -w /project nprpc-dev:latest \
  bash -c "cmake -B build && cmake --build build"
```

## Environment Variables

Pre-configured in the image:
- `BOOST_ROOT=/opt/boost`
- `NPRPC_ROOT=/opt/nprpc`
- `NPRPC_SWIFT_ROOT=/opt/nprpc_swift`
- `PATH` includes `/opt/nprpc/bin` (npidl, npnameserver)
- `LD_LIBRARY_PATH` includes `/opt/nprpc/lib` and `/opt/boost/lib`
- `PKG_CONFIG_PATH` includes `/opt/nprpc/lib/pkgconfig`

## Customization

### Build with Different Tag

```bash
just build-dev-image mycompany/nprpc 1.0.0
```

### Push to Registry

```bash
docker tag nprpc-dev:latest myregistry.com/nprpc-dev:latest
docker push myregistry.com/nprpc-dev:latest
```

## Example: Multi-Stage Production Build

```dockerfile
# Build stage - use NPRPC dev image
FROM nprpc-dev:latest AS builder

WORKDIR /app
COPY . .
RUN swift build -c release

# Runtime stage - minimal image
FROM swift:6.3.0-slim

# Copy only runtime dependencies
COPY --from=builder /opt/nprpc/lib/libnprpc.so* /usr/local/lib/
COPY --from=builder /opt/boost/lib/libboost_*.so* /usr/local/lib/
COPY --from=builder /app/.build/release/MyApp /usr/local/bin/

RUN ldconfig

CMD ["MyApp"]
```

## Troubleshooting

### Swift Can't Find NPRPC Package

Ensure your `Package.swift` references the correct path:
```swift
.package(path: "/opt/nprpc_swift")
```

### CMake Can't Find nprpc

Include the toolchain file:
```bash
cmake -DCMAKE_TOOLCHAIN_FILE=/opt/nprpc-toolchain.cmake -B build
```

### Library Not Found at Runtime

Libraries are in `LD_LIBRARY_PATH`, but if running executables outside the image, copy libs or mount them.

## Version Information

Run inside the container:
```bash
swift --version        # Swift 6.3.0
npidl --version        # NPRPC IDL compiler
cmake --version        # CMake
```

# Swift Runtime Package for NPRPC

This document explains how to use the Swift runtime package with libnprpc.so.

## Current Status

The Swift package is set up to link against the C++ `libnprpc.so` library. However, there are a few challenges:

### Challenge 1: Header Path Resolution

Swift's C++ interop requires headers to be found when importing the module. The cxxSettings in Package.swift don't always propagate to module imports.

**Solution**: Use environment variables or create a bridge configuration:

```bash
# Set include paths for Swift build
export CPATH=/home/nikita/projects/nprpc/include:/home/nikita/projects/nprpc/.build_relwith_debinfo/include

# Build (no need for -stdlib=libc++)
cd nprpc_swift
swift build
```

### Challenge 2: Library Linking

The package needs to find `libnprpc.so` at both compile and runtime.

**Solution**: Set library paths:

```bash
# For linking
export LIBRARY_PATH=/home/nikita/projects/nprpc/.build_relwith_debinfo

# For runtime
export LD_LIBRARY_PATH=/home/nikita/projects/nprpc/.build_relwith_debinfo

# Or install the library system-wide
sudo cmake --install /home/nikita/projects/nprpc/.build_relwith_debinfo
```

### Challenge 3: C++ Standard Library Compatibility

**Toolchain Versions**:
- Swift 6.2.3 uses embedded **Clang 17.0.0** (from swiftlang/llvm-project)
- System Clang is 21.1.6 (separate)
- System libc++ headers expect Clang 19+
- NPRPC is built with GCC 15.2.1 + libstdc++

**The Problem**: When you use `-stdlib=libc++` with Swift:
1. Swift's Clang 17 tries to use system libc++ headers (designed for Clang 21)
2. libc++ headers detect Clang 17 and warn: "Libc++ only supports Clang 19 and later"
3. Mixing libc++ (Swift) with libstdc++ (NPRPC) causes ABI incompatibilities

**Current Solution**: Don't use `-stdlib=libc++`. Swift's Clang 17 works fine with GCC's libstdc++ (the default).

**Future Solution**: Build everything (NPRPC + Boost) with Clang 19+ and libc++ for consistent ABIs.

## Recommended Usage Pattern

For now, the best approach is to keep the bridge minimal and use Swift's built-in types where possible:

1. **Generated IDL Code**: Works great - pure Swift with marshal/unmarshal functions
2. **RPC Runtime**: Use C++ directly or wait for full Clang-based build

## Building with Environment Variables

```bash
#!/bin/bash
# build_swift.sh

# Set paths relative to script location
NPRPC_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$NPRPC_ROOT/.build_relwith_debinfo"

export CPATH="$NPRPC_ROOT/include:$BUILD_DIR/include"
export LIBRARY_PATH="$BUILD_DIR"
export LD_LIBRARY_PATH="$BUILD_DIR"

cd "$NPRPC_ROOT/nprpc_swift"
swift build -Xcc -stdlib=libc++ -Xlinker -lc++ -Xlinker -lc++abi "$@"
```

## Testing Generated Code

The generated code from `npidl --swift` doesn't require libnprpc.so and works perfectly:

```bash
cd nprpc_swift
swift test -Xcc -stdlib=libc++ -Xlinker -lc++ -Xlinker -lc++abi --filter GeneratedCodeTests
```

This tests marshal/unmarshal, servants, and all IDL-generated Swift code.

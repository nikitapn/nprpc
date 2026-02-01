# NPRPC Swift Bindings

Swift bindings for NPRPC using Swift 6.2+ C++ interoperability.

## Requirements

- Swift 6.2 or later (for C++ interop)
- NPRPC C++ library built (`libnprpc.so`)
- Linux (tested) or macOS

## Building

First, ensure NPRPC is built:

```bash
cd ..
cmake -S . -B .build_release -DCMAKE_BUILD_TYPE=Release
cmake --build .build_release -j$(nproc)
```

Then build the Swift package:

```bash
cd nprpc_swift
swift build
```

## Running the Proof of Concept

```bash
swift run nprpc-poc
```

Expected output:
```
============================================================
NPRPC Swift Proof of Concept
============================================================

Test 1: Basic C++ function call
  40 + 2 = 42
  ✓ PASSED

Test 2: String passing to/from C++
  Greeting: Hello from NPRPC C++, Swift!
  ✓ PASSED

...
```

## Running Tests

```bash
swift test
```

## Project Structure

```
nprpc_swift/
├── Package.swift              # Swift Package Manager manifest
├── Sources/
│   ├── CNprpc/                # C++ bridge module
│   │   ├── include/
│   │   │   ├── module.modulemap   # Exposes C++ to Swift
│   │   │   └── nprpc_bridge.hpp   # Bridge header
│   │   └── nprpc_bridge.cpp       # Bridge implementation
│   ├── NPRPC/                 # Main Swift module
│   │   └── NPRPC.swift        # Swift wrappers
│   └── NPRPCPoC/              # Proof of concept executable
│       └── main.swift
└── Tests/
    └── NPRPCTests/
        └── NPRPCTests.swift
```

## Architecture

```
┌─────────────────────────────────────────┐
│           Your Swift Code               │
│  (async/await, Swift types, protocols)  │
├─────────────────────────────────────────┤
│            NPRPC Module                 │
│  (Swift wrappers: Rpc, EndPoint, etc)   │
├─────────────────────────────────────────┤
│            CNprpc Module                │
│  (C++ bridge: nprpc_bridge.hpp/cpp)     │
├─────────────────────────────────────────┤
│          libnprpc.so (C++)              │
│  (All transports, serialization, POA)   │
└─────────────────────────────────────────┘
```

## Phase 0 Status: Proof of Concept

- [x] Package.swift with C++ interop settings
- [x] Module map exposing C++ headers
- [x] Basic C++ bridge (add, greet, array)
- [x] Swift wrappers (EndPoint, RpcConfiguration, Rpc)
- [x] Proof of concept executable
- [x] Unit tests

## Next Steps (Phase 1)

1. Integrate with actual `nprpc::Rpc` (currently stubbed)
2. Add `ObjectPtr<T>` wrapper
3. Add `Poa` wrapper for servant activation
4. Bridge exceptions to Swift errors
5. Add async/await wrappers

## Notes on C++ Interop

Swift 5.9+ has native C++ interoperability. Key points:

- `std::string` ↔ `String` conversion works via `std.string()`
- `std::vector<T>` accessible via subscript and `.size()`
- C++ classes can be used directly in Swift
- `std::optional<T>` exposed as optional in Swift
- Must use `.interoperabilityMode(.Cxx)` in Package.swift

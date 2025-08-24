# NPRPC JavaScript Testing Framework

This directory contains the JavaScript/TypeScript testing framework for the NPRPC (Network Procedure Call) system. The test setup mirrors the C++ test cases and is designed to verify the JavaScript client implementation against the C++ server.

## Current Status

✅ **Complete:**
- Basic test framework setup (Mocha + TypeScript + Chai)
- TypeScript compilation and build system
- CMake integration for automated builds
- Generated TypeScript interfaces from IDL files

🟡 **In Progress:**
- NPRPC library Node.js compatibility (browser vs Node.js environment)
- C++ server coordination and lifecycle management

❌ **Pending:**
- Complete NPRPC test case implementation
- Integration with C++ test server
- WebSocket connection testing
- Large message handling tests

## Architecture

The JavaScript testing framework follows the same structure as the C++ tests:

```
test/js/
├── src/
│   ├── gen/               # Generated TypeScript interfaces from IDL
│   │   └── test.ts        # Auto-generated from test.npidl
│   └── my_modules/        # Local module dependencies
│       └── nprpc/         # Local NPRPC library copy
├── test/
│   ├── basic.test.ts      # Basic framework validation tests
│   ├── server-manager.ts  # C++ server lifecycle management
│   └── nprpc.test.ts      # NPRPC integration tests (pending)
├── package.json           # Node.js dependencies and scripts
├── tsconfig.json          # TypeScript configuration
├── .mocharc.json          # Mocha test configuration
└── run-tests.sh           # Test runner script
```

## Test Categories

### 1. Basic Framework Tests (`basic.test.ts`)
- ✅ Test framework validation
- ✅ TypeScript compilation verification
- ✅ Async/await support

### 2. NPRPC Integration Tests (`nprpc.test.ts` - Pending)
Based on C++ test cases in `../src/main.cpp`:

#### TestBasic Interface
- `ReturnBoolean()` - Simple boolean return test
- `In(uint32, boolean, vector<uint8>)` - Input parameter validation
- `Out(uint32&, boolean&, vector<uint8>&)` - Output parameter handling

#### TestOptional Interface  
- `InEmpty(optional<uint32>)` - Empty optional handling
- `In(optional<uint32>, optional<AAA>)` - Optional with data
- `OutEmpty(optional<uint32>&)` - Empty optional output
- `Out(optional<uint32>&)` - Optional output with data

#### TestNested Interface
- `Out(optional<BBB>&)` - Complex nested structure handling
- Tests arrays of structures with nested optionals

#### Large Message Tests
- 3MB message transmission/reception
- Tests async_write fix for large WebSocket messages

#### Error Handling Tests
- `TestBadInput` interface for validation
- Exception handling (ExceptionBadInput, etc.)

## Technical Challenges

### 1. Browser vs Node.js Compatibility

**Issue:** The NPRPC JavaScript library is compiled for browser use and references `self`, which doesn't exist in Node.js.

**Current Error:**
```
ReferenceError: self is not defined
    at Object.<anonymous> (/path/to/nprpc_js/dist/index.js:1:224)
```

**Potential Solutions:**
1. **Node.js-specific build:** Modify webpack config to generate Node.js compatible version
2. **Browser emulation:** Use jsdom to provide browser globals in Node.js
3. **Puppeteer/Playwright:** Run tests in actual browser environment
4. **Conditional builds:** Support both UMD (browser) and CommonJS (Node.js) builds

### 2. C++ Server Coordination

**Requirements:**
- Auto-start/stop nameserver process
- Launch C++ test server with appropriate test objects  
- Synchronize test execution between JS client and C++ server
- Handle different connection types (TCP, WebSocket, SSL WebSocket)

**Current Implementation:** Basic process management in `server-manager.ts`

### 3. Generated Code Compatibility

**Status:** ✅ Resolved
- Generated TypeScript interfaces from IDL work correctly
- Build system properly copies generated files
- TypeScript compilation succeeds

## Running Tests

### Prerequisites
```bash
# Install Node.js dependencies
npm install

# Ensure NPRPC JavaScript library is built
cd ../../nprpc_js && npm run build-prd
```

### Test Commands
```bash
# Run all tests
npm test

# Run basic framework tests only
npm run test:basic

# Build TypeScript code
npm run build

# Clean build artifacts
npm run clean
```

### Manual Test Runner
```bash
# Use the test runner script
./run-tests.sh
```

## Development Workflow

1. **Update IDL files** (`../idl/test.npidl`)
2. **Regenerate interfaces:**
   ```bash
   cd /path/to/npsystem
   cmake --build build/linux --target nprpc_test_stub_gen
   ```
3. **Copy generated files:**
   ```bash
   cp build/linux/nprpc_test_stub/src/gen/js/test.ts nprpc/test/js/src/gen/
   ```
4. **Build and test:**
   ```bash
   cd nprpc/test/js
   npm run build
   npm test
   ```

## CMake Integration

The JavaScript tests are integrated into the main CMake build system:

```cmake
# Build JavaScript tests
cmake --build build/linux --target nprpc_js_test
```

**Dependencies:**
- `nprpc_js` - Main NPRPC JavaScript library
- `nprpc_test_stub_gen` - Generated TypeScript interfaces

## Next Steps

1. **Resolve Node.js Compatibility**
   - Create Node.js-compatible build of NPRPC library
   - Or implement browser-based testing with Puppeteer

2. **Implement C++ Server Integration**
   - Modify C++ test to support server-only mode
   - Implement proper process lifecycle management
   - Add connection type testing (TCP, WebSocket, SSL)

3. **Port C++ Test Cases**
   - Implement all test interfaces in JavaScript
   - Add request ID-based protocol testing
   - Verify large message handling

4. **Add CI/CD Integration**
   - Automate test execution in build pipeline
   - Add performance benchmarking
   - Cross-platform testing

## Related Files

- **C++ Tests:** `../src/main.cpp` - Reference implementation
- **IDL Definition:** `../idl/test.npidl` - Interface definitions  
- **NPRPC JS Library:** `../../nprpc_js/` - Main client library
- **Generated Interfaces:** `src/gen/test.ts` - Auto-generated from IDL
- **CMake Config:** `CMakeLists.txt` - Build integration

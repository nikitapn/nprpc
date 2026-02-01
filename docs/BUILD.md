# Building and Installing NPRPC

This document describes how to build and install NPRPC as a standalone library.

## Prerequisites

- **C++ Compiler**: GCC 10+, Clang 12+, or MSVC 2019+ with C++20 support
- **CMake**: 3.15 or higher
- **OpenSSL**: For secure transport support
- **Boost**: (Optional) For program_options in npidl tool
- **GTest**: (Optional) For building tests
- **Node.js**: (Optional) For TypeScript/JavaScript bindings

### Ubuntu/Debian

```bash
sudo apt update
sudo apt install -y build-essential cmake libssl-dev libboost-dev libgtest-dev
```

### macOS

```bash
brew install cmake openssl boost googletest
```

### Windows

Install dependencies using vcpkg:

```bash
vcpkg install openssl boost-program-options gtest
```

## Building

### Basic Build

```bash
# Clone the repository
git clone https://github.com/yourusername/nprpc.git
cd nprpc

# Create build directory
mkdir build && cd build

# Configure
cmake ..

# Build
cmake --build .

# Install (optional)
sudo cmake --install .
```

### Build Options

NPRPC provides several CMake options to customize the build:

- `NPRPC_BUILD_TOOLS` (default: ON when standalone) - Build npidl tool and nameserver
- `NPRPC_BUILD_TESTS` (default: ON when standalone) - Build test suite
- `NPRPC_BUILD_JS` (default: ON when standalone) - Build JavaScript/TypeScript bindings
- `NPRPC_INSTALL` (default: ON when standalone) - Generate install targets
- `BUILD_SHARED_LIBS` (default: ON) - Build shared libraries instead of static
- `NPRPC_ENABLE_QUIC` (default: OFF) - Enable QUIC transport (builds MsQuic from submodule)
- `NPRPC_ENABLE_HTTP3` (default: OFF) - Enable HTTP/3 server support (requires QUIC, builds msh3)

#### Examples

Build only the library (minimal build):

```bash
cmake -DNPRPC_BUILD_TOOLS=OFF -DNPRPC_BUILD_TESTS=OFF -DNPRPC_BUILD_JS=OFF ..
cmake --build .
```

Build with QUIC and HTTP/3 support:

```bash
cmake -DNPRPC_ENABLE_QUIC=ON -DNPRPC_ENABLE_HTTP3=ON ..
cmake --build .

Build static library:

```bash
cmake -DBUILD_SHARED_LIBS=OFF ..
cmake --build .
```

Build with custom install prefix:

```bash
cmake -DCMAKE_INSTALL_PREFIX=/usr/local ..
cmake --build .
sudo cmake --install .
```

### Running Tests

If you built with tests enabled:

```bash
# Run all tests
ctest --output-on-failure

# Or use custom targets
cmake --build . --target run_nprpc_tests
```

## Using NPRPC in Your Project

### With CMake's find_package

After installing NPRPC, you can use it in your CMake project:

```cmake
cmake_minimum_required(VERSION 3.15)
project(MyProject)

# Find NPRPC package
find_package(nprpc REQUIRED)

# Create your executable
add_executable(myapp main.cpp)

# Link against nprpc
target_link_libraries(myapp PRIVATE nprpc::nprpc)
```

### As a Subdirectory

You can also include NPRPC directly in your project:

```cmake
# Add nprpc as subdirectory
add_subdirectory(external/nprpc)

# Create your executable
add_executable(myapp main.cpp)

# Link against nprpc
target_link_libraries(myapp PRIVATE nprpc::nprpc)
```

### Using npidl to Generate Code

If you installed NPRPC with tools, you can use the `npidl_generate_idl_files` function:

```cmake
find_package(nprpc REQUIRED)

# Generate code from IDL files
set(IDL_FILES
  ${CMAKE_CURRENT_SOURCE_DIR}/idl/myservice.npidl
)

npidl_generate_idl_files("${IDL_FILES}" myservice_stub)

# Create executable with generated code
add_executable(myapp
  main.cpp
  ${myservice_stub_GENERATED_SOURCES}
)

target_include_directories(myapp PRIVATE
  ${myservice_stub_INCLUDE_DIR}
)

target_link_libraries(myapp PRIVATE nprpc::nprpc)
```

## Cross-Compilation

For cross-compilation, specify the toolchain file:

```bash
cmake -DCMAKE_TOOLCHAIN_FILE=/path/to/toolchain.cmake ..
cmake --build .
```

## Development Build

For development with debugging symbols:

```bash
cmake -DCMAKE_BUILD_TYPE=Debug ..
cmake --build .
```

For release with optimizations:

```bash
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build .
```

## Troubleshooting

### OpenSSL not found

If CMake cannot find OpenSSL, specify the path manually:

```bash
cmake -DOPENSSL_ROOT_DIR=/path/to/openssl ..
```

### Boost not found

Specify Boost root directory:

```bash
cmake -DBOOST_ROOT=/path/to/boost ..
```

### GTest not found

If you want to build tests but GTest is not found:

```bash
# Option 1: Disable tests
cmake -DNPRPC_BUILD_TESTS=OFF ..

# Option 2: Specify GTest location
cmake -DGTest_DIR=/path/to/gtest/lib/cmake/GTest ..
```

## Installation Paths

By default, NPRPC installs to:

- **Headers**: `${CMAKE_INSTALL_PREFIX}/include/nprpc/`
- **Libraries**: `${CMAKE_INSTALL_PREFIX}/lib/`
- **Executables**: `${CMAKE_INSTALL_PREFIX}/bin/` (npidl, npnameserver)
- **CMake config**: `${CMAKE_INSTALL_PREFIX}/lib/cmake/nprpc/`

## Uninstalling

CMake doesn't provide an uninstall target by default. To uninstall, you can use:

```bash
# From build directory
cat install_manifest.txt | sudo xargs rm
```

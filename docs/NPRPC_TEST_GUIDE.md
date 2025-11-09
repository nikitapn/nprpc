# NPRPC Google Test Integration - Usage Guide

## Overview
The NPRPC library now has proper Google Test integration with CMake CTest, providing multiple ways to run tests automatically or manually.

## Available Test Targets

### Build Targets
- `nprpc_test` - Builds the test executable
- `nprpc_test_gen` - Generates IDL stub files for tests

### Test Execution Targets
- `run_nprpc_tests` - Runs all tests with summary output
- `run_nprpc_tests_verbose` - Runs all tests with verbose output
- `run_large_message_test` - Runs only the large message test (for async_write fix verification)

## Running Tests

### 1. Manual Test Execution
```bash
# Run all tests directly
cd /home/nikita/projects/npsystem
./build/linux/bin/nprpc_test

# Run specific test
./build/linux/bin/nprpc_test --gtest_filter=NprpcTest.TestLargeMessage

# List all available tests
./build/linux/bin/nprpc_test --gtest_list_tests
```

### 2. Using CMake/CTest
```bash
cd /home/nikita/projects/npsystem/build/linux

# Run all tests using CTest
ctest

# Run tests with verbose output
ctest --verbose

# Run specific test by name
ctest -R "TestLargeMessage"

# Run from specific test directory
cd nprpc/test && ctest --verbose
```

### 3. Using Custom Make Targets
```bash
cd /home/nikita/projects/npsystem/build/linux

# Build and run all tests
make run_nprpc_tests

# Build and run tests with verbose output
make run_nprpc_tests_verbose

# Build and run only large message test
make run_large_message_test
```

### 4. Automatic Test Execution After Build
Tests can be integrated into your CI/CD pipeline or run automatically:

```bash
# Build everything and then run tests
cd /home/nikita/projects/npsystem
./build.sh && cd build/linux && make run_nprpc_tests
```

## Test Configuration

### Test Properties
- **Timeout**: 120 seconds (for large message tests)
- **Working Directory**: `/home/nikita/projects/npsystem` (project root)
- **Test Discovery**: Automatic via `gtest_discover_tests()`

### Individual Tests Available
1. `NprpcTest.TestBasic` - Basic functionality test
2. `NprpcTest.TestOptional` - Optional types test  
3. `NprpcTest.TestNested` - Nested structures test (6.7MB data)
4. `NprpcTest.TestLargeMessage` - Large message test (3MB data, verifies async_write fix)
5. `nprpc_all_tests` - Runs all tests together

## Test Features

### Large Message Testing
- Tests verify the async_write fix for messages >2.6MB
- `TestLargeMessage`: Tests 3MB data transmission/reception
- `TestNested`: Tests 6.7MB nested structure data
- Pattern verification ensures data integrity
- Performance monitoring (typical: ~85-600ms for large transfers)

### Nameserver Management
- Automatic nameserver process spawning/cleanup
- Process isolation for each test run
- Proper cleanup on test completion

### Environment Setup
- Automatic RPC initialization
- POA (Portable Object Adapter) setup
- Thread pool management
- Debug level configuration

## CMakeLists.txt Configuration

The test configuration in `/home/nikita/projects/npsystem/nprpc/test/CMakeLists.txt` includes:

```cmake
# Test discovery and individual test registration
gtest_discover_tests(nprpc_test
  PROPERTIES
    TIMEOUT 120
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  DISCOVERY_TIMEOUT 30
  DISCOVERY_MODE PRE_TEST
)

# Summary test for all tests together
add_test(
  NAME nprpc_all_tests
  COMMAND nprpc_test
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Custom convenience targets
add_custom_target(run_nprpc_tests ...)
add_custom_target(run_nprpc_tests_verbose ...)
add_custom_target(run_large_message_test ...)
```

## Best Practices

1. **For Development**: Use `make run_nprpc_tests` for quick feedback
2. **For Debugging**: Use `make run_nprpc_tests_verbose` to see detailed output
3. **For Large Message Testing**: Use `make run_large_message_test` to verify async_write fix
4. **For CI/CD**: Use `ctest --output-on-failure` for automated testing
5. **For Manual Testing**: Run individual tests with `--gtest_filter=TestName`

## Troubleshooting

### Common Issues
- **"No tests found"**: Make sure you're in the correct directory (`build/linux/nprpc/test` or `build/linux`)
- **Nameserver errors**: Check that port 22222 is available
- **Timeout errors**: Large tests may need more time; timeout is set to 120 seconds
- **Build errors**: Ensure all dependencies are built first with `./build.sh`

### Verification Commands
```bash
# Verify test executable exists
ls -la /home/nikita/projects/npsystem/build/linux/bin/nprpc_test

# Check test discovery
cd /home/nikita/projects/npsystem/build/linux/nprpc/test
ctest --show-only

# Verify nameserver binary
ls -la /home/nikita/projects/npsystem/build/linux/bin/npnameserver
```

This integration provides a robust testing framework that automatically validates the NPRPC library functionality, including the critical large message async_write fix.

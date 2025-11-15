# NPIDL tests

# Find GTest
find_package(GTest REQUIRED)

# Main test executable for NPIDL tests
add_executable(npidl_test
  src/main.cpp
)

# Link against the npidl_core library
target_link_libraries(npidl_test 
  PRIVATE
    npidl_core
    GTest::gtest_main
)

# Enable testing
include(GoogleTest)

# Discover individual tests
gtest_discover_tests(npidl_test
  PROPERTIES
    TIMEOUT 120
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../..
  DISCOVERY_TIMEOUT 30
  DISCOVERY_MODE PRE_TEST
)

# Helper custom target to run tests manually
add_custom_target(run_npidl_tests
  COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
  DEPENDS npidl_test
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  COMMENT "Running npidl tests..."
)

# Helper custom target to run tests with verbose output
add_custom_target(run_npidl_tests_verbose
  COMMAND ${CMAKE_CTEST_COMMAND} --verbose
  DEPENDS npidl_test
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  COMMENT "Running npidl tests with verbose output..."
)

# LSP Integration Tests (Python scripts)
# These test the actual LSP server executable end-to-end
find_package(Python3 COMPONENTS Interpreter)

if(Python3_FOUND)
  # Add the LSP integration test suite
  add_test(
    NAME npidl_lsp_integration_tests
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/integration_tests/run_lsp_tests.py
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/integration_tests
  )
  
  # Set environment variables for the test
  set_tests_properties(npidl_lsp_integration_tests PROPERTIES
    ENVIRONMENT "NPIDL_BIN=${CMAKE_BINARY_DIR}/bin/npidl"
    TIMEOUT 60
    LABELS "integration;lsp"
  )
  
  # Make sure npidl is built before running LSP tests
  add_dependencies(npidl_test npidl)
  
  message(STATUS "LSP integration tests enabled (Python ${Python3_VERSION})")
else()
  message(WARNING "Python3 not found - LSP integration tests will be skipped")
endif()

# Helper target to run only LSP integration tests
if(Python3_FOUND)
  add_custom_target(run_lsp_tests
    COMMAND ${CMAKE_CTEST_COMMAND} -R npidl_lsp_integration_tests --output-on-failure
    DEPENDS npidl
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running LSP integration tests..."
  )
endif()
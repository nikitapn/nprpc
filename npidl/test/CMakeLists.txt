# Main test executable for NPIDL tests
add_executable(npidl_test
  src/main.cpp
)

# Link against the npidl_core library
# Now any changes to npidl sources will trigger npidl_test rebuild!
target_link_libraries(npidl_test PRIVATE
  npidl_core
  GTest::gtest_main
)

# Enable testing and include GoogleTest
enable_testing()
include(GoogleTest)

# Optionally, also discover individual tests (requires the executable to be built first)
# This will create individual test entries for each TEST_F in your code
gtest_discover_tests(npidl_test
  PROPERTIES
    TIMEOUT 120
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  DISCOVERY_TIMEOUT 30
  DISCOVERY_MODE PRE_TEST
)

# Run tests automatically after building the main test executable
add_custom_command(
  TARGET npidl_test
  POST_BUILD
  COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure --test-dir ${CMAKE_CURRENT_BINARY_DIR}
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Automatically running nprpc tests after build..."
)

# Helper custom target to run tests manually
add_custom_target(run_npidl_tests
  COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure --test-dir ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS npidl_test
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Running nprpc tests..."
)

# Helper custom target to run tests with verbose output
add_custom_target(run_npidl_tests_verbose
  COMMAND ${CMAKE_CTEST_COMMAND} --verbose --test-dir ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS npidl_test
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Running nprpc tests with verbose output..."
)
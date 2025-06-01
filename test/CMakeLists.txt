include(${CMAKE_SOURCE_DIR}/nprpc/cmake/npidl.cmake)

set(IDL_FILES 
  ${CMAKE_SOURCE_DIR}/nprpc/test/idl/test.npidl
)

npidl_generate_idl_files("${IDL_FILES}" nprpc_test_stub)

add_custom_target(
  nprpc_test_gen
  DEPENDS ${nprpc_test_stub_GENERATED_SOURCES} ${nprpc_test_stub_GENERATED_HEADERS}
  COMMENT "Generating all nprpc_test_stub files"
)

add_executable(nprpc_test
  src/main.cpp
  ${nprpc_test_stub_GENERATED_SOURCES}
)

add_dependencies(nprpc_test nprpc_test_gen npnameserver)

target_include_directories(nprpc_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/nprpc/include
    ${CMAKE_SOURCE_DIR}/nplib/include
    ${nprpc_test_stub_INCLUDE_DIR}  # For generated headers
)

target_link_libraries(nprpc_test
  GTest::gtest_main
  nprpc
)

# Enable testing and include GoogleTest
enable_testing()
include(GoogleTest)

# Add a simple test that runs the executable
add_test(
  NAME nprpc_all_tests
  COMMAND nprpc_test
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Set test properties
set_tests_properties(nprpc_all_tests PROPERTIES
  TIMEOUT 120  # 120 seconds timeout for large message tests
)

# Optionally, also discover individual tests (requires the executable to be built first)
# This will create individual test entries for each TEST_F in your code
gtest_discover_tests(nprpc_test
  PROPERTIES
    TIMEOUT 120
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  DISCOVERY_TIMEOUT 30
  DISCOVERY_MODE PRE_TEST
)

# Custom target to run tests automatically after build
add_custom_target(run_nprpc_tests
  COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure --test-dir ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS nprpc_test
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Running nprpc tests..."
)

# Custom target to run tests with verbose output
add_custom_target(run_nprpc_tests_verbose
  COMMAND ${CMAKE_CTEST_COMMAND} --verbose --test-dir ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS nprpc_test
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Running nprpc tests with verbose output..."
)

# Custom target to run only the large message test
add_custom_target(run_large_message_test
  COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure --test-dir ${CMAKE_CURRENT_BINARY_DIR} -R "TestLargeMessage"
  DEPENDS nprpc_test
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Running large message test..."
)


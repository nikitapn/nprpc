# NPRPC Tests

# Find GTest
find_package(GTest REQUIRED)

# Generate test IDL files
include(../cmake/npidl.cmake)

set(IDL_FILES 
  ${CMAKE_CURRENT_SOURCE_DIR}/idl/nprpc_test.npidl
)

npidl_generate_idl_files("${IDL_FILES}" nprpc_test_stub)

add_custom_target(
  nprpc_test_gen
  DEPENDS 
    ${nprpc_test_stub_GENERATED_SOURCES}
    ${nprpc_test_stub_GENERATED_HEADERS}
    ${nprpc_test_stub_GENERATED_TS}
  COMMENT
    "Generating all nprpc_test_stub files"
)

# Main test executable for C++ tests
add_executable(nprpc_test
  src/main.cpp
  src/basic.cpp
  src/streams.cpp
  src/concurrent.cpp
  ${nprpc_test_stub_GENERATED_SOURCES}
)

add_dependencies(nprpc_test nprpc_test_gen)
if(TARGET npnameserver)
  add_dependencies(nprpc_test npnameserver)
endif()

target_include_directories(nprpc_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${nprpc_test_stub_INCLUDE_DIR}
)

target_link_libraries(nprpc_test
  PRIVATE
    GTest::gtest_main
    nprpc::nprpc
    OpenSSL::SSL
    OpenSSL::Crypto
)

target_compile_definitions(nprpc_test
  PRIVATE
    NPRPC_USE_GTEST
    NPRPC_BUILD_DIR="${CMAKE_BINARY_DIR}"
    $<$<BOOL:${NPRPC_ENABLE_QUIC}>:NPRPC_HAS_QUIC>
)

# Server for JavaScript tests
add_executable(nprpc_server_test
  src/server.cpp
  ${nprpc_test_stub_GENERATED_SOURCES}
)

add_dependencies(nprpc_server_test nprpc_test_gen)

target_include_directories(nprpc_server_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${nprpc_test_stub_INCLUDE_DIR}
)

target_link_libraries(nprpc_server_test
  PRIVATE
    nprpc::nprpc
    OpenSSL::SSL
    OpenSSL::Crypto
)

# Add JavaScript tests if enabled
if(NPRPC_BUILD_JS AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/js)
  add_subdirectory(js)
endif()

# Enable testing
include(GoogleTest)

# Add a simple test that runs the executable
add_test(
  NAME nprpc_all_tests
  COMMAND nprpc_test
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# Set test properties
set_tests_properties(nprpc_all_tests PROPERTIES
  TIMEOUT 120
)

# Discover individual tests
gtest_discover_tests(nprpc_test
  PROPERTIES
    TIMEOUT 120
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
  DISCOVERY_TIMEOUT 30
  DISCOVERY_MODE PRE_TEST
)

# Custom target to run tests manually
add_custom_target(run_nprpc_tests
  COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
  DEPENDS nprpc_test
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  COMMENT "Running nprpc tests..."
)

# Custom target to run tests with verbose output
add_custom_target(run_nprpc_tests_verbose
  COMMAND ${CMAKE_CTEST_COMMAND} --verbose
  DEPENDS nprpc_test
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  COMMENT "Running nprpc tests with verbose output..."
)

# Run tests automatically after building the main test executable
# add_custom_command(
#   TARGET nprpc_test
#   POST_BUILD
#   COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
#   WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
#   COMMENT "Automatically running nprpc C++ tests after build..."
# )

# Test executable for lock-free ring buffer
add_executable(test_lock_free_ring_buffer
  src/test_lock_free_ring_buffer.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/thread_identity.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/logging.cpp
)

target_include_directories(test_lock_free_ring_buffer
  PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

target_link_libraries(test_lock_free_ring_buffer
  PRIVATE
    Boost::headers
    GTest::gtest
    rt
    pthread
    ssl
    crypto
)

add_test(NAME LockFreeRingBuffer COMMAND test_lock_free_ring_buffer)

# Test executable for variant / one-of feature (pure type tests, no nameserver needed)
add_executable(test_variant
  src/test_variant.cpp
)

add_dependencies(test_variant nprpc_test_gen)

target_include_directories(test_variant
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${nprpc_test_stub_INCLUDE_DIR}
)

target_compile_definitions(test_variant
  PRIVATE
    NPRPC_USE_GTEST
)

target_link_libraries(test_variant
  PRIVATE
    nprpc::nprpc
    GTest::gtest_main
)

gtest_discover_tests(test_variant
  PROPERTIES
    TIMEOUT 30
  DISCOVERY_TIMEOUT 15
  DISCOVERY_MODE PRE_TEST
)

# Test executable for shared memory endpoint
add_executable(test_shared_memory_endpoint
  src/test_shared_memory_endpoint.cpp
)

target_include_directories(test_shared_memory_endpoint 
  PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_link_libraries(test_shared_memory_endpoint 
  PRIVATE 
    nprpc::nprpc 
    GTest::gtest
)

add_test(NAME SharedMemoryEndpoint COMMAND test_shared_memory_endpoint)

# Test executable for shared memory listener
add_executable(test_shared_memory_listener
  src/test_shared_memory_listener.cpp
)

target_include_directories(test_shared_memory_listener 
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_link_libraries(test_shared_memory_listener 
  PRIVATE
    nprpc::nprpc
    GTest::gtest
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
)

add_test(NAME SharedMemoryListener COMMAND test_shared_memory_listener)

# Test executable for HTTP utility helpers
add_executable(test_http_utils
  src/test_http_utils.cpp
  ../src/http/http_utils.cpp
)

target_include_directories(test_http_utils
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_SOURCE_DIR}/third_party/unordered_dense/include
)

target_link_libraries(test_http_utils
  PRIVATE
    nprpc::nprpc
    GTest::gtest
    OpenSSL::SSL
    OpenSSL::Crypto
)

add_test(NAME HttpUtils COMMAND test_http_utils)

# HTTP/3 Shell Script Tests (requires server to be started manually or via wrapper)
if(NPRPC_ENABLE_HTTP3)
  # Create a wrapper script that starts the server, runs tests, and stops it
  configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/test_http3_rpc.sh
    ${CMAKE_CURRENT_BINARY_DIR}/test_http3_rpc.sh
    COPYONLY
  )

  # Create a test wrapper script
  file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/run_http3_tests.sh
"#!/bin/bash
# Auto-generated wrapper to run HTTP/3 tests with server

SCRIPT_DIR=\"$(cd \"$(dirname \"\${BASH_SOURCE[0]}\")\" && pwd)\"
SERVER=\"\${SCRIPT_DIR}/nprpc_server_test\"
TEST_SCRIPT=\"\${SCRIPT_DIR}/test_http3_rpc.sh\"

  ensure_nprpc_bpf_capabilities() {
    local binary=\"\${1:?binary path is required}\"
    local current_caps=\"\"

    command -v getcap >/dev/null 2>&1 || {
      echo \"Required command not found: getcap\" >&2
      exit 1
    }
    command -v setcap >/dev/null 2>&1 || {
      echo \"Required command not found: setcap\" >&2
      exit 1
    }

    if [[ ! -x \"\$binary\" ]]; then
      echo \"nprpc_server_test not found or not executable: \$binary\" >&2
      exit 1
    fi

    current_caps=\"$(getcap \"\$binary\" 2>/dev/null || true)\"
    if [[ \"\$current_caps\" == *\"cap_net_admin\"* && \"\$current_caps\" == *\"cap_bpf\"* ]]; then
      return 0
    fi

    if [[ \${EUID:-$(id -u)} -eq 0 ]]; then
      setcap cap_net_admin,cap_bpf+ep \"\$binary\"
    else
      # sudo -v || {
      #   echo \"Failed to obtain sudo permissions; cannot grant BPF capabilities to nprpc_server_test\" >&2
      #   exit 1
      # }
      sudo setcap cap_net_admin,cap_bpf+ep \"\$binary\"
    fi
  }

cleanup() {
    # Kill server and nameserver processes
    kill \$SERVER_PID 2>/dev/null
    pkill -P \$SERVER_PID 2>/dev/null  # Kill child processes (nameserver)
    sleep 1
    kill -9 \$SERVER_PID 2>/dev/null   # Force kill if still running
    pkill -9 -f npnameserver 2>/dev/null
}

trap cleanup EXIT

# Start server in background
ensure_nprpc_bpf_capabilities \"\$SERVER\"
\"\$SERVER\" &
SERVER_PID=\$!

# Wait for server to start
sleep 3

# Run tests
\"\$TEST_SCRIPT\"
RESULT=\$?

exit \$RESULT
")

  # Make the wrapper executable
  file(CHMOD ${CMAKE_CURRENT_BINARY_DIR}/run_http3_tests.sh
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                GROUP_READ GROUP_EXECUTE
                WORLD_READ WORLD_EXECUTE
  )
  
  add_test(
    NAME HTTP3Transport
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/run_http3_tests.sh
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  )
  
  set_tests_properties(HTTP3Transport PROPERTIES
    TIMEOUT 60
    LABELS "http3;integration"
  )
  
  # Add dependency on the server test executable
  add_dependencies(nprpc_test nprpc_server_test)
endif()

# Ensure the auxiliary shared-memory test executables are built before
# running the post-build CTest invocation on the main `nprpc_test` target.
add_dependencies(nprpc_test
  test_lock_free_ring_buffer
  test_shared_memory_endpoint
  test_shared_memory_listener
  test_http_utils
)

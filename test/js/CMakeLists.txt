# JavaScript tests for NPRPC

# Collect TypeScript source files for dependency tracking
file(GLOB_RECURSE JS_TEST_TS_SOURCES 
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.ts"
    "${CMAKE_CURRENT_SOURCE_DIR}/test/*.ts"
)

# Configuration files that affect the build
set(JS_TEST_CONFIG_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/package.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/tsconfig.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/.mocharc.json"
)

set(NPRPC_TEST_JS_SRC ${CMAKE_BINARY_DIR}/nprpc_test_stub/src/gen/js/nprpc_test.ts)
set(NPRPC_TEST_JS_DST ${CMAKE_CURRENT_SOURCE_DIR}/src/gen/nprpc_test.ts)

# Copy generated test interfaces
add_custom_command(
  OUTPUT ${NPRPC_TEST_JS_DST}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_SOURCE_DIR}/src/gen
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${NPRPC_TEST_JS_SRC} ${NPRPC_TEST_JS_DST}
  DEPENDS ${NPRPC_TEST_JS_SRC}
  COMMENT "Copying nprpc_test_stub generated TypeScript to test/js/src/gen"
  VERBATIM
)

# Build and run JavaScript tests
add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/nprpc_js_test.stamp
  COMMAND npm ci && npm run build
  COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_CURRENT_BINARY_DIR}/nprpc_js_test.stamp
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  DEPENDS 
    ${JS_TEST_TS_SOURCES}
    ${JS_TEST_CONFIG_FILES}
    ${NPRPC_TEST_JS_DST}
    nprpc_js
    nprpc_server_test
  COMMENT "Building JavaScript tests"
  VERBATIM
)

add_custom_target(nprpc_js_test ALL
  DEPENDS
    ${CMAKE_CURRENT_BINARY_DIR}/nprpc_js_test.stamp
  COMMENT 
    "Building nprpc test js project"
  VERBATIM
)

# Run JavaScript tests using Mocha
add_custom_command(
  TARGET nprpc_js_test
  POST_BUILD
  COMMAND npm test
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMENT "Running JavaScript tests with Mocha"
  VERBATIM
)

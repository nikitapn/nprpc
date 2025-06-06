set(NPRPC_TEST_JS_SRC ${CMAKE_BINARY_DIR}/nprpc_test_stub/src/gen/js/test.ts)
set(NPRPC_TEST_JS_DST ${CMAKE_SOURCE_DIR}/nprpc/test/js/src/gen/test.ts)

add_custom_command(
  OUTPUT ${NPRPC_TEST_JS_DST}
  COMMAND ${CMAKE_COMMAND} -E make_directory
    ${CMAKE_SOURCE_DIR}/nprpc/test/js/src/gen
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${NPRPC_TEST_JS_SRC}
    ${NPRPC_TEST_JS_DST}
  DEPENDS ${NPRPC_TEST_JS_SRC}
  COMMENT "Copying nprpc_stub generated TypeScript headers to nprpc/nprpc_js/src/gen"
  VERBATIM
)

add_custom_command(
  OUTPUT nprpc_js_test.stamp
  COMMAND  npm ci && npm run build
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/nprpc/test/js
  DEPENDS ${NPRPC_TEST_JS_DST}
  COMMENT "Building nprpc test js project"
  VERBATIM
)

add_custom_target(nprpc_js_test ALL
  DEPENDS nprpc_js_test.stamp ${NPRPC_TEST_JS_DST}
)
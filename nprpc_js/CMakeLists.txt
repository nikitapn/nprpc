
set(NPRPC_BASE_DST ${CMAKE_CURRENT_SOURCE_DIR}/src/gen/nprpc_base.ts)
set(NPRPC_NAMESERVER_DST ${CMAKE_CURRENT_SOURCE_DIR}/src/gen/nprpc_nameserver.ts)

add_custom_command(
  OUTPUT ${NPRPC_BASE_DST} ${NPRPC_NAMESERVER_DST}
  COMMAND ${CMAKE_COMMAND} -E make_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gen
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_BINARY_DIR}/nprpc_stub/src/gen/js/nprpc_base.ts
    ${NPRPC_BASE_DST}
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_BINARY_DIR}/nprpc_stub/src/gen/js/nprpc_nameserver.ts
    ${NPRPC_NAMESERVER_DST}
  DEPENDS ${CMAKE_BINARY_DIR}/nprpc_stub/src/gen/js/nprpc_base.ts
          ${CMAKE_BINARY_DIR}/nprpc_stub/src/gen/js/nprpc_nameserver.ts
  COMMENT "Copying nprpc_stub generated TypeScript headers to nprpc/nprpc_js/src/gen"
  VERBATIM
)

add_custom_command(
  OUTPUT nprpc_js.stamp
  COMMAND  npm ci && npm run build-debug
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  DEPENDS ${NPRPC_BASE_DST} ${NPRPC_NAMESERVER_DST}
  COMMENT "Building nprpc js library"
  VERBATIM
)

add_custom_target(nprpc_js ALL
  DEPENDS nprpc_js.stamp ${NPRPC_BASE_DST} ${NPRPC_NAMESERVER_DST}
)
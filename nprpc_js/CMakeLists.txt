
# Add TypeScript source files
set(TS_SOURCES 
    "${CMAKE_CURRENT_SOURCE_DIR}/src/base.ts"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/flat_buffer.ts"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/index_internal.ts"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/index.ts"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/marshal_helpers.ts"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/nprpc.ts"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/utils.ts"
)

# Add configuration files that affect the build
set(CONFIG_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/package.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/tsconfig.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/webpack.config.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/fix-declaration-paths.js"
)

# Define output files that webpack generates
set(JS_OUTPUT_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/dist/index.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/dist/index.esm.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/dist/index.node.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/dist/index.d.ts"
)

# Generated TypeScript files from nprpc_stub
set(NPRPC_BASE_DST "${CMAKE_CURRENT_SOURCE_DIR}/src/gen/nprpc_base.ts")
set(NPRPC_NAMESERVER_DST "${CMAKE_CURRENT_SOURCE_DIR}/src/gen/nprpc_nameserver.ts")

# First, copy generated TypeScript files from nprpc_stub
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
  COMMENT "Copying nprpc_stub generated TypeScript to nprpc/nprpc_js/src/gen"
  VERBATIM
)

# Build JavaScript library - only rebuild when source files or config changes
add_custom_command(
  OUTPUT ${JS_OUTPUT_FILES}
  COMMAND npm ci && npm run build-prd
  # This command fixes issue when compiling generated .d.ts files from different projects
  # CMake always rubuilds the target if output modified time is older than input file modified time
  # but in this case the generated .d.ts files are identical and should not trigger rebuild
  # So we touch the output file to update its modified time
  # Webpack may also not update the modified time of the output files when the content is unchanged
  # but to be safe we always touch the output files after build
  COMMAND ${CMAKE_COMMAND} -E touch ${NPRPC_BASE_DST} ${NPRPC_NAMESERVER_DST} ${JS_OUTPUT_FILES}
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  DEPENDS ${TS_SOURCES} ${CONFIG_FILES} ${NPRPC_BASE_DST} ${NPRPC_NAMESERVER_DST}
  COMMENT "Building nprpc JavaScript library and touching output files"
  VERBATIM
)

add_custom_target(nprpc_js ALL
  DEPENDS ${JS_OUTPUT_FILES}
)

# Clean target to remove generated files
add_custom_target(nprpc_js_clean
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMAKE_CURRENT_SOURCE_DIR}/dist"
    COMMENT "Cleaning JavaScript build artifacts"
)

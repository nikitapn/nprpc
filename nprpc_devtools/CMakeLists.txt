# NPRPC DevTools Chrome extension

file(GLOB_RECURSE NPRPC_DEVTOOLS_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*"
  "${CMAKE_CURRENT_SOURCE_DIR}/public/*"
)

set(NPRPC_DEVTOOLS_CONFIG_FILES
  "${CMAKE_CURRENT_SOURCE_DIR}/index.html"
  "${CMAKE_CURRENT_SOURCE_DIR}/package.json"
  "${CMAKE_CURRENT_SOURCE_DIR}/package-lock.json"
  "${CMAKE_CURRENT_SOURCE_DIR}/svelte.config.js"
  "${CMAKE_CURRENT_SOURCE_DIR}/tsconfig.app.json"
  "${CMAKE_CURRENT_SOURCE_DIR}/tsconfig.json"
  "${CMAKE_CURRENT_SOURCE_DIR}/tsconfig.node.json"
  "${CMAKE_CURRENT_SOURCE_DIR}/vite.config.ts"
)

set(NPRPC_DEVTOOLS_STAMP "${CMAKE_CURRENT_BINARY_DIR}/nprpc_devtools.stamp")

add_custom_command(
  OUTPUT ${NPRPC_DEVTOOLS_STAMP}
  COMMAND npm ci
  COMMAND npm run build
  COMMAND ${CMAKE_COMMAND} -E touch ${NPRPC_DEVTOOLS_STAMP}
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  DEPENDS ${NPRPC_DEVTOOLS_SOURCES} ${NPRPC_DEVTOOLS_CONFIG_FILES}
  COMMENT "Building NPRPC DevTools extension"
  VERBATIM
)

add_custom_target(nprpc_devtools ALL
  DEPENDS ${NPRPC_DEVTOOLS_STAMP}
)

add_custom_target(nprpc_devtools_clean
  COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMAKE_CURRENT_SOURCE_DIR}/dist"
  COMMAND ${CMAKE_COMMAND} -E rm -f ${NPRPC_DEVTOOLS_STAMP}
  COMMENT "Cleaning NPRPC DevTools build artifacts"
)
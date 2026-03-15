# NPRPC node.js C++ addon

set(SOURCES 
  "${CMAKE_CURRENT_SOURCE_DIR}/src/addon.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/converter.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/shm_channel_wrapper.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/shm_channel_wrapper.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/index.js"
  "${CMAKE_CURRENT_SOURCE_DIR}/index.d.ts"
  "${CMAKE_CURRENT_SOURCE_DIR}/binding.gyp"
)

set(CONFIG_FILES
  "${CMAKE_CURRENT_SOURCE_DIR}/package.json"
  "${CMAKE_CURRENT_SOURCE_DIR}/tsconfig.json"
)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(NODE_BUILD_TYPE "Debug")
else()
  # Release of RelWithDebInfo
  set(NODE_BUILD_TYPE "Release")
endif()

set(NODE_OUTPUT_FILES
  "${CMAKE_CURRENT_SOURCE_DIR}/build/${NODE_BUILD_TYPE}/nprpc_shm.node"
)

add_custom_command(
  OUTPUT ${NODE_OUTPUT_FILES}
  COMMAND npm run install
  # This command fixes issue when compiling generated .d.ts files from different projects
  # CMake always rebuilds the target if output modified time is older than input file modified time
  # but in this case the generated .d.ts files are identical and should not trigger rebuild
  # So we touch the output file to update its modified time
  # Webpack may also not update the modified time of the output files when the content is unchanged
  # but to be safe we always touch the output files after build
  COMMAND ${CMAKE_COMMAND} -E touch ${NODE_OUTPUT_FILES}
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  DEPENDS ${SOURCES} ${CONFIG_FILES} nprpc npidl
  COMMENT "Building NPRPC Node.js C++ addon"
  VERBATIM
)

add_custom_target(nprpc_node_addon ALL
  DEPENDS ${NODE_OUTPUT_FILES}
)

add_custom_target(nprpc_node_addon_clean
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMAKE_CURRENT_SOURCE_DIR}/build"
    COMMENT "Cleaning Node.js C++ addon build artifacts"
)

# Run tests for the Node.js addon
add_custom_target(nprpc_node_addon_test
  COMMAND npm test
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  DEPENDS nprpc_node_addon
  COMMENT "Running tests for NPRPC Node.js C++ addon"
)
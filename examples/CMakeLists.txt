
add_subdirectory(ssr-svelte-app)

# Standalone hybrid example workspace:
#   examples/live-blog/
# It is intentionally not added here because the client is a separate
# SvelteKit app and the server is a standalone Swift package meant to be run
# with the development Docker image.

# Swift test server (for testing Swift clients without nameserver)
if(EXISTS ${CMAKE_SOURCE_DIR}/nprpc_swift/Examples/CMakeLists.txt)
  add_subdirectory(${CMAKE_SOURCE_DIR}/nprpc_swift/Examples ${CMAKE_BINARY_DIR}/nprpc_swift_examples)
endif()

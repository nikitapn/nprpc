
add_subdirectory(ssr-svelte-app)

# Swift test server (for testing Swift clients without nameserver)
if(EXISTS ${CMAKE_SOURCE_DIR}/nprpc_swift/Examples/CMakeLists.txt)
  add_subdirectory(${CMAKE_SOURCE_DIR}/nprpc_swift/Examples ${CMAKE_BINARY_DIR}/nprpc_swift_examples)
endif()

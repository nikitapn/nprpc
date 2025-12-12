
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/ssr-svelte-app.stamp
    COMMAND ${NPM_EXECUTABLE} install ${CMAKE_BINARY_DIR}/nprpc_node --save
    COMMAND ${CMAKE_COMMAND} -E copy_directory build/client ${CMAKE_CURRENT_BINARY_DIR}
    COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_BINARY_DIR}/ssr-svelte-app.stamp
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    # DEPENDS ${DEPS}
    COMMENT "Building Svelte SSR app"
)

add_custom_target(example_ssr_svelte_client
    DEPENDS ${CMAKE_BINARY_DIR}/ssr-svelte-app.stamp
)
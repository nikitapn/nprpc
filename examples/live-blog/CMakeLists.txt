find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_program(BASH_EXECUTABLE NAMES bash REQUIRED)
find_program(NPM_EXECUTABLE NAMES npm REQUIRED)

if(NOT TARGET nprpc_dev_docker)
  message(FATAL_ERROR "The live-blog Swift example requires the nprpc_dev_docker target")
endif()

if(NOT TARGET npidl)
  message(FATAL_ERROR "The live-blog example requires NPRPC_BUILD_TOOLS=ON so npidl is available")
endif()

set(LIVE_BLOG_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(LIVE_BLOG_GEN_SCRIPT ${LIVE_BLOG_ROOT}/scripts/gen_stubs.py)
set(LIVE_BLOG_BUILD_SCRIPT ${LIVE_BLOG_ROOT}/scripts/build-swift-server.sh)
set(LIVE_BLOG_RUN_SCRIPT ${LIVE_BLOG_ROOT}/scripts/run-swift-server.sh)
set(LIVE_BLOG_CLIENT_ROOT ${LIVE_BLOG_ROOT}/client)
set(LIVE_BLOG_CLIENT_INSTALL_STAMP ${CMAKE_CURRENT_BINARY_DIR}/live_blog_client_npm_install.stamp)
set(LIVE_BLOG_CPP_ROOT ${LIVE_BLOG_ROOT}/cpp)
set(LIVE_BLOG_CPP_STATIC_ROOT ${LIVE_BLOG_CLIENT_ROOT}/build/client)
set(LIVE_BLOG_CPP_RUNTIME_ROOT ${LIVE_BLOG_CLIENT_ROOT}/build)
set(LIVE_BLOG_IDL_FILES ${LIVE_BLOG_ROOT}/idl/live_blog.npidl)

npidl_generate_idl_files("${LIVE_BLOG_IDL_FILES}" live_blog_cpp_stub)

file(GLOB_RECURSE LIVE_BLOG_CLIENT_SOURCES CONFIGURE_DEPENDS
  ${LIVE_BLOG_CLIENT_ROOT}/src/*
  ${LIVE_BLOG_CLIENT_ROOT}/static/*
)

add_custom_target(live_blog_stubs
  COMMAND ${Python3_EXECUTABLE} ${LIVE_BLOG_GEN_SCRIPT}
  WORKING_DIRECTORY ${LIVE_BLOG_ROOT}
  DEPENDS nprpc_dev_docker
  COMMENT "Generating live-blog TypeScript and Swift stubs"
  USES_TERMINAL
  VERBATIM
)

add_custom_command(
  OUTPUT ${LIVE_BLOG_CLIENT_INSTALL_STAMP}
  COMMAND ${NPM_EXECUTABLE} ci
  COMMAND ${CMAKE_COMMAND} -E touch ${LIVE_BLOG_CLIENT_INSTALL_STAMP}
  WORKING_DIRECTORY ${LIVE_BLOG_CLIENT_ROOT}
  DEPENDS
    ${LIVE_BLOG_CLIENT_ROOT}/package.json
    ${CMAKE_SOURCE_DIR}/nprpc_node/package.json
    ${CMAKE_SOURCE_DIR}/nprpc_adapter_sveltekit/package.json
    ${CMAKE_SOURCE_DIR}/nprpc_vite_plugin/package.json
  COMMENT "Installing live-blog client dependencies"
  USES_TERMINAL
  VERBATIM
)

add_custom_target(live_blog_client_deps
  DEPENDS ${LIVE_BLOG_CLIENT_INSTALL_STAMP}
)

add_custom_target(live_blog_client
  COMMAND ${NPM_EXECUTABLE} run build
  WORKING_DIRECTORY ${LIVE_BLOG_CLIENT_ROOT}
  DEPENDS
    live_blog_client_deps
    live_blog_stubs
    ${LIVE_BLOG_CLIENT_ROOT}/package.json
    ${LIVE_BLOG_CLIENT_ROOT}/svelte.config.js
    ${LIVE_BLOG_CLIENT_ROOT}/tsconfig.json
    ${LIVE_BLOG_CLIENT_ROOT}/vite.config.ts
    ${LIVE_BLOG_CLIENT_SOURCES}
  COMMENT "Building live-blog Vite client"
  USES_TERMINAL
  VERBATIM
)

add_executable(live_blog_cpp_server
  ${LIVE_BLOG_CPP_ROOT}/main.cpp
  ${live_blog_cpp_stub_GENERATED_SOURCES}
)

add_dependencies(live_blog_cpp_server live_blog_client live_blog_cpp_stub_gen)

target_include_directories(live_blog_cpp_server
  PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${live_blog_cpp_stub_INCLUDE_DIR}
)

target_link_libraries(live_blog_cpp_server
  PRIVATE
    nprpc::nprpc
)

target_compile_definitions(live_blog_cpp_server
  PRIVATE
    NPRPC_LIVE_BLOG_STATIC_ROOT="${LIVE_BLOG_CPP_STATIC_ROOT}"
    NPRPC_LIVE_BLOG_RUNTIME_ROOT="${LIVE_BLOG_CPP_RUNTIME_ROOT}"
    NPRPC_LIVE_BLOG_CERT_FILE="${CMAKE_SOURCE_DIR}/certs/out/localhost.crt"
    NPRPC_LIVE_BLOG_KEY_FILE="${CMAKE_SOURCE_DIR}/certs/out/localhost.key"
)

add_custom_target(live_blog_swift_server
  COMMAND ${BASH_EXECUTABLE} ${LIVE_BLOG_BUILD_SCRIPT}
  WORKING_DIRECTORY ${LIVE_BLOG_ROOT}
  DEPENDS nprpc_dev_docker live_blog_stubs
  COMMENT "Building live-blog Swift server"
  USES_TERMINAL
  VERBATIM
)

add_custom_target(live_blog_example ALL
  DEPENDS live_blog_client live_blog_cpp_server live_blog_swift_server
)

add_custom_target(run_live_blog_cpp_server
  COMMAND $<TARGET_FILE:live_blog_cpp_server>
  DEPENDS live_blog_cpp_server
  COMMENT "Running live-blog C++ server"
  USES_TERMINAL
  VERBATIM
)

add_custom_target(run_live_blog_swift_server
  COMMAND ${BASH_EXECUTABLE} ${LIVE_BLOG_RUN_SCRIPT} $<$<CONFIG:Debug>:--debug>
  WORKING_DIRECTORY ${LIVE_BLOG_ROOT}
  DEPENDS live_blog_example
  COMMENT "Running live-blog Swift server in the development Docker image"
  USES_TERMINAL
  VERBATIM
)
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
set(LIVE_BLOG_CLIENT_WORKSPACE examples/live-blog/client)
file(GLOB_RECURSE LIVE_BLOG_CLIENT_SOURCES CONFIGURE_DEPENDS
  ${LIVE_BLOG_CLIENT_ROOT}/src/*
  ${LIVE_BLOG_ROOT}/templates/*
  ${LIVE_BLOG_ROOT}/web/*
)

add_custom_target(live_blog_stubs
  COMMAND ${Python3_EXECUTABLE} ${LIVE_BLOG_GEN_SCRIPT}
  WORKING_DIRECTORY ${LIVE_BLOG_ROOT}
  DEPENDS nprpc_dev_docker
  COMMENT "Generating live-blog TypeScript and Swift stubs"
  USES_TERMINAL
  VERBATIM
)

add_custom_target(live_blog_client_deps
  COMMAND ${NPM_EXECUTABLE} ls --workspace ${LIVE_BLOG_CLIENT_WORKSPACE} --depth=0 vite @tailwindcss/cli
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  DEPENDS
    ${CMAKE_SOURCE_DIR}/package.json
    ${CMAKE_SOURCE_DIR}/package-lock.json
    ${LIVE_BLOG_CLIENT_ROOT}/package.json
  COMMENT "Checking live-blog client workspace dependencies (run 'npm ci' at the repo root if this fails)"
  USES_TERMINAL
  VERBATIM
)

add_custom_target(live_blog_client
  COMMAND ${LIVE_BLOG_ROOT}/scripts/build-assets.sh
  WORKING_DIRECTORY ${LIVE_BLOG_ROOT}
  DEPENDS
    live_blog_client_deps
    live_blog_stubs
    ${LIVE_BLOG_CLIENT_ROOT}/package.json
    ${LIVE_BLOG_CLIENT_ROOT}/tsconfig.json
    ${LIVE_BLOG_CLIENT_ROOT}/vite.config.ts
    ${LIVE_BLOG_CLIENT_SOURCES}
  COMMENT "Building live-blog static root (islands, CSS, vendored assets)"
  USES_TERMINAL
  VERBATIM
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
  DEPENDS live_blog_client live_blog_swift_server
)

add_custom_target(run_live_blog_swift_server
  COMMAND ${BASH_EXECUTABLE} ${LIVE_BLOG_RUN_SCRIPT} $<$<CONFIG:Debug>:--debug>
  WORKING_DIRECTORY ${LIVE_BLOG_ROOT}
  DEPENDS live_blog_example
  COMMENT "Running live-blog Swift server in the development Docker image"
  USES_TERMINAL
  VERBATIM
)
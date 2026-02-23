# npidl - NPRPC Interface Definition Language compiler

# Generate builtins.hpp from nprpc_base.npidl
file(READ "${CMAKE_SOURCE_DIR}/idl/nprpc_base.npidl" NPRPC_BASE_IDL_CONTENT)
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/src/builtins.hpp.in"
  "${CMAKE_CURRENT_BINARY_DIR}/builtins.hpp"
  @ONLY
)

# Library with the core parser logic (reusable for tests)
add_library(npidl_core STATIC
  src/ast.hpp
  src/builder.hpp
  src/builder.cpp
  src/arguments_builder.hpp
  src/arguments_builder.cpp
  src/cpp_builder.cpp
  src/cpp_builder.hpp
  src/lsp_server.hpp
  src/lsp_server.cpp
  src/parse_for_lsp.hpp
  src/parser_interfaces.hpp
  src/parser_implementations.hpp
  src/parser_implementations.cpp
  src/source_location.hpp
  src/workspace_manager.hpp
  src/workspace_manager.cpp
  src/compilation.cpp
  src/ts_builder.hpp
  src/ts_builder.cpp
  src/swift_builder.hpp
  src/swift_builder.cpp
  src/utils.cpp
  src/utils.hpp
  src/library.cpp
  src/namespace.cpp
  src/context.cpp
)

target_include_directories(npidl_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}  # For generated builtins.hpp
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_SOURCE_DIR}/include/nprpc/impl/misc
    ${CMAKE_SOURCE_DIR}/third_party/glaze/include
)

# Find Boost (optional, for program_options)
if(NOT TARGET Boost::program_options)
  # Handle new CMake Boost policy
  if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
  endif()
  
  find_package(Boost QUIET COMPONENTS program_options)
  if(Boost_FOUND)
    target_link_libraries(npidl_core PUBLIC Boost::program_options)
  else()
    message(WARNING "Boost program_options not found - npidl may have limited functionality")
  endif()
endif()

# Main executable
add_executable(npidl
  src/main.cpp
)

target_include_directories(npidl
  PRIVATE
    ${CMAKE_SOURCE_DIR}/include/nprpc/impl/misc
    ${CMAKE_SOURCE_DIR}/third_party/glaze/include
)

target_link_libraries(npidl
  PRIVATE 
    npidl_core
    Boost::headers
)

# Optional: Build tests
if(NPRPC_BUILD_TESTS AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/test)
  add_subdirectory(test)
  add_dependencies(npidl_test npidl)
endif()

# Library with the core parser logic (reusable for tests)
add_library(npidl_core STATIC
  src/ast.hpp
  src/builder.hpp
  src/cpp_builder.cpp
  src/cpp_builder.hpp
  src/lsp_server.hpp
  src/lsp_server.cpp
  src/parse_for_lsp.hpp
  src/parser_interface.hpp
  src/source_location.hpp
  src/ts_builder.hpp
  src/ts_builder.cpp
  src/utils.cpp
  src/utils.hpp
)

target_include_directories(npidl_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/nprpc/include
  ${CMAKE_SOURCE_DIR}/nplib/include
  ${CMAKE_SOURCE_DIR}/3rd/glaze/include
)

if(UNIX)
  target_link_libraries(npidl_core PUBLIC boost_program_options)
endif()

# Main executable
add_executable(npidl
  src/main.cpp
)

target_link_libraries(npidl PRIVATE npidl_core)

if (NOT OPT_NPRPC_SKIP_TESTS)
  # Test executable for npidl
  add_subdirectory(test)
  add_dependencies(npidl_test npidl)
endif()
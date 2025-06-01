
add_executable(npidl
  src/ast.hpp
  src/builder.hpp
  src/cpp_builder.cpp
  src/cpp_builder.hpp
  src/main.cpp
  src/ts_builder.hpp
  src/ts_builder.cpp
  src/utils.cpp
  src/utils.hpp
)

target_include_directories(npidl PRIVATE
  ${CMAKE_SOURCE_DIR}/nprpc/include
)

if(UNIX)
  target_link_libraries(npidl PRIVATE boost_program_options)
endif()

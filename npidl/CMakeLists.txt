
add_executable(npidl
  src/ast.hpp
  src/builder.hpp
  src/check_size.cpp
  src/cpp_builder.cpp
  src/cpp_builder.hpp
  src/main.cpp
  src/ts_builder.hpp
  src/ts_builder.cpp
  src/utils.cpp
  src/utils.hpp
)

if(UNIX)
  target_link_libraries(npidl PUBLIC boost_program_options)
endif()
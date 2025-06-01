
add_library(check_size STATIC
  src/check_size.cpp
)

target_include_directories(check_size PRIVATE
  ${CMAKE_SOURCE_DIR}/nprpc/include
  ${CMAKE_SOURCE_DIR}/nprpc/npidl/src
)


add_library(check_size STATIC
  src/check_size.cpp
)

target_include_directories(check_size PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
  ${CMAKE_CURRENT_SOURCE_DIR}/../npidl/src
)

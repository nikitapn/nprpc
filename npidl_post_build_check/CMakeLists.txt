# Check size utility

add_library(npidl_check_size STATIC
  src/check_size.cpp
  ${nprpc_stub_COPIED_HEADERS}
)

target_include_directories(npidl_check_size
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../npidl/src
)

# Depend on generated headers if they're being generated
add_dependencies(npidl_check_size nprpc_copy_headers)

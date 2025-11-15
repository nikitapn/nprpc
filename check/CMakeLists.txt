# Check size utility

add_library(check_size STATIC
  src/check_size.cpp
)

target_include_directories(check_size 
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../npidl/src
)

# Depend on generated headers if they're being generated
if(TARGET nprpc_copy_headers)
  add_dependencies(check_size nprpc_copy_headers)
endif()

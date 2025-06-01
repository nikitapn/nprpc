include(${CMAKE_SOURCE_DIR}/nprpc/cmake/npidl.cmake)

set(IDL_FILES 
  ${CMAKE_SOURCE_DIR}/nprpc/test/idl/test.npidl
)

npidl_generate_idl_files("${IDL_FILES}" nprpc_test_stub)

add_custom_target(
  nprpc_test_gen
  DEPENDS ${nprpc_test_stub_GENERATED_SOURCES} ${nprpc_test_stub_GENERATED_HEADERS}
  COMMENT "Generating all nprpc_test_stub files"
)

add_executable(nprpc_test
  src/main.cpp
  ${nprpc_test_stub_GENERATED_SOURCES}
)

add_dependencies(nprpc_test nprpc_test_gen)

target_include_directories(nprpc_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/nprpc/include
    ${CMAKE_SOURCE_DIR}/nplib/include
    ${nprpc_test_stub_INCLUDE_DIR}  # For generated headers
)

target_link_libraries(nprpc_test
  GTest::gtest_main
	nprpc
)

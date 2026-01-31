# CMakeLists.txt for Swift test server
# Builds the shape_server executable for testing Swift client

# This assumes it's included from the main NPRPC CMakeLists.txt
# or built with proper include paths

add_executable(swift_shape_server
    shape_server.cpp
    ${CMAKE_SOURCE_DIR}/nprpc_swift/Tests/NPRPCTests/Generated/basic_test.cpp
)

target_include_directories(swift_shape_server PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}/include
    ${CMAKE_SOURCE_DIR}/nprpc_swift/Tests/NPRPCTests
)

target_link_libraries(swift_shape_server PRIVATE
    nprpc
)

target_compile_features(swift_shape_server PRIVATE cxx_std_23)

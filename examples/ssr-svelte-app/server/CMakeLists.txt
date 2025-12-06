cmake_minimum_required(VERSION 3.20)

add_executable(example_ssr_server
  src/main.cpp
)

target_link_libraries(example_ssr_server PRIVATE nprpc::nprpc)

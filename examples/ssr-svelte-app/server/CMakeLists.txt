cmake_minimum_required(VERSION 3.20)

add_executable(ssr_server
    src/main.cpp
)

target_link_libraries(ssr_server PRIVATE 
    nprpc
    nlohmann_json::nlohmann_json
)

target_compile_features(ssr_server PRIVATE cxx_std_23)

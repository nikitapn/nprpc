cmake_minimum_required(VERSION 3.15)

add_executable(npquicrouter
  src/main.cpp
)

target_compile_features(npquicrouter PRIVATE cxx_std_23)

target_include_directories(npquicrouter PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/glaze/include
)

target_link_libraries(npquicrouter PRIVATE
  Boost::headers
  OpenSSL::SSL
  OpenSSL::Crypto
  Threads::Threads
)

if(UNIX)
  target_compile_options(npquicrouter PRIVATE
    -Wall -Wextra -fno-omit-frame-pointer
  )
endif()

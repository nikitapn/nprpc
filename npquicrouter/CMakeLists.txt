cmake_minimum_required(VERSION 3.15)

# Support standalone builds (e.g. Docker for VPS deployment).
# When included via add_subdirectory() from the nprpc root, these
# find_package calls are already handled by the parent CMakeLists.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(npquicrouter CXX)
  find_package(Threads REQUIRED)
  find_package(OpenSSL REQUIRED)
  find_package(Boost REQUIRED)
  # CMake FindBoost module < 3.19 may not expose Boost::headers; patch it.
  if(NOT TARGET Boost::headers AND TARGET Boost::boost)
    add_library(Boost::headers ALIAS Boost::boost)
  endif()
endif()

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

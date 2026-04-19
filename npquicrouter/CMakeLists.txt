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

# ── LockFreeRingBuffer / logging sources ─────────────────────────────────────
# Use in-tree sources when building inside the nprpc repo tree (standard dev
# or CI builds).  For standalone Docker builds the Dockerfile copies the same
# files under nprpc_src/ / nprpc_include/ next to this CMakeLists.txt.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../src/shm/lock_free_ring_buffer.cpp")
  set(NPRPC_SHM_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/shm/lock_free_ring_buffer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/logging.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/thread_identity.cpp"
  )
  set(NPRPC_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../include")
  set(NPRPC_SRC_DIR     "${CMAKE_CURRENT_SOURCE_DIR}/../src")
else()
  # Standalone / Docker build: files copied alongside this CMakeLists.txt.
  set(NPRPC_SHM_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/nprpc_src/lock_free_ring_buffer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/nprpc_src/logging.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/nprpc_src/thread_identity.cpp"
  )
  set(NPRPC_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/nprpc_include")
  set(NPRPC_SRC_DIR     "${CMAKE_CURRENT_SOURCE_DIR}/nprpc_src")
endif()

add_executable(npquicrouter
  src/main.cpp
  ${NPRPC_SHM_SRC}
)

target_compile_features(npquicrouter PRIVATE cxx_std_23)

target_include_directories(npquicrouter PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/glaze/include
  ${NPRPC_INCLUDE_DIR}
  ${NPRPC_SRC_DIR}
)

target_link_libraries(npquicrouter PRIVATE
  Boost::headers
  OpenSSL::SSL
  OpenSSL::Crypto
  Threads::Threads
  $<$<PLATFORM_ID:Linux>:rt>   # shm_open / shm_unlink
)

if(UNIX)
  target_compile_options(npquicrouter PRIVATE
    -Wall -Wextra -fno-omit-frame-pointer
  )
endif()

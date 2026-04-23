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

  # BPF support for SO_REUSEPORT multi-worker routing (standalone build).
  if(UNIX AND NOT WIN32)
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
      pkg_check_modules(libbpf QUIET IMPORTED_TARGET libbpf)
    endif()
    if(NOT CLANG_EXECUTABLE)
      find_program(CLANG_EXECUTABLE NAMES clang)
    endif()
    if(NOT XXD_EXECUTABLE)
      find_program(XXD_EXECUTABLE NAMES xxd)
    endif()
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
    "${CMAKE_CURRENT_SOURCE_DIR}/nprpc_src/shm/lock_free_ring_buffer.cpp"
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

# ── SO_REUSEPORT eBPF multi-worker routing (optional) ────────────────────────
# Requires: clang (to compile the BPF C source), xxd (to embed the object as
# a C header), and libbpf (runtime BPF loader used by npquicrouter).
if(UNIX AND NOT WIN32)
  # In parent-project builds the find_program/pkg_check calls were already
  # done in the root CMakeLists.  In standalone builds they are done above.
  if(TARGET PkgConfig::libbpf AND CLANG_EXECUTABLE AND XXD_EXECUTABLE)
    set(NPRPC_ROUTER_REUSEPORT_BPF_OBJECT
      ${CMAKE_CURRENT_BINARY_DIR}/generated/quic_reuseport_router.bpf.o)
    set(NPRPC_ROUTER_REUSEPORT_BPF_HEADER
      ${CMAKE_CURRENT_BINARY_DIR}/generated/quic_reuseport_router_bpf.inc)

    add_custom_command(
      OUTPUT
        ${NPRPC_ROUTER_REUSEPORT_BPF_OBJECT}
        ${NPRPC_ROUTER_REUSEPORT_BPF_HEADER}
      COMMAND ${CMAKE_COMMAND} -E make_directory
        ${CMAKE_CURRENT_BINARY_DIR}/generated
      COMMAND ${CLANG_EXECUTABLE}
        -target bpf
        -O2
        -g
        -c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/quic_reuseport_router.bpf.c
        -o ${NPRPC_ROUTER_REUSEPORT_BPF_OBJECT}
      COMMAND ${XXD_EXECUTABLE}
        -i
        -n nprpc_quic_reuseport_router_bpf_obj
        ${NPRPC_ROUTER_REUSEPORT_BPF_OBJECT}
        ${NPRPC_ROUTER_REUSEPORT_BPF_HEADER}
      DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/quic_reuseport_router.bpf.c
      COMMENT "Compiling npquicrouter QUIC reuseport eBPF program"
      VERBATIM
    )

    add_custom_target(npquicrouter_reuseport_bpf_gen
      DEPENDS
        ${NPRPC_ROUTER_REUSEPORT_BPF_OBJECT}
        ${NPRPC_ROUTER_REUSEPORT_BPF_HEADER}
    )

    add_dependencies(npquicrouter npquicrouter_reuseport_bpf_gen)

    target_include_directories(npquicrouter PRIVATE
      ${CMAKE_CURRENT_BINARY_DIR}/generated
    )

    target_link_libraries(npquicrouter PRIVATE PkgConfig::libbpf)

    target_compile_definitions(npquicrouter PRIVATE
      NPRPC_ROUTER_REUSEPORT_BPF_ENABLED=1
    )

    message(STATUS "npquicrouter: SO_REUSEPORT eBPF multi-worker routing enabled")
  else()
    message(STATUS "npquicrouter: SO_REUSEPORT eBPF disabled (clang, xxd, or libbpf not found)")
  endif()
endif()

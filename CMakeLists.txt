cmake_minimum_required(VERSION 3.15)

# Determine if nprpc is built as a subproject (using add_subdirectory) or standalone
if(NOT DEFINED PROJECT_NAME)
  set(NPRPC_IS_TOPLEVEL_PROJECT ON)
else()
  set(NPRPC_IS_TOPLEVEL_PROJECT OFF)
endif()

project(nprpc 
  VERSION 1.0.0
  DESCRIPTION "Multi-Transport RPC Framework and more"
  LANGUAGES CXX
)

# C++ Standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_definitions(BOOST_ASIO_NO_DEPRECATED)

# Options
# For systems with OpenSSL < 3.5, can use system libcrypto (faster builds)
# For OpenSSL 3.5+, must build from source due to ossl_time_now being internal
# Set NPRPC_QUIC_USE_SYSTEM_LIBCRYPTO=ON when building on Debian-based systems with OpenSSL < 3.5
option(NPRPC_QUIC_USE_SYSTEM_LIBCRYPTO "Use system libcrypto for QUIC (requires OpenSSL < 3.5)" OFF)
option(NPRPC_BUILD_TOOLS "Build npidl tool and nameserver" ${NPRPC_IS_TOPLEVEL_PROJECT})
option(NPRPC_BUILD_TESTS "Build tests" ${NPRPC_IS_TOPLEVEL_PROJECT})
option(NPRPC_BUILD_JS "Build JavaScript bindings" ${NPRPC_IS_TOPLEVEL_PROJECT})
option(NPRPC_INSTALL "Generate install target" ${NPRPC_IS_TOPLEVEL_PROJECT})
option(NPRPC_ENABLE_QUIC "Enable QUIC transport using MsQuic" OFF)
option(NPRPC_ENABLE_HTTP3 "Enable HTTP/3 server" OFF)
option(NPRPC_ENABLE_SSR "Enable SSR support via Node.js worker (requires Boost.Process)" OFF)
option(NPRPC_BUILD_NODE_ADDON "Build Node.js addon" OFF)
option(BUILD_SHARED_LIBS "Build shared libraries" ON)

# HTTP/3 backend selection: msh3 (default, uses MsQuic) or nghttp3 (uses ngtcp2)
set(NPRPC_HTTP3_BACKEND "msh3" CACHE STRING "HTTP/3 backend: msh3 or nghttp3")
set_property(CACHE NPRPC_HTTP3_BACKEND PROPERTY STRINGS "msh3" "nghttp3")

# Validate HTTP/3 backend choice
if(NPRPC_ENABLE_HTTP3)
  if(NOT NPRPC_HTTP3_BACKEND STREQUAL "msh3" AND NOT NPRPC_HTTP3_BACKEND STREQUAL "nghttp3")
    message(FATAL_ERROR "Invalid NPRPC_HTTP3_BACKEND: ${NPRPC_HTTP3_BACKEND}. Must be 'msh3' or 'nghttp3'")
  endif()
  message(STATUS "HTTP/3 backend: ${NPRPC_HTTP3_BACKEND}")
endif()

# HTTP/3 with msh3 backend requires QUIC (MsQuic)
if(NPRPC_ENABLE_HTTP3 AND NPRPC_HTTP3_BACKEND STREQUAL "msh3" AND NOT NPRPC_ENABLE_QUIC)
  message(STATUS "HTTP/3 with msh3 backend requires QUIC - enabling QUIC transport")
  set(NPRPC_ENABLE_QUIC ON CACHE BOOL "Enable QUIC transport using MsQuic" FORCE)
endif()

# Find dependencies
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

# Custom Boost directory support (for Docker/CI builds with local Boost)
if(DEFINED BOOST_DIR)
  message(STATUS "Using custom Boost directory: ${BOOST_DIR}")
  set(BOOST_ROOT ${BOOST_DIR})
  set(Boost_NO_SYSTEM_PATHS ON)
  set(CMAKE_PREFIX_PATH ${BOOST_DIR} ${CMAKE_PREFIX_PATH})
endif()

# Find Boost (required for Asio and other header-only libraries)
find_package(Boost REQUIRED)

# When using Swift's Clang 17, explicitly link libstdc++
if(CMAKE_CXX_COMPILER MATCHES "swift.*clang")
  link_libraries(stdc++)
  message(STATUS "Using Swift's Clang - linking libstdc++")
endif()

# MsQuic integration
if(NPRPC_ENABLE_QUIC)
  message(STATUS "QUIC transport enabled - building MsQuic")
  
  # Configure MsQuic options before adding subdirectory
  # Use MsQuic's bundled OpenSSL build (not system libcrypto) to avoid
  # symbol compatibility issues with OpenSSL 3.5+ (ossl_time_now is internal)
  # See: https://github.com/microsoft/msquic/issues/5210
  set(QUIC_TLS_LIB "openssl" CACHE STRING "Use OpenSSL for TLS" FORCE)
  set(QUIC_USE_SYSTEM_LIBCRYPTO ${NPRPC_QUIC_USE_SYSTEM_LIBCRYPTO} CACHE BOOL "Build libcrypto from source" FORCE)
  set(QUIC_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
  set(QUIC_BUILD_TEST OFF CACHE BOOL "" FORCE)
  set(QUIC_BUILD_PERF OFF CACHE BOOL "" FORCE)
  set(QUIC_BUILD_SHARED ON CACHE BOOL "" FORCE)
  set(QUIC_ENABLE_LOGGING OFF CACHE BOOL "" FORCE)
  
  add_subdirectory(third_party/msquic)
endif()

# HTTP/3 support
if(NPRPC_ENABLE_HTTP3)
  if(NPRPC_HTTP3_BACKEND STREQUAL "msh3")
    message(STATUS "HTTP/3 server enabled - building msh3 backend")
    
    # Build ls-qpack first (msh3 needs it)
    set(LSQPACK_TESTS OFF CACHE BOOL "" FORCE)
    set(LSQPACK_BIN OFF CACHE BOOL "" FORCE)
    add_subdirectory(third_party/ls-qpack)
    
    # Build msh3 (will find existing msquic and ls-qpack targets)
    set(MSH3_TOOL OFF CACHE BOOL "" FORCE)
    set(MSH3_PING OFF CACHE BOOL "" FORCE)
    set(MSH3_TEST OFF CACHE BOOL "" FORCE)
    add_subdirectory(third_party/msh3)
    
  elseif(NPRPC_HTTP3_BACKEND STREQUAL "nghttp3")
    message(STATUS "HTTP/3 server enabled - building nghttp3 backend")
    
    # Build ngtcp2 (QUIC implementation)
    set(ENABLE_STATIC_LIB ON CACHE BOOL "" FORCE)
    set(ENABLE_SHARED_LIB OFF CACHE BOOL "" FORCE)
    set(ENABLE_LIB_ONLY ON CACHE BOOL "" FORCE)
    set(ENABLE_OPENSSL ON CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    # Need PIC for static libs that will be linked into shared libraries
    set(CMAKE_POSITION_INDEPENDENT_CODE ON CACHE BOOL "" FORCE)
    add_subdirectory(third_party/ngtcp2 EXCLUDE_FROM_ALL)
    
    # Build nghttp3 (HTTP/3 framing)
    set(ENABLE_STATIC_LIB ON CACHE BOOL "" FORCE)
    set(ENABLE_SHARED_LIB OFF CACHE BOOL "" FORCE)
    set(ENABLE_LIB_ONLY ON CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    add_subdirectory(third_party/nghttp3 EXCLUDE_FROM_ALL)
  endif()
endif()

# Add subdirectories
if(NPRPC_BUILD_TOOLS)
  add_subdirectory(npidl)
endif()

# Include IDL generation functions
include(cmake/npidl.cmake)

# Generate IDL files if npidl is available
set(nprpc_stub_GENERATED_SOURCES "")
set(IDL_FILES
  ${CMAKE_CURRENT_SOURCE_DIR}/idl/nprpc_base.npidl
  ${CMAKE_CURRENT_SOURCE_DIR}/idl/nprpc_nameserver.npidl
  ${CMAKE_CURRENT_SOURCE_DIR}/idl/nprpc_node.npidl
)

npidl_generate_idl_files("${IDL_FILES}" nprpc_stub)

# Copy generated headers to include directory
foreach(file ${nprpc_stub_GENERATED_HEADERS})
  get_filename_component(filename ${file} NAME)
  add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/include/${filename}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${file} ${CMAKE_CURRENT_SOURCE_DIR}/include
    DEPENDS ${file}
    COMMENT "Copying ${filename} to include directory"
  )
  list(APPEND nprpc_stub_COPIED_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/include/${filename})
endforeach()

# Add a custom target for the copied headers
add_custom_target(nprpc_copy_headers
  DEPENDS ${nprpc_stub_COPIED_HEADERS}
)

# Main library target
add_library(nprpc
  ${nprpc_stub_GENERATED_SOURCES}
  # Shared Memory transport
  src/shm/lock_free_ring_buffer.cpp
  src/shm/shared_memory_channel.cpp
  src/shm/shared_memory_connection.cpp
  src/shm/shared_memory_listener.cpp
  src/shm/shared_memory_server_session.cpp
  src/shm/shared_memory_init.cpp
  # TCP transport
  src/tcp/client_socket_connection.cpp
  src/tcp/server_session_socket.cpp
  # HTTP transport
  src/http/http_server.cpp
  src/http/http_utils.cpp
  src/http/http_rpc_session.cpp
  src/http/http_file_cache.cpp
  src/http/http_auth.cpp
  src/http/http3_server_msh3.cpp
  src/http/http3_server_nghttp3.cpp
  # WebSocket transport
  src/ws/async_connect.cpp
  src/ws/client_websocket_connection.cpp
  src/ws/server_session_websocket.cpp
  src/ws/websocket_session.cpp
  # QUIC transport
  src/quic/quic_transport.cpp
  # UDP transport
  src/udp/udp_connection.cpp
  src/udp/udp_listener.cpp
  # Core framework files
  src/nprpc.cpp
  src/logging.cpp
  src/rpc_impl.cpp
  src/session_context.cpp
  src/session.cpp
  src/stream_manager.cpp
  src/helper.hpp
  src/helper.cpp
  src/flat_buffer.cpp
  src/uuid.cpp
  src/framework.h
  # Public headers
  include/nprpc_base.hpp
  include/nprpc_nameserver.hpp
  include/nprpc/common.hpp
  include/nprpc/basic.hpp
  include/nprpc/endpoint.hpp
  include/nprpc/exception.hpp
  include/nprpc/export.hpp
  include/nprpc/flat.hpp
  include/nprpc/nprpc.hpp
  include/nprpc/object_ptr.hpp
  include/nprpc/session_context.h
  include/nprpc/utils.hpp
  include/nprpc/host_json.hpp
  include/nprpc/stream_base.hpp
  include/nprpc/stream_reader.hpp
  include/nprpc/stream_writer.hpp
  # Implementation headers
  include/nprpc/impl/async_connect.hpp
  include/nprpc/impl/nprpc_impl.hpp
  include/nprpc/impl/session.hpp
  include/nprpc/impl/udp_connection.hpp
  include/nprpc/impl/udp_listener.hpp
  include/nprpc/impl/quic_transport.hpp
  include/nprpc/impl/http3_server.hpp
  include/nprpc/impl/uuid.hpp
  include/nprpc/impl/websocket_session.hpp
  include/nprpc/impl/stream_manager.hpp
  include/nprpc/serialization/iarchive.h
  include/nprpc/serialization/nvp.hpp
  include/nprpc/serialization/oarchive.h
  include/nprpc/serialization/serialization.h
)

# SSR support (Node.js worker)
if(NPRPC_ENABLE_SSR)
  target_sources(nprpc PRIVATE
    src/ssr/node_worker_manager.cpp
    src/ssr/ssr_manager.cpp
    include/nprpc/impl/node_worker_manager.hpp
    include/nprpc/impl/ssr_manager.hpp
  )
endif()

# Add alias for namespaced target (modern CMake practice)
add_library(nprpc::nprpc ALIAS nprpc)

# Set target properties
set_target_properties(nprpc PROPERTIES
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
  POSITION_INDEPENDENT_CODE ON
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
)

# Include directories
target_include_directories(nprpc
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link libraries
target_link_libraries(nprpc
  PUBLIC
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
    Boost::headers
)

# MsQuic linking
if(NPRPC_ENABLE_QUIC)
  target_link_libraries(nprpc PUBLIC msquic)
  target_include_directories(nprpc PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/msquic/src/inc
  )
  target_compile_definitions(nprpc PUBLIC NPRPC_QUIC_ENABLED)
endif()

# HTTP/3 linking
if(NPRPC_ENABLE_HTTP3)
  target_compile_definitions(nprpc PUBLIC NPRPC_HTTP3_ENABLED)
  
  if(NPRPC_HTTP3_BACKEND STREQUAL "msh3")
    target_link_libraries(nprpc PRIVATE msh3)
    target_include_directories(nprpc PRIVATE 
      ${CMAKE_CURRENT_SOURCE_DIR}/third_party/msh3
    )
    target_compile_definitions(nprpc PRIVATE NPRPC_HTTP3_BACKEND_MSH3)
    
  elseif(NPRPC_HTTP3_BACKEND STREQUAL "nghttp3")
    target_link_libraries(nprpc PRIVATE 
      ngtcp2_static
      ngtcp2_crypto_ossl_static
      nghttp3_static
    )
    target_include_directories(nprpc PRIVATE 
      ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ngtcp2/lib/includes
      ${CMAKE_CURRENT_BINARY_DIR}/third_party/ngtcp2/lib/includes
      ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ngtcp2/crypto/includes
      ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nghttp3/lib/includes
      ${CMAKE_CURRENT_BINARY_DIR}/third_party/nghttp3/lib/includes
    )
    target_compile_definitions(nprpc PRIVATE NPRPC_HTTP3_BACKEND_NGHTTP3)
  endif()
endif()

# Platform-specific settings
if(WIN32)
  target_link_libraries(nprpc PUBLIC crypt32)
endif()

# SSR support (Node.js worker)
if(NPRPC_ENABLE_SSR)
  message(STATUS "SSR support enabled - requires Boost.Process v2")
  
  # Find Boost with required components for Process v2
  find_package(Boost REQUIRED COMPONENTS filesystem process)
  
  target_include_directories(nprpc PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/glaze/include
  )
  target_link_libraries(nprpc PRIVATE Boost::filesystem Boost::process)
  target_compile_definitions(nprpc PUBLIC NPRPC_SSR_ENABLED)
endif()

# Ensure export macro is defined when building the library so symbols are exported
target_compile_definitions(nprpc PRIVATE NPRPC_EXPORTS)

# Check size utility
add_library(npidl_check_size STATIC
  npidl/post_build/check_size.cpp
  ${nprpc_stub_COPIED_HEADERS}
)

target_include_directories(npidl_check_size
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/npidl/src
)

target_link_libraries(npidl_check_size
  PRIVATE
    Boost::headers
)

add_dependencies(npidl_check_size nprpc_copy_headers)
add_dependencies(nprpc npidl_check_size)

# Build nameserver executable
if(NPRPC_BUILD_TOOLS)
  # Filter nameserver generated sources
  set(nprpc_nameserver_GENERATED_SOURCES "")
  foreach(file ${nprpc_stub_GENERATED_SOURCES})
    if(file MATCHES "nprpc_nameserver")
      list(APPEND nprpc_nameserver_GENERATED_SOURCES ${file})
    endif()
  endforeach()

  add_executable(npnameserver
    npnameserver/npnameserver.cpp
    ${nprpc_nameserver_GENERATED_SOURCES}
  )

  target_link_libraries(npnameserver
    PRIVATE
      nprpc::nprpc
    )

  if(TARGET npidl)
    add_dependencies(npnameserver nprpc_stub_gen)
  endif()
endif()

# Optional components

if(NPRPC_BUILD_JS AND UNIX)
  add_subdirectory(nprpc_js)
endif()

if(NPRPC_BUILD_TESTS AND UNIX)
  enable_testing()
  add_subdirectory(test)
endif()

# Benchmarks (optional, requires Google Benchmark)
if(NPRPC_BUILD_TESTS AND UNIX)
  add_subdirectory(benchmark)
endif()

if(NPRPC_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

# Node.js Addon
# if(NPRPC_BUILD_NODE_ADDON)
#   find_program(NPM_EXECUTABLE npm REQUIRED)

#   set(NPRPC_NODE_SOURCES
#     ${CMAKE_CURRENT_SOURCE_DIR}/include/nprpc_node.hpp
#     ${CMAKE_CURRENT_SOURCE_DIR}/nprpc_node/binding.gyp
#     ${CMAKE_CURRENT_SOURCE_DIR}/nprpc_node/package.json
#     ${CMAKE_CURRENT_SOURCE_DIR}/nprpc_node/src/addon.cpp
#     ${CMAKE_CURRENT_SOURCE_DIR}/nprpc_node/src/shm_channel_wrapper.cpp
#     ${CMAKE_CURRENT_SOURCE_DIR}/nprpc_node/src/shm_channel_wrapper.hpp
#     ${CMAKE_CURRENT_SOURCE_DIR}/nprpc_node/src/nprpc_stub.cpp
#     ${CMAKE_CURRENT_SOURCE_DIR}/src/shm/lock_free_ring_buffer.cpp
#     ${CMAKE_CURRENT_SOURCE_DIR}/include/nprpc/impl/lock_free_ring_buffer.hpp
#   )

#   add_custom_command(
#     OUTPUT ${CMAKE_BINARY_DIR}/nprpc_shm.node
#     COMMAND ${NPM_EXECUTABLE} install
#     COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/nprpc_node
#     COMMAND ${CMAKE_COMMAND} -E copy
#       build/Release/nprpc_shm.node
#       ${CMAKE_BINARY_DIR}/nprpc_node/nprpc_shm.node
#     COMMAND ${CMAKE_COMMAND} -E copy
#       index.js
#       ${CMAKE_BINARY_DIR}/nprpc_node/index.js
#     COMMAND ${CMAKE_COMMAND} -E copy
#       index.d.ts
#       ${CMAKE_BINARY_DIR}/nprpc_node/index.d.ts
#     COMMAND ${CMAKE_COMMAND} -E copy
#       package.json
#       ${CMAKE_BINARY_DIR}/nprpc_node/package.json
#     WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/nprpc_node
#     DEPENDS ${NPRPC_NODE_SOURCES}
#     COMMENT "Building Node.js addon"
#     VERBATIM
#   )

#   add_custom_target(nprpc_node_addon ALL
#     DEPENDS ${CMAKE_BINARY_DIR}/nprpc_shm.node
#   )
# endif()

# Installation
if(NPRPC_INSTALL)
  include(GNUInstallDirs)
  include(CMakePackageConfigHelpers)

  # Install library
  install(TARGETS nprpc
    EXPORT nprpcTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  )

  # Install headers
  install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
  )

  # Install tools
  if(NPRPC_BUILD_TOOLS)
    install(TARGETS npnameserver npidl
      RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
  endif()

  # Export targets
  install(EXPORT nprpcTargets
    FILE nprpcTargets.cmake
    NAMESPACE nprpc::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/nprpc
  )

  # Create package config
  configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/nprpcConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/nprpcConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/nprpc
  )

  # Create version file
  write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/nprpcConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
  )

  # Install config files
  install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/nprpcConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/nprpcConfigVersion.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/npidl.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/nprpc
  )
endif()

cmake_minimum_required(VERSION 3.15)

# Determine if nprpc is built as a subproject (using add_subdirectory) or standalone
if(NOT DEFINED PROJECT_NAME)
  set(NPRPC_IS_TOPLEVEL_PROJECT ON)
else()
  set(NPRPC_IS_TOPLEVEL_PROJECT OFF)
endif()

project(nprpc 
  VERSION 1.0.0
  DESCRIPTION "Multi-Transport RPC Framework"
  LANGUAGES CXX
)

# C++ Standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options
option(NPRPC_BUILD_TOOLS "Build npidl tool and nameserver" ${NPRPC_IS_TOPLEVEL_PROJECT})
option(NPRPC_BUILD_TESTS "Build tests" ${NPRPC_IS_TOPLEVEL_PROJECT})
option(NPRPC_BUILD_JS "Build JavaScript bindings" ${NPRPC_IS_TOPLEVEL_PROJECT})
option(NPRPC_INSTALL "Generate install target" ${NPRPC_IS_TOPLEVEL_PROJECT})
option(BUILD_SHARED_LIBS "Build shared libraries" ON)

# Find dependencies
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

# Add subdirectories
if(NPRPC_BUILD_TOOLS)
  add_subdirectory(npidl)
endif()

# Include IDL generation functions
include(cmake/npidl.cmake)

# Generate IDL files if npidl is available
set(nprpc_stub_GENERATED_SOURCES "")
if(TARGET npidl)
  set(IDL_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/idl/nprpc_base.npidl
    ${CMAKE_CURRENT_SOURCE_DIR}/idl/nprpc_nameserver.npidl
  )

  npidl_generate_idl_files("${IDL_FILES}" nprpc_stub)

  # Copy generated headers to include directory
  foreach(file ${nprpc_stub_GENERATED_HEADERS})
    get_filename_component(filename ${file} NAME)
    add_custom_command(
      OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/include/${filename}
      DEPENDS ${file}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_SOURCE_DIR}/include
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${file} ${CMAKE_CURRENT_SOURCE_DIR}/include
      COMMENT "Copying ${filename} to include directory"
    )
    list(APPEND nprpc_stub_COPIED_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/include/${filename})
  endforeach()
  
  # Add a custom target for the copied headers
  add_custom_target(nprpc_copy_headers
    DEPENDS ${nprpc_stub_COPIED_HEADERS}
  )
else()
  # Use pre-generated files if npidl is not built
  message(STATUS "npidl not available - using pre-generated IDL files")
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/include/nprpc_base.hpp AND 
     EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/include/nprpc_nameserver.hpp)
    message(STATUS "Found pre-generated IDL files in include/")
    set(nprpc_stub_GENERATED_SOURCES
      ${CMAKE_CURRENT_SOURCE_DIR}/include/nprpc_base.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/include/nprpc_nameserver.cpp
    )
  else()
    message(FATAL_ERROR "No pre-generated IDL files found. Please build with NPRPC_BUILD_TOOLS=ON first to generate them.")
  endif()
endif()

# Main library target
add_library(nprpc
  src/http_server.cpp
  ${nprpc_stub_GENERATED_SOURCES}
  src/framework.h
  src/nprpc.cpp
  src/rpc_impl.cpp
  src/lock_free_ring_buffer.cpp
  src/shared_memory_channel.cpp
  src/shared_memory_connection.cpp
  src/shared_memory_listener.cpp
  src/shared_memory_server_session.cpp
  src/shared_memory_init.cpp
  src/async_connect.cpp
  src/server_session_socket.cpp
  src/server_session_websocket.cpp
  src/client_websocket_connection.cpp
  src/websocket_session.cpp
  src/client_socket_connection.cpp
  src/session_context.cpp
  src/session.cpp
  src/udp_connection.cpp
  src/udp_listener.cpp
  src/uuid.cpp
  src/helper.hpp
  src/helper.cpp
  # Public headers
  include/nprpc_base.hpp
  include/nprpc_nameserver.hpp
  include/nprpc/common.hpp
  include/nprpc/basic.hpp
  include/nprpc/buffer.hpp
  include/nprpc/endpoint.hpp
  include/nprpc/exception.hpp
  include/nprpc/export.hpp
  include/nprpc/flat.hpp
  include/nprpc/nprpc.hpp
  include/nprpc/object_ptr.hpp
  include/nprpc/session_context.h
  include/nprpc/utils.hpp
  include/nprpc/host_json.hpp
  include/nprpc/impl/async_connect.hpp
  include/nprpc/impl/nprpc_impl.hpp
  include/nprpc/impl/session.hpp
  include/nprpc/impl/udp_connection.hpp
  include/nprpc/impl/udp_listener.hpp
  include/nprpc/impl/uuid.hpp
  include/nprpc/impl/websocket_session.hpp
  include/nprpc/serialization/iarchive.h
  include/nprpc/serialization/nvp.hpp
  include/nprpc/serialization/oarchive.h
  include/nprpc/serialization/serialization.h
)

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
)

# Platform-specific settings
if(WIN32)
  target_link_libraries(nprpc PUBLIC crypt32)
endif()

# Ensure export macro is defined when building the library so symbols are exported
target_compile_definitions(nprpc PRIVATE NPRPC_EXPORTS)

# Dependencies for generated files
if(TARGET npidl)
  add_dependencies(nprpc nprpc_stub_gen)
endif()

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
  
  target_link_libraries(npnameserver PRIVATE nprpc::nprpc)
  
  if(TARGET npidl)
    add_dependencies(npnameserver nprpc_stub_gen)
  endif()
endif()

# Optional components
if(NPRPC_BUILD_TOOLS)
  add_subdirectory(check)
endif()

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
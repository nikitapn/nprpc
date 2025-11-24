# NPRPC Performance Benchmarks

# Find Google Benchmark
find_package(benchmark QUIET)

if(NOT benchmark_FOUND)
  message(STATUS "Google Benchmark not found - benchmarks will not be built")
  message(STATUS "Install with: sudo apt install libbenchmark-dev (Ubuntu/Debian)")
  return()
endif()

# Find gRPC using pkg-config (avoids CMake target conflicts with glaze/protobuf)
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(GRPC QUIET grpc++)
  pkg_check_modules(PROTOBUF QUIET protobuf)
  
  if(GRPC_FOUND AND PROTOBUF_FOUND)
    set(GRPC_AVAILABLE TRUE)
    message(STATUS "gRPC found via pkg-config - gRPC comparison benchmarks enabled")
    
    # Locate tools
    find_program(PROTOC_EXECUTABLE protoc)
    find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin)
    
    if(NOT PROTOC_EXECUTABLE OR NOT GRPC_CPP_PLUGIN)
      message(WARNING "protoc or grpc_cpp_plugin not found, gRPC benchmarks disabled")
      set(GRPC_AVAILABLE FALSE)
    endif()
  else()
    set(GRPC_AVAILABLE FALSE)
    message(STATUS "gRPC not found - only NPRPC benchmarks will be built")
    message(STATUS "Install with: sudo pacman -S grpc (Arch) or sudo apt install libgrpc++-dev (Ubuntu/Debian)")
  endif()
else()
  set(GRPC_AVAILABLE FALSE)
  message(STATUS "pkg-config not found - gRPC benchmarks disabled")
endif()

# Generate benchmark IDL files (reuse test interfaces)
include(../cmake/npidl.cmake)

set(BENCHMARK_IDL_FILES 
  ${CMAKE_CURRENT_SOURCE_DIR}/idl/nprpc_benchmark.npidl
)

npidl_generate_idl_files("${BENCHMARK_IDL_FILES}" nprpc_benchmark_stub)

# Generate gRPC files if available
if(GRPC_AVAILABLE)
  set(PROTO_FILES ${CMAKE_CURRENT_SOURCE_DIR}/idl/benchmark.proto)
  set(PROTO_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/grpc_generated)
  file(MAKE_DIRECTORY ${PROTO_OUTPUT_DIR})

  # Generate C++ files from .proto
  set(PROTO_SRCS ${PROTO_OUTPUT_DIR}/benchmark.pb.cc)
  set(PROTO_HDRS ${PROTO_OUTPUT_DIR}/benchmark.pb.h)
  set(GRPC_SRCS ${PROTO_OUTPUT_DIR}/benchmark.grpc.pb.cc)
  set(GRPC_HDRS ${PROTO_OUTPUT_DIR}/benchmark.grpc.pb.h)

  add_custom_command(
    OUTPUT ${PROTO_SRCS} ${PROTO_HDRS} ${GRPC_SRCS} ${GRPC_HDRS}
    COMMAND ${PROTOC_EXECUTABLE}
    ARGS --grpc_out=${PROTO_OUTPUT_DIR}
         --cpp_out=${PROTO_OUTPUT_DIR}
         -I${CMAKE_CURRENT_SOURCE_DIR}/idl
         --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}
         ${PROTO_FILES}
    DEPENDS ${PROTO_FILES}
    COMMENT "Generating gRPC files"
  )

  add_custom_target(grpc_benchmark_gen 
    DEPENDS ${PROTO_SRCS} ${GRPC_SRCS}
  )
endif()

# Benchmark server executable
add_executable(benchmark_server
  src/server.cpp
  ${nprpc_benchmark_stub_GENERATED_SOURCES}
)

add_dependencies(benchmark_server nprpc_benchmark_stub_gen)
if(TARGET npnameserver)
  add_dependencies(benchmark_server npnameserver)
endif()

target_include_directories(benchmark_server
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${nprpc_benchmark_stub_INCLUDE_DIR}
)

target_link_libraries(benchmark_server
  PRIVATE
    nprpc::nprpc
)

# gRPC benchmark server (if gRPC is available)
if(GRPC_AVAILABLE)
  add_executable(grpc_benchmark_server
    src/grpc_server.cpp
    ${PROTO_SRCS}
    ${GRPC_SRCS}
  )

  add_dependencies(grpc_benchmark_server grpc_benchmark_gen)

  target_include_directories(grpc_benchmark_server
    PRIVATE
      ${PROTO_OUTPUT_DIR}
      ${GRPC_INCLUDE_DIRS}
      ${PROTOBUF_INCLUDE_DIRS}
  )

  target_link_libraries(grpc_benchmark_server
    PRIVATE
      ${GRPC_LIBRARIES}
      ${PROTOBUF_LIBRARIES}
  )
endif()

# Main benchmark executable
set(BENCHMARK_SOURCES
  src/benchmark_main.cpp
  src/benchmark_latency.cpp
  # src/benchmark_throughput.cpp
  # src/benchmark_bandwidth.cpp
  ${nprpc_benchmark_stub_GENERATED_SOURCES}
)

# Add gRPC benchmarks if available
if(GRPC_AVAILABLE)
  list(APPEND BENCHMARK_SOURCES 
    src/benchmark_grpc_latency.cpp
    ${PROTO_SRCS}
    ${GRPC_SRCS}
  )
endif()

add_executable(nprpc_benchmarks ${BENCHMARK_SOURCES})

add_dependencies(nprpc_benchmarks nprpc_benchmark_stub_gen benchmark_server)
if(TARGET npnameserver)
  add_dependencies(nprpc_benchmarks npnameserver)
endif()
if(GRPC_AVAILABLE)
  add_dependencies(nprpc_benchmarks grpc_benchmark_gen grpc_benchmark_server)
endif()

target_include_directories(nprpc_benchmarks
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${nprpc_benchmark_stub_INCLUDE_DIR}
)

if(GRPC_AVAILABLE)
  target_include_directories(nprpc_benchmarks
    PRIVATE
      ${PROTO_OUTPUT_DIR}
      ${GRPC_INCLUDE_DIRS}
      ${PROTOBUF_INCLUDE_DIRS}
  )
endif()

set(BENCHMARK_LIBRARIES
  nprpc::nprpc
  benchmark::benchmark
)

if(GRPC_AVAILABLE)
  list(APPEND BENCHMARK_LIBRARIES
    ${GRPC_LIBRARIES}
    ${PROTOBUF_LIBRARIES}
  )
endif()

target_link_libraries(nprpc_benchmarks
  PRIVATE
    ${BENCHMARK_LIBRARIES}
)

# Custom target to run benchmarks
add_custom_target(run_benchmarks
  COMMAND nprpc_benchmarks --benchmark_counters_tabular=true
  DEPENDS nprpc_benchmarks benchmark_server npnameserver
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  COMMENT "Running NPRPC performance benchmarks..."
)

# Target to run benchmarks and save results
add_custom_target(run_benchmarks_save
  COMMAND nprpc_benchmarks 
    --benchmark_counters_tabular=true
    --benchmark_out=benchmark_results.json
    --benchmark_out_format=json
  DEPENDS nprpc_benchmarks benchmark_server npnameserver
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  COMMENT "Running NPRPC benchmarks and saving results to benchmark_results.json..."
)

message(STATUS "Google Benchmark found - benchmarks enabled")

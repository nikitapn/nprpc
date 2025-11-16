# NPRPC Performance Benchmarks

# Find Google Benchmark
find_package(benchmark QUIET)

if(NOT benchmark_FOUND)
  message(STATUS "Google Benchmark not found - benchmarks will not be built")
  message(STATUS "Install with: sudo apt install libbenchmark-dev (Ubuntu/Debian)")
  return()
endif()

# Generate benchmark IDL files (reuse test interfaces)
include(../cmake/npidl.cmake)

set(BENCHMARK_IDL_FILES 
  ${CMAKE_CURRENT_SOURCE_DIR}/idl/nprpc_benchmark.npidl
)

npidl_generate_idl_files("${BENCHMARK_IDL_FILES}" nprpc_benchmark_stub)

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

# Main benchmark executable
add_executable(nprpc_benchmarks
  src/benchmark_main.cpp
  src/benchmark_latency.cpp
  # src/benchmark_throughput.cpp
  # src/benchmark_bandwidth.cpp
  ${nprpc_benchmark_stub_GENERATED_SOURCES}
)

add_dependencies(nprpc_benchmarks nprpc_benchmark_stub_gen benchmark_server)
if(TARGET npnameserver)
  add_dependencies(nprpc_benchmarks npnameserver)
endif()

target_include_directories(nprpc_benchmarks
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${nprpc_benchmark_stub_INCLUDE_DIR}
)

target_link_libraries(nprpc_benchmarks
  PRIVATE
    nprpc::nprpc
    benchmark::benchmark
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

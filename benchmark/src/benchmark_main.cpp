// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// NPRPC Performance Benchmarks - Main Entry Point

#include "common.hpp"

#include <benchmark/benchmark.h>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <signal.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#include "common.hpp"

nprpc::Rpc* g_rpc;

using thread_pool = nprpc::thread_pool_1;
namespace fs = std::filesystem;

// Global test infrastructure
namespace nprpc::benchmark {

// Server process manager
class BenchmarkServerManager
{
  pid_t nprpc_server_pid_ = -1;
  pid_t grpc_server_pid_ = -1;
  pid_t capnp_server_pid_ = -1;

public:
  bool start_nprpc_server(fs::path server_path)
  {
    std::cout << "Starting NPRPC benchmark server...\n";

    if (!fs::exists(server_path)) {
      std::cerr << "ERROR: benchmark_server executable not found at: "
                << server_path << "\n";
      std::cerr << "Current path: " << fs::current_path() << "\n";
      return false;
    }

    // Fork a child process to run the server
    nprpc_server_pid_ = fork();

    if (nprpc_server_pid_ == -1) {
      std::cerr << "ERROR: Failed to fork NPRPC server process\n";
      return false;
    } else if (nprpc_server_pid_ == 0) {
      // Child process - run the server
      execl(server_path.c_str(), "benchmark_server", nullptr);

      // If exec fails
      std::cerr << "ERROR: Failed to execute benchmark_server\n";
      _exit(1);
    } else {
      // Parent process - wait for server to be ready
      std::cout << "NPRPC benchmark server started with PID: "
                << nprpc_server_pid_ << "\n";
      std::cout << "Waiting for server to initialize...\n";
      std::this_thread::sleep_for(std::chrono::seconds(2));

      // Check if the child process is still alive
      int status;
      pid_t result = waitpid(nprpc_server_pid_, &status, WNOHANG);
      if (result != 0) {
        std::cerr << "ERROR: NPRPC server process failed to start\n";
        nprpc_server_pid_ = -1;
        return false;
      }

      std::cout << "NPRPC server ready\n";
      return true;
    }
  }

  bool start_grpc_server(fs::path server_path)
  {
    std::cout << "Starting gRPC benchmark server...\n";

    if (!fs::exists(server_path)) {
      std::cout << "WARNING: grpc_benchmark_server not found, gRPC benchmarks "
                   "will be skipped\n";
      return false;
    }

    // Fork a child process to run the server
    grpc_server_pid_ = fork();

    if (grpc_server_pid_ == -1) {
      std::cerr << "ERROR: Failed to fork gRPC server process\n";
      return false;
    } else if (grpc_server_pid_ == 0) {
      // Child process - run the server
      // Redirect output to avoid cluttering benchmark output
      freopen("/dev/null", "w", stdout);
      freopen("/dev/null", "w", stderr);
      execl(server_path.c_str(), "grpc_benchmark_server", nullptr);
      _exit(1);
    } else {
      std::cout << "gRPC benchmark server started with PID: "
                << grpc_server_pid_ << "\n";
      std::this_thread::sleep_for(std::chrono::seconds(1));

      // Check if still alive
      int status;
      pid_t result = waitpid(grpc_server_pid_, &status, WNOHANG);
      if (result != 0) {
        std::cerr << "ERROR: gRPC server process failed to start\n";
        grpc_server_pid_ = -1;
        return false;
      }

      std::cout << "gRPC server ready\n";
      return true;
    }
  }

  void stop_nprpc_server()
  {
    if (nprpc_server_pid_ <= 0)
      return;

    std::cout << "\nShutting down NPRPC benchmark server...\n";

    try {
      auto control =
          get_object<nprpc::benchmark::ServerControl>("nprpc_server_control");
      control->Shutdown();
      std::cout << "Graceful shutdown signal sent\n";

      // Wait up to 3 seconds for graceful shutdown
      for (int i = 0; i < 30; ++i) {
        int status;
        pid_t result = waitpid(nprpc_server_pid_, &status, WNOHANG);
        if (result != 0) {
          std::cout << "NPRPC server shut down gracefully\n";
          nprpc_server_pid_ = -1;
          return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    } catch (...) {
      // Graceful shutdown failed, continue to forceful shutdown
    }

    // Force shutdown if graceful failed
    std::cout << "Forcing NPRPC server shutdown with SIGTERM...\n";
    kill(nprpc_server_pid_, SIGTERM);

    int status;
    waitpid(nprpc_server_pid_, &status, 0);
    nprpc_server_pid_ = -1;
    std::cout << "NPRPC server stopped\n";
  }

  void stop_grpc_server()
  {
    if (grpc_server_pid_ <= 0)
      return;

    std::cout << "Shutting down gRPC benchmark server...\n";
    kill(grpc_server_pid_, SIGTERM);

    int status;
    waitpid(grpc_server_pid_, &status, 0);
    grpc_server_pid_ = -1;
    std::cout << "gRPC server stopped\n";
  }

  bool start_capnp_server(fs::path server_path)
  {
    std::cout << "Starting Cap'n Proto benchmark server...\n";

    if (!fs::exists(server_path)) {
      std::cout << "WARNING: capnp_benchmark_server not found, Cap'n Proto "
                   "benchmarks will be skipped\n";
      return false;
    }

    // Fork a child process to run the server
    capnp_server_pid_ = fork();

    if (capnp_server_pid_ == -1) {
      std::cerr << "ERROR: Failed to fork Cap'n Proto server process\n";
      return false;
    } else if (capnp_server_pid_ == 0) {
      // Child process - run the server
      freopen("/dev/null", "w", stdout);
      freopen("/dev/null", "w", stderr);
      execl(server_path.c_str(), "capnp_benchmark_server", "localhost:50052",
            nullptr);
      _exit(1);
    } else {
      std::cout << "Cap'n Proto benchmark server started with PID: "
                << capnp_server_pid_ << "\n";
      std::this_thread::sleep_for(std::chrono::seconds(1));

      // Check if still alive
      int status;
      pid_t result = waitpid(capnp_server_pid_, &status, WNOHANG);
      if (result != 0) {
        std::cerr << "ERROR: Cap'n Proto server process failed to start\n";
        capnp_server_pid_ = -1;
        return false;
      }

      std::cout << "Cap'n Proto server ready\n";
      return true;
    }
  }

  void stop_capnp_server()
  {
    if (capnp_server_pid_ <= 0)
      return;

    std::cout << "Shutting down Cap'n Proto benchmark server...\n";
    kill(capnp_server_pid_, SIGTERM);

    int status;
    waitpid(capnp_server_pid_, &status, 0);
    capnp_server_pid_ = -1;
    std::cout << "Cap'n Proto server stopped\n";
  }

  ~BenchmarkServerManager()
  {
    stop_nprpc_server();
    stop_grpc_server();
    stop_capnp_server();
  }
};

} // namespace nprpc::benchmark

int main(int argc, char** argv)
{
  if (argc == 0 || argv[0] == nullptr)
    return 1;

  fs::path exe_directory = fs::absolute(fs::path(argv[0])).parent_path();
  std::cout << "Executable directory: " << exe_directory << std::endl;

  ::benchmark::Initialize(&argc, argv);

  if (::benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }

  // Setup: Start the benchmark servers before running benchmarks
  std::cout << "\n=== NPRPC Benchmark Environment Setup ===\n";

  // Initialize global RPC instance
  g_rpc = nprpc::RpcBuilder().build(thread_pool::get_instance().ctx());

  nprpc::benchmark::BenchmarkServerManager server;

  if (!server.start_nprpc_server(exe_directory / "benchmark_server")) {
    std::cerr << "FATAL: Failed to start NPRPC benchmark server\n";
    return 1;
  }

  // Start gRPC server (optional - will skip gRPC benchmarks if not available)
  server.start_grpc_server(exe_directory / "grpc_benchmark_server");

  // Start Cap'n Proto server (optional - will skip Cap'n Proto benchmarks if
  // not available)
  server.start_capnp_server(exe_directory / "capnp_benchmark_server");

  std::cout << "=== Setup Complete ===\n\n";

  // Run all benchmarks
  ::benchmark::RunSpecifiedBenchmarks();
  ::benchmark::Shutdown();

  // Teardown: Servers will be stopped
  std::cout << "\n=== NPRPC Benchmark Environment Teardown ===\n";
  server.stop_nprpc_server();
  server.stop_grpc_server();
  server.stop_capnp_server();

  if (g_rpc) {
    thread_pool::get_instance().stop();
    g_rpc->destroy();
    g_rpc = nullptr;
  }

  std::cout << "=== Teardown Complete ===\n";

  return 0;
}

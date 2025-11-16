// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// NPRPC Performance Benchmarks - Main Entry Point

#include "common.hpp"

#include <benchmark/benchmark.h>

#include <iostream>
#include <thread>
#include <chrono>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <filesystem>

nprpc::Rpc* g_rpc;

using thread_pool = nprpc::thread_pool_1;

// Global test infrastructure
namespace nprpc::benchmark {

// Server process manager
class BenchmarkServerManager {
  pid_t server_pid_ = -1;
  
public:
  bool start_server() {
    std::cout << "Starting benchmark server...\n";
    
    // Find the server executable
    std::filesystem::path server_path = std::filesystem::current_path() / "benchmark_server";
    
    if (!std::filesystem::exists(server_path)) {
      // Try one level up (if running from build/benchmark)
      server_path = std::filesystem::current_path().parent_path() / "benchmark" / "benchmark_server";
    }
    
    if (!std::filesystem::exists(server_path)) {
      std::cerr << "ERROR: benchmark_server executable not found at: " << server_path << "\n";
      std::cerr << "Current path: " << std::filesystem::current_path() << "\n";
      return false;
    }
    
    // Fork a child process to run the server
    server_pid_ = fork();
    
    if (server_pid_ == -1) {
      std::cerr << "ERROR: Failed to fork server process\n";
      return false;
    } else if (server_pid_ == 0) {
      // Child process - run the server
      execl(server_path.c_str(), "benchmark_server", nullptr);
      
      // If exec fails
      std::cerr << "ERROR: Failed to execute benchmark_server\n";
      _exit(1);
    } else {
      // Parent process - wait for server to be ready
      std::cout << "Benchmark server started with PID: " << server_pid_ << "\n";
      std::cout << "Waiting for server to initialize...\n";
      std::this_thread::sleep_for(std::chrono::seconds(2));
      
      // Check if the child process is still alive
      int status;
      pid_t result = waitpid(server_pid_, &status, WNOHANG);
      if (result != 0) {
        std::cerr << "ERROR: Server process failed to start\n";
        server_pid_ = -1;
        return false;
      }
      
      std::cout << "Server ready for benchmarks\n";
      return true;
    }
  }
  
  void stop_server() {
    if (server_pid_ <= 0) return;
    
    std::cout << "\nShutting down benchmark server...\n";
    
    try {
      // Try graceful shutdown via RPC first
      auto nameserver = g_rpc->get_nameserver("127.0.0.1");
      
      nprpc::Object* obj = nullptr;
      if (!nameserver->Resolve("nprpc_server_control", obj)) {
        auto control = nprpc::narrow<nprpc::benchmark::ServerControl>(obj);
        if (control) {
          control->Shutdown();
          std::cout << "Graceful shutdown signal sent\n";
          
          // Wait up to 3 seconds for graceful shutdown
          for (int i = 0; i < 30; ++i) {
            int status;
            pid_t result = waitpid(server_pid_, &status, WNOHANG);
            if (result != 0) {
              std::cout << "Server shut down gracefully\n";
              server_pid_ = -1;
              return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
          }
        }
      }
    } catch (...) {
      // Graceful shutdown failed, continue to forceful shutdown
    }
    
    // Force shutdown if graceful failed
    std::cout << "Forcing server shutdown with SIGTERM...\n";
    kill(server_pid_, SIGTERM);
    
    // Wait for the process to terminate
    int status;
    waitpid(server_pid_, &status, 0);
    server_pid_ = -1;
    std::cout << "Server stopped\n";
  }
  
  ~BenchmarkServerManager() {
    stop_server();
  }
};

} // namespace nprpc::benchmark

int main(int argc, char** argv) {
  ::benchmark::Initialize(&argc, argv);
  
  if (::benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  
  // Setup: Start the benchmark server before running benchmarks
  std::cout << "\n=== NPRPC Benchmark Environment Setup ===\n";
  
  // Initialize global RPC instance
  g_rpc = nprpc::RpcBuilder().build(thread_pool::get_instance().ctx());
  
  nprpc::benchmark::BenchmarkServerManager server;
  
  if (!server.start_server()) {
    std::cerr << "FATAL: Failed to start benchmark server\n";
    return 1;
  }

  std::cout << "=== Setup Complete ===\n\n";

  // Run all benchmarks
  ::benchmark::RunSpecifiedBenchmarks();
  ::benchmark::Shutdown();

  // Teardown: Server will be stopped by destructor
  std::cout << "\n=== NPRPC Benchmark Environment Teardown ===\n";
  // server.stop_server() will be called by destructor

  if (g_rpc) {
    thread_pool::get_instance().stop();
    g_rpc->destroy();
    g_rpc = nullptr;
  }
  
  std::cout << "=== Teardown Complete ===\n";
  
  return 0;
}

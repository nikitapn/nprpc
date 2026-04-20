#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <signal.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#include <nprpc_benchmark.hpp>
#include <nprpc_nameserver.hpp>

using namespace std::string_literals;
using namespace std::string_view_literals;

#define LOG_PREFIX "[benchmark_server] "

// Helper class to manage nameserver process
class NameserverManager
{
  pid_t nameserver_pid = -1;

public:
  bool start_nameserver()
  {
    // Fork a child process to run the nameserver
    nameserver_pid = fork();

    if (nameserver_pid == -1) {
      std::cerr << LOG_PREFIX "Failed to fork nameserver process" << std::endl;
      return false;
    } else if (nameserver_pid == 0) {
      // Child process - run the nameserver
      // Try to find npnameserver in the build directory
      execl("/home/nikita/projects/nprpc/.build_release/npnameserver",
            "npnameserver", nullptr);
      execl("/home/nikita/projects/nprpc/.build_relwith_debinfo/npnameserver",
            "npnameserver", nullptr);
      execl("/home/nikita/projects/nprpc/.build_debug/npnameserver",
            "npnameserver", nullptr);

      // If all fail, exit with error
      std::cerr << LOG_PREFIX "Failed to execute npnameserver" << std::endl;
      _exit(1);
    } else {
      // Parent process - wait a bit for nameserver to start
      std::this_thread::sleep_for(std::chrono::milliseconds(500));

      // Check if the child process is still alive
      int status;
      pid_t result = waitpid(nameserver_pid, &status, WNOHANG);
      if (result != 0) {
        std::cerr << LOG_PREFIX "Nameserver process failed to start"
                  << std::endl;
        nameserver_pid = -1;
        return false;
      }

      std::cout << LOG_PREFIX "Nameserver started with PID: " << nameserver_pid
                << std::endl;
      return true;
    }
  }

  void stop_nameserver()
  {
    if (nameserver_pid > 0) {
      // std::cout << LOG_PREFIX "Stopping nameserver with PID: " << nameserver_pid
                // << std::endl;
      kill(nameserver_pid, SIGTERM);

      // Wait for the process to terminate
      int status;
      waitpid(nameserver_pid, &status, 0);
      nameserver_pid = -1;
    }
  }

  ~NameserverManager() { stop_nameserver(); }
};

nprpc::Rpc* rpc;
nprpc::Poa* poa;
NameserverManager nameserver_manager;

class Environment
{
public:
  void SetUp()
  {
    // Start the nameserver first
    if (!nameserver_manager.start_nameserver()) {
      std::cerr << LOG_PREFIX "Failed to start nameserver process\n";
      std::exit(1);
    }

    try {
      // Use the new RpcBuilder API
      const bool use_epoll = ::getenv("NPRPC_EPOLL") != nullptr;
      const bool use_uring  = ::getenv("NPRPC_URING")  != nullptr;
      const char* http_root_dir = ::getenv("NPRPC_HTTP_ROOT_DIR");
      const char* shm_egress_env = ::getenv("NPRPC_BENCH_SHM_EGRESS");

      auto parse_port = [](const char* env, uint16_t fallback) -> uint16_t {
        if (env && *env) {
          long v = std::strtol(env, nullptr, 10);
          if (v > 0 && v < 65536) return static_cast<uint16_t>(v);
        }
        return fallback;
      };
      const uint16_t http_port = parse_port(::getenv("NPRPC_BENCH_HTTP_PORT"), 22223);
      const uint16_t quic_port = parse_port(::getenv("NPRPC_BENCH_QUIC_PORT"), 22225);

      auto builder = nprpc::RpcBuilder()
                .set_log_level(nprpc::LogLevel::error)
                .with_hostname("localhost")
                .enable_ssl_client_self_signed_cert("/home/nikita/projects/nprpc/certs/out/localhost.crt")
                .with_tcp(22222).with_epoll_if(use_epoll).with_uring_if(use_uring)
                .with_quic(quic_port)
                  .ssl("/home/nikita/projects/nprpc/certs/out/localhost.crt",
                       "/home/nikita/projects/nprpc/certs/out/localhost.key")
                .with_http(http_port)
                  .root_dir(http_root_dir && *http_root_dir
                        ? http_root_dir
                        : "/home/nikita/projects/nprpc/test/http")
                  .ssl("/home/nikita/projects/nprpc/certs/out/localhost.crt",
                       "/home/nikita/projects/nprpc/certs/out/localhost.key")
                  .enable_http3()
                  .http3_workers(8);

      if (shm_egress_env && *shm_egress_env)
        builder.http3_shm_egress_channel(shm_egress_env);

      rpc = builder.build();

      rpc->start_thread_pool(1);

      // Use the new PoaBuilder API
      poa = rpc->create_poa()
                .with_max_objects(128)
                .with_lifespan(nprpc::PoaPolicy::Lifespan::Persistent)
                .build();

    } catch (nprpc::Exception& ex) {
      nameserver_manager.stop_nameserver();
      std::cerr << LOG_PREFIX "Failed to set up RPC environment: " << ex.what()
                << std::endl;
      std::exit(1);
    }
  }

  void TearDown()
  {
    if (rpc) {
      rpc->destroy();
    }
    // Stop the nameserver
    nameserver_manager.stop_nameserver();
  }
};

std::condition_variable cv;
std::mutex cv_m;
bool shutdown_requested = false;

class ServerControlImpl : public ::nprpc::benchmark::IServerControl_Servant
{
  void Shutdown() override
  {
    // std::cout << LOG_PREFIX "Shutdown requested" << std::endl;
    {
      std::lock_guard<std::mutex> lk(cv_m);
      shutdown_requested = true;
    }
    cv.notify_one();
  }
};

class BenchmarkServerImpl : public ::nprpc::benchmark::IBenchmark_Servant
{
  void Ping() override {}
  uint32_t Func1(uint32_t a, uint32_t b) override { return a + b; }
  void Func2(::nprpc::flat::Span<char> data) override {}

  ::nprpc::benchmark::Employee
  ProcessEmployee(::nprpc::benchmark::flat::Employee_Direct employee) override
  {
    nprpc::benchmark::Employee result;
    nprpc::benchmark::helper::assign_from_flat_ProcessEmployee_employee(
        employee, result);
    return result;
  }

  void ProcessLargeData(
      ::nprpc::flat::Span<uint8_t> data,
      ::nprpc::flat::Vector_Direct1<uint8_t> result) override
  {
    result.length(data.size());
    auto span = result();
    std::memcpy(span.data(), data.data(), data.size());
  }
};

int main(int argc, char** argv)
{
  Environment env;
  env.SetUp();

  constexpr auto flags = nprpc::ObjectActivationFlags::all;

  auto nameserver = rpc->get_nameserver("127.0.0.1");

  ServerControlImpl server_control;
  auto oid = poa->activate_object(&server_control, flags);
  nameserver->Bind(oid, "nprpc_server_control");

  BenchmarkServerImpl benchmark_server;
  oid = poa->activate_object(&benchmark_server, flags);
  nameserver->Bind(oid, "nprpc_benchmark");

  // Capture interrupt signal to allow graceful shutdown
  signal(SIGINT, [](int signum) {
    std::cout << LOG_PREFIX "Interrupt signal (" << signum << ") received."
              << std::endl;
    {
      std::lock_guard<std::mutex> lk(cv_m);
      shutdown_requested = true;
    }
    cv.notify_one();
  });

  std::cout << LOG_PREFIX "NPRPC Benchmark Server is running" << std::endl;

  // Wait for shutdown signal from JavaScript client
  std::unique_lock<std::mutex> lk(cv_m);
  cv.wait(lk, [] { return shutdown_requested; });

  // std::cout << LOG_PREFIX "Server shutting down..." << std::endl;

  // Give some time for the client to receive the response
  std::this_thread::sleep_for(std::chrono::seconds(1));

  env.TearDown();

  return 0;
}

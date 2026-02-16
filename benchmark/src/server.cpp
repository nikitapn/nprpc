#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <future>
#include <iostream>
#include <signal.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#include <nprpc/impl/misc/thread_pool.hpp>
#include <nprpc_benchmark.hpp>
#include <nprpc_nameserver.hpp>

using namespace std::string_literals;
using namespace std::string_view_literals;

using thread_pool = nprpc::thread_pool_4;

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
      std::cout << LOG_PREFIX "Stopping nameserver with PID: " << nameserver_pid
                << std::endl;
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
      rpc = nprpc::RpcBuilder()
                .set_log_level(nprpc::LogLevel::error)
                .with_hostname("localhost")
                .with_tcp(22222)
                .with_http(22223)
                .ssl("/home/nikita/projects/nprpc/certs/out/localhost.crt",
                     "/home/nikita/projects/nprpc/certs/out/localhost.key")
                .with_udp(22224)
                .with_quic(22225)
                .ssl("/home/nikita/projects/nprpc/certs/out/localhost.crt",
                     "/home/nikita/projects/nprpc/certs/out/localhost.key")
                .enable_ssl_client_self_signed_cert(
                    "/home/nikita/projects/nprpc/certs/out/localhost.crt")
                .build(thread_pool::get_instance().ctx());

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
    thread_pool::get_instance().stop();
    if (rpc) {
      thread_pool::get_instance().stop();
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
    std::cout << LOG_PREFIX "Shutdown requested" << std::endl;
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

  std::vector<uint8_t>
  ProcessLargeData(::nprpc::flat::Span<uint8_t> data) override
  {
    // Echo back the data
    std::vector<uint8_t> result(data.size());
    std::memcpy(result.data(), data.data(), data.size());
    return result;
  }
};

class BenchmarkUDPImpl : public ::nprpc::benchmark::IBenchmarkUDP_Servant
{
  void Ping() override {}
};

int main(int argc, char** argv)
{
  Environment env;
  env.SetUp();

  constexpr auto flags = nprpc::ObjectActivationFlags::ALLOW_ALL;

  auto nameserver = rpc->get_nameserver("127.0.0.1");

  ServerControlImpl server_control;
  auto oid = poa->activate_object(&server_control, flags);
  nameserver->Bind(oid, "nprpc_server_control");

  BenchmarkServerImpl benchmark_server;
  oid = poa->activate_object(&benchmark_server, flags);
  nameserver->Bind(oid, "nprpc_benchmark");

  BenchmarkUDPImpl benchmark_udp;
  oid = poa->activate_object(&benchmark_udp,
                             nprpc::ObjectActivationFlags::ALLOW_UDP);
  nameserver->Bind(oid, "nprpc_benchmark_udp");

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

  std::cout << LOG_PREFIX "Server shutting down..." << std::endl;

  // Give some time for the client to receive the response
  std::this_thread::sleep_for(std::chrono::seconds(1));

  env.TearDown();

  return 0;
}

// Cap'n Proto benchmark server
#include "benchmark.capnp.h"
#include <capnp/ez-rpc.h>
#include <csignal>
#include <iostream>
#include <kj/debug.h>

class BenchmarkServiceImpl final : public BenchmarkService::Server
{
protected:
  kj::Promise<void> emptyCall(EmptyCallContext context) override
  {
    return kj::READY_NOW;
  }

  kj::Promise<void> callWithReturn(CallWithReturnContext context) override
  {
    context.getResults().setResult(42);
    return kj::READY_NOW;
  }

  kj::Promise<void> smallStringCall(SmallStringCallContext context) override
  {
    auto params = context.getParams();
    auto input = params.getData();
    context.getResults().setResult(input);
    return kj::READY_NOW;
  }

  kj::Promise<void> processEmployee(ProcessEmployeeContext context) override
  {
    auto params = context.getParams();
    auto employee = params.getEmployee();

    // Echo back the employee data
    auto results = context.getResults();
    results.setResult(employee);

    return kj::READY_NOW;
  }

  kj::Promise<void> processLargeData(ProcessLargeDataContext context) override
  {
    auto params = context.getParams();
    auto data = params.getData();

    // Echo back the data
    context.getResults().setResult(data);

    return kj::READY_NOW;
  }
};

static kj::Own<kj::PromiseFulfiller<void>> g_shutdownFulfiller;

void signalHandler(int signum)
{
  std::cout << "[capnp_server] Shutdown signal received (" << signum << ")\n";
  if (g_shutdownFulfiller) g_shutdownFulfiller->fulfill();
}

int main(int argc, const char* argv[])
{
  // Set up signal handling
  std::signal(SIGTERM, signalHandler);
  std::signal(SIGINT, signalHandler);

  if (argc != 2) {
    std::cerr << "Usage: " << argv[0] << " HOST:PORT" << std::endl;
    return 1;
  }

  capnp::EzRpcServer server(kj::heap<BenchmarkServiceImpl>(), argv[1]);

  std::cout << "[capnp_server] Cap'n Proto Benchmark Server is running on "
            << argv[1] << std::endl;

  // Set up a promise-based shutdown trigger and block the event loop on it.
  // Previously the loop used usleep(10000) which caused ~10ms latency on
  // every request because the server was sleeping between polls.
  auto paf = kj::newPromiseAndFulfiller<void>();
  g_shutdownFulfiller = kj::mv(paf.fulfiller);
  paf.promise.wait(server.getWaitScope());

  std::cout << "[capnp_server] Server shutting down...\n";
  return 0;
}

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <signal.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/asio_compat.hpp>
#include <nprpc_nameserver.hpp>
#include <nprpc_test.hpp>

#include <boost/range/algorithm_ext/push_back.hpp>
#include <boost/range/irange.hpp>

#include "common/helper.inl"
#include "../../src/logging.hpp"

namespace nprpctest {
  nprpc::Rpc* rpc;
  nprpc::Poa* poa;
  NameserverManager nameserver_manager;
}

std::condition_variable cv;
std::mutex cv_m;
int shutdown_requested = 0;

class ServerControlImpl : public ::nprpc::test::IServerControl_Servant
{
  void Shutdown() override
  {
    // std::cout << "Shutdown requested" << std::endl;
    {
      std::lock_guard lk(cv_m);
      shutdown_requested = 1;
    }
    cv.notify_one();
  }
  void RegisterAckHandler(::nprpc::Object* handler) override
  {
    (void)handler;
    assert(false && "Should not be called in server");
  }
};

int main(int argc, char** argv)
{
  nprpctest::NprpcTestEnvironment env;
  // Calling it manually since we are not using gtest main
  env.SetUp();

  using F = nprpc::ObjectActivationFlags;
  constexpr auto flags =
      F::ws | F::wss | F::http | F::https;

  ServerControlImpl server_control;
  nprpctest::bind<nprpc::test::ServerControl>( server_control, flags, "nprpc_test_server_control");

  // Activating test objects
  #include "common/tests/basic.inl"
  TestBasicImpl test_basic;
  nprpctest::bind<nprpc::test::TestBasic>(test_basic, flags, "nprpc_test_basic");

  #include "common/tests/optional.inl"
  TestOptionalImpl test_optional;
  nprpctest::bind<nprpc::test::TestOptional>( test_optional, flags, "nprpc_test_optional");

  #include "common/tests/nested.inl"
  TestNestedImpl test_nested;
  nprpctest::bind<nprpc::test::TestNested>(test_nested, flags, "nprpc_test_nested");

  #include "common/tests/large_message.inl"
  TestLargeMessage test_large_message;
  nprpctest::bind<nprpc::test::TestLargeMessage>( test_large_message, flags, "nprpc_test_large_message");

  #include "common/tests/objects.inl"
  TestObjectsImpl test_objects(nprpctest::poa);
  nprpctest::bind<nprpc::test::TestObjects>(test_objects, flags, "nprpc_test_objects");

  #include "common/tests/fixed_arrays.inl"
  TestFixedSizeArrayTestImpl test_fixed_size_array_test;
  nprpctest::bind<nprpc::test::FixedSizeArrayTest>( test_fixed_size_array_test, flags, "nprpc_test_fixed_size_array_test");

  #include "common/tests/streams.inl"
  TestStreamsImpl test_streams;
  nprpctest::bind<nprpc::test::TestStreams>(test_streams, flags, "streams_test");

  TestStreamsImpl test_object_streams;
  nprpctest::bind<nprpc::test::TestStreams>(test_object_streams, flags, "object_stream_test");

  TestStreamsImpl test_client_streams;
  nprpctest::bind<nprpc::test::TestStreams>(test_client_streams, flags, "client_stream_test");

  TestStreamsImpl test_bidi_streams;
  nprpctest::bind<nprpc::test::TestStreams>(test_bidi_streams, flags, "bidi_stream_test");

  #include "common/tests/variant.inl"
  TestVariantRpcImpl test_variant_rpc;
  nprpctest::bind<nprpc::test::TestVariantRpc>(test_variant_rpc, flags, "nprpc_test_variant_rpc");

  // Capture interrupt signal to allow graceful shutdown
  signal(SIGINT, [](int signum) {
    std::cout << "Interrupt signal (" << signum << ") received." << std::endl;
    shutdown_requested = 2;
    cv.notify_one();
  });

  // Wait for shutdown signal from JavaScript client
  std::unique_lock lk(cv_m);
  cv.wait(lk, [] { return shutdown_requested; });

  // std::cout << "Server shutting down..." << std::endl;

  // Give some time for the client to receive the response
  if (shutdown_requested == 1)
    std::this_thread::sleep_for(std::chrono::seconds(1));

  // Calling TearDown to clean up manually since we are not using gtest main
  env.TearDown();

  return 0;
}

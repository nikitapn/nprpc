#include <iostream>
#include <chrono>
#include <numeric>
#include <thread>
#include <future>
#include <cstdlib>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <source_location>
#include <condition_variable>

#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc_test_stub/test.hpp>
#include <nprpc_stub/nprpc_nameserver.hpp>

#include <nplib/utils/thread_pool.hpp>

#include <boost/range/algorithm_ext/push_back.hpp> 
#include <boost/range/irange.hpp>
#include <boost/asio/thread_pool.hpp>

#include "common/helper.inl"

std::condition_variable cv;
std::mutex cv_m;
bool shutdown_requested = false;

class ServerControlImpl : public ::test::IServerControl_Servant {
  void Shutdown() override {
    std::cout << "Shutdown requested" << std::endl;
    {
      std::lock_guard<std::mutex> lk(cv_m);
      shutdown_requested = true;
    }
    cv.notify_one();
  }
};

int main(int argc, char** argv) {
  constexpr auto flags = nprpc::ObjectActivationFlags::Enum::ALLOW_WEBSOCKET |
                         nprpc::ObjectActivationFlags::Enum::ALLOW_SSL_WEBSOCKET;
  nprpctest::NprpcTestEnvironment env;
  env.SetUp();

  // Activating test objects  
  #include "common/tests/basic.inl"
  TestBasicImpl test_basic;
  nprpctest::make_stuff_happen<test::TestBasic>(test_basic, flags, "nprpc_test_basic");

  #include "common/tests/optional.inl"
  TestOptionalImpl test_optional;
  nprpctest::make_stuff_happen<test::TestOptional>(test_optional, flags, "nprpc_test_optional");

  #include "common/tests/nested.inl"
  TestNestedImpl test_nested;
  nprpctest::make_stuff_happen<test::TestNested>(test_nested, flags, "nprpc_test_nested");

  #include "common/tests/large_message.inl"
  TestLargeMessage test_large_message;
  nprpctest::make_stuff_happen<test::TestBasic>(test_large_message, flags, "nprpc_test_large_message");

  // Wait for shutdown signal from JavaScript client
  std::unique_lock<std::mutex> lk(cv_m);
  cv.wait(lk, [] { return shutdown_requested; });

  // Give some time for the client to receive the response
  std::this_thread::sleep_for(std::chrono::seconds(1));

  return 0;
}



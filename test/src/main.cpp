#include <sys/wait.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/stream_reader.hpp>
#include <nprpc/stream_writer.hpp>
#include <nprpc_nameserver.hpp>
#include <nprpc_test.hpp>

#include <boost/range/algorithm_ext/push_back.hpp>
#include <boost/range/irange.hpp>

#include "common/helper.inl"

namespace nprpctest {
  nprpc::Rpc* rpc;
  nprpc::Poa* poa;
  NameserverManager nameserver_manager;
}

int main(int argc, char** argv)
{
  ::testing::InitGoogleTest(&argc, argv);

  // Register the test environment
  ::testing::AddGlobalTestEnvironment(new nprpctest::NprpcTestEnvironment);

  return RUN_ALL_TESTS();
}

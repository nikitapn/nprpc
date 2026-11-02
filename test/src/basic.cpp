#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/write.hpp>

#include <future>
#include <latch>
#ifdef NPRPC_QUIC_ENABLED
#include <future>
#include <nprpc/impl/quic_transport.hpp>
#endif
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <numeric>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#include <gtest/gtest.h>

#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/impl/shared_memory_channel.hpp>
#include <nprpc/impl/shared_memory_connection.hpp>
#include <nprpc/impl/shared_memory_listener.hpp>
#include <nprpc/stream_reader.hpp>
#include <nprpc/stream_writer.hpp>
#include <nprpc_nameserver.hpp>
#include <nprpc_test.hpp>

#include <boost/range/algorithm_ext/push_back.hpp>
#include <boost/range/irange.hpp>

#include "common/helper.inl"

namespace nprpctest {
// Basic functionality test
TEST_F(NprpcTest, TestBasic)
{
#include "common/tests/basic.inl"
  TestBasicImpl servant;
  auto exec_test = [this, &servant](nprpc::ObjectActivationFlags flags) {
    try {
      auto obj = bind_and_resolve<nprpc::test::TestBasic>(servant, flags);

      // ReturnU32 test
      EXPECT_EQ(obj->ReturnU32(), 42u);

      // ReturnBoolean test
      EXPECT_TRUE(obj->ReturnBoolean());

      // In/Out test
      std::vector<uint8_t> ints;
      ints.reserve(256);
      boost::push_back(ints, boost::irange(0, 255));

      EXPECT_TRUE(obj->In_(100, true, nprpc::flat::make_read_only_span(ints)));

      uint32_t a;
      bool b;

      obj->Out(a, b, ints);

      EXPECT_EQ(a, 100u);
      EXPECT_TRUE(b);

      uint8_t ix = 0;
      for (auto i : ints) {
        EXPECT_EQ(ix++, i);
      }

      // InArrayOfStrings test
      std::array<std::string, 32> string_array_in;
      for (size_t i = 0; i < string_array_in.size(); ++i)
        string_array_in[i] = "Array string " + std::to_string(i);
      obj->InArrayOfStrings(string_array_in);

      // InVectorOfStrings test
      std::vector<std::string> strings{"first", "", "third string"};
      obj->InVectorOfStrings(nprpc::flat::make_read_only_span(strings));

      // OutStruct test
      nprpc::test::AAA aaa;
      obj->OutStruct(aaa);
      EXPECT_EQ(aaa.a, 12345);
      EXPECT_EQ(std::string_view(aaa.b), "Hello from OutStruct"sv);
      EXPECT_EQ(std::string_view(aaa.c), "Another string"sv);

      // OutArrayOfStructs test
      std::vector<nprpc::test::SimpleStruct> struct_array;
      obj->OutArrayOfStructs(struct_array);
      EXPECT_EQ(struct_array.size(), 10u);
      for (uint32_t i = 0; i < 10; ++i) {
        EXPECT_EQ(struct_array[i].id, i + 1);
      }

      // ReturnStringArray test
      auto string_array = obj->ReturnStringArray(5);
      EXPECT_EQ(string_array.size(), 5u);
      for (uint32_t i = 0; i < 5; ++i) {
        EXPECT_EQ(string_array[i], "String " + std::to_string(i));
      }
    } catch (nprpc::Exception& ex) {
      FAIL() << "Exception in TestBasic: " << ex.what();
    }
  };

  exec_test(nprpc::ObjectActivationFlags::tcp);
  exec_test(nprpc::ObjectActivationFlags::ws);
  exec_test(nprpc::ObjectActivationFlags::wss);
  exec_test(nprpc::ObjectActivationFlags::shm);
  exec_test(nprpc::ObjectActivationFlags::quic);
}

// Preferred transport is a soft hint for select_endpoint().
TEST_F(NprpcTest, PreferredTransport)
{
#include "common/tests/basic.inl"
  TestBasicImpl servant;
  try {
    // Advertise SHM + TCP so default preference would pick SHM (same machine).
    auto flags = nprpc::ObjectActivationFlags::tcp |
                 nprpc::ObjectActivationFlags::shm;
    auto obj = bind_and_resolve<nprpc::test::TestBasic>(servant, flags);

    // Default: same-machine → SharedMemory.
    EXPECT_TRUE(obj->select_endpoint());
    EXPECT_EQ(obj->get_endpoint().type(), nprpc::EndPointType::SharedMemory);

    // Prefer TCP when available.
    obj->set_preferred_transport(nprpc::EndPointType::Tcp);
    EXPECT_TRUE(obj->select_endpoint());
    EXPECT_EQ(obj->get_endpoint().type(), nprpc::EndPointType::Tcp);
    EXPECT_EQ(obj->preferred_transport(), nprpc::EndPointType::Tcp);

    // Prefer QUIC when not advertised → fall back to default (SHM).
    obj->set_preferred_transport(nprpc::EndPointType::Quic);
    EXPECT_TRUE(obj->select_endpoint());
    EXPECT_EQ(obj->get_endpoint().type(), nprpc::EndPointType::SharedMemory);

    // Clear preference → default again.
    obj->set_preferred_transport(std::nullopt);
    EXPECT_TRUE(obj->select_endpoint());
    EXPECT_EQ(obj->get_endpoint().type(), nprpc::EndPointType::SharedMemory);
    EXPECT_FALSE(obj->preferred_transport().has_value());

    // Preferred TCP must still work for an actual call.
    obj->set_preferred_transport(nprpc::EndPointType::Tcp);
    EXPECT_TRUE(obj->select_endpoint());
    EXPECT_EQ(obj->ReturnU32(), 42u);
  } catch (nprpc::Exception& ex) {
    FAIL() << "Exception in PreferredTransport: " << ex.what();
  }
}

// Basic exception handling
TEST_F(NprpcTest, TestException)
{
#include "common/tests/basic.inl"
  TestBasicImpl servant;
  auto exec_test = [this, &servant](nprpc::ObjectActivationFlags flags) {
    try {
      auto obj = bind_and_resolve<nprpc::test::TestBasic>(servant, flags);

      // InException test
      try {
        obj->InException();
        FAIL() << "Expected InException to throw SimpleException";
      } catch (const nprpc::test::SimpleException& ex) {
        EXPECT_EQ(std::string_view(ex.message), "This is a test exception"sv);
        EXPECT_EQ(ex.code, 123);
      }

      try {
        obj->MultipleExceptions(0);
        FAIL() << "Expected MultipleExceptions(0) to throw SimpleException";
      } catch (const nprpc::test::SimpleException& ex) {
        EXPECT_EQ(std::string_view(ex.message), "Simple exception branch"sv);
        EXPECT_EQ(ex.code, 456);
      }

      try {
        obj->MultipleExceptions(1);
        FAIL() << "Expected MultipleExceptions(1) to throw AssertionFailed";
      } catch (const nprpc::test::AssertionFailed& ex) {
        EXPECT_EQ(std::string_view(ex.message), "Assertion failed branch"sv);
      }

      // OutScalarWithException test - tests flat output struct with
      // exception handler This verifies the fix where output parameters
      // must be declared before try block
      uint8_t read_value;
      obj->OutScalarWithException(10, 20, read_value);
      EXPECT_EQ(read_value, 30); // dev_addr + addr = 10 + 20 = 30
    } catch (nprpc::Exception& ex) {
      FAIL() << "Exception in TestBasic: " << ex.what();
    }
  };

  exec_test(nprpc::ObjectActivationFlags::tcp);
  exec_test(nprpc::ObjectActivationFlags::ws);
  exec_test(nprpc::ObjectActivationFlags::wss);
  exec_test(nprpc::ObjectActivationFlags::shm);
  exec_test(nprpc::ObjectActivationFlags::quic);
}

// Optional types test
TEST_F(NprpcTest, TestOptional)
{
#include "common/tests/optional.inl"
  TestOptionalImpl servant;
  auto exec_test = [this, &servant](nprpc::ObjectActivationFlags flags) {
    try {
      auto obj = bind_and_resolve<nprpc::test::TestOptional>(servant, flags);

      EXPECT_TRUE(obj->InEmpty(std::nullopt));
      EXPECT_TRUE(obj->In_(100, nprpc::test::AAA{100u, "test_b"s, "test_c"s}));

      std::optional<uint32_t> a;

      obj->OutEmpty(a);
      EXPECT_FALSE(a.has_value());

      obj->Out(a);
      EXPECT_TRUE(a.has_value());
      EXPECT_EQ(a.value(), 100u);

      auto opt = obj->ReturnOpt1();
      EXPECT_EQ(opt.str, "test_string");
      EXPECT_TRUE(opt.data.has_value());
      EXPECT_EQ(opt.data->size(), 10u);
      for (uint8_t i = 0; i < 10; ++i) {
        EXPECT_EQ(opt.data->at(i), i);
      }

    } catch (nprpc::Exception& ex) {
      FAIL() << "Exception in TestOptional: " << ex.what();
    }
  };
  exec_test(nprpc::ObjectActivationFlags::tcp);
  exec_test(nprpc::ObjectActivationFlags::ws);
  exec_test(nprpc::ObjectActivationFlags::wss);
  exec_test(nprpc::ObjectActivationFlags::shm);
  exec_test(nprpc::ObjectActivationFlags::quic);
}

TEST_F(NprpcTest, ProduceHostJson)
{
#include "common/tests/basic.inl"
  TestBasicImpl servant;

  auto oid = poa->activate_object(
      &servant,
      nprpc::ObjectActivationFlags::ws |
          nprpc::ObjectActivationFlags::wss |
          nprpc::ObjectActivationFlags::http |
          nprpc::ObjectActivationFlags::https);

  rpc->clear_host_json();
  rpc->add_to_host_json("calculator", oid);

  const auto output_path =
      (std::filesystem::temp_directory_path() / "nprpc-host-json-test" /
       "host.json")
          .string();
  rpc->produce_host_json(output_path);

  std::ifstream is(output_path);
  ASSERT_TRUE(is.is_open());

  const std::string text{std::istreambuf_iterator<char>{is},
                         std::istreambuf_iterator<char>{}};

  EXPECT_NE(text.find("\"secured\": true"), std::string::npos);
#ifdef NPRPC_HTTP3_ENABLED
  EXPECT_NE(text.find("\"webtransport\": true"), std::string::npos);
#else
  EXPECT_NE(text.find("\"webtransport\": false"), std::string::npos);
#endif
  EXPECT_NE(text.find("\"calculator\""), std::string::npos);
  EXPECT_NE(text.find(std::string("\"class_id\": \"") +
                          std::string(servant.get_class()) + "\""),
            std::string::npos);
  EXPECT_NE(text.find("\"urls\": \"web://localhost:22223;"),
            std::string::npos);
}

// Nested structures test
TEST_F(NprpcTest, TestNested)
{
  // set test timeout to 60 seconds
  Test::RecordProperty("timeout", "60");
#include "common/tests/nested.inl"
  TestNestedImpl servant;
  auto exec_test = [this, &servant](nprpc::ObjectActivationFlags flags) {
    try {
      auto obj = bind_and_resolve<nprpc::test::TestNested>(servant, flags);
      obj->set_timeout(5000); // Set a longer timeout for this test

      std::optional<nprpc::test::BBB> a;

      obj->Out(a);

      EXPECT_TRUE(a.has_value());
      auto& value = a.value();

      EXPECT_EQ(value.a.size(), 1024ull);

      std::uint32_t ix = 0;
      for (auto& i : value.a) {
        EXPECT_EQ(i.a, ix++);
        EXPECT_EQ(std::string_view(i.b), std::string_view(nested_test_str1));
        EXPECT_EQ(std::string_view(i.c), std::string_view(nested_test_str2));
      }

      EXPECT_EQ(value.b.size(), 2048ull);

      bool b = false;
      for (auto& i : value.b) {
        EXPECT_EQ(std::string_view(i.a), std::string_view(nested_test_str1));
        EXPECT_EQ(std::string_view(i.b), std::string_view(nested_test_str2));
        EXPECT_TRUE(i.c.has_value());
        EXPECT_EQ(i.c.value(), b ^= 1);
      }
    } catch (nprpc::Exception& ex) {
      FAIL() << "Exception in TestNested: " << ex.what();
    }
  };
  exec_test(nprpc::ObjectActivationFlags::tcp);
  exec_test(nprpc::ObjectActivationFlags::ws);
  exec_test(nprpc::ObjectActivationFlags::wss);
  exec_test(nprpc::ObjectActivationFlags::shm);
  exec_test(nprpc::ObjectActivationFlags::quic);
}

TEST_F(NprpcTest, TestFixedSizeArrays)
{
  #include "common/tests/fixed_arrays.inl"
  TestFixedSizeArrayTestImpl servant;
  auto exec_test = [this, &servant](nprpc::ObjectActivationFlags flags) {
    try {
      auto obj = bind_and_resolve<nprpc::test::FixedSizeArrayTest>(servant, flags);

      std::array<uint32_t, 10> input_array;
      std::iota(input_array.begin(), input_array.end(), 1); // Fill with 1,2,3,4,5,6,7,8,9,10

      // InFixedArray test - verifies that fixed-size array is correctly received and that data is not corrupted
      obj->InFixedArray(input_array);

      // OutFixedArray test - verifies that fixed-size array is correctly handled in both directions and that data is not corrupted
      obj->OutFixedArray(input_array);
      for (size_t i = 0; i < input_array.size(); ++i) {
        EXPECT_EQ(input_array[i], i + 1);
      }

      // OutTwoFixedArrays test - verifies that multiple fixed-size arrays are correctly handled and that data is not corrupted
      std::array<uint32_t, 10> second_array;
      obj->OutTwoFixedArrays(input_array, second_array);
      for (size_t i = 0; i < input_array.size(); ++i) {
        EXPECT_EQ(input_array[i], i + 1);
        EXPECT_EQ(second_array[i], (i + 1) * 10);
      }

      // InFixedArrayOfStructs test - verifies that fixed-size array of structs is correctly received and that data is not corrupted
      std::array<nprpc::test::SimpleStruct, 5> struct_array;
      for (size_t i = 0; i < struct_array.size(); ++i) {
        struct_array[i].id = i + 1;
      }
      obj->InFixedArrayOfStructs(struct_array);

      // OutFixedArrayOfStructs test - verifies that fixed-size array of structs is correctly handled in both directions and that data is not corrupted
      obj->OutFixedArrayOfStructs(struct_array);
      for (size_t i = 0; i < struct_array.size(); ++i) {
        EXPECT_EQ(struct_array[i].id, i + 1);
      }

      // OutTwoFixedArraysOfStructs test - verifies that multiple fixed-size arrays of structs are correctly handled and that data is not corrupted
      std::array<nprpc::test::AAA, 5> aaa_array;
      obj->OutTwoFixedArraysOfStructs(struct_array, aaa_array);
      for (size_t i = 0; i < struct_array.size(); ++i) {
        EXPECT_EQ(struct_array[i].id, i + 1);
        EXPECT_EQ(aaa_array[i].a, (i + 1) * 10);
        EXPECT_EQ(std::string_view(aaa_array[i].b),
          std::string_view("str" + std::to_string((i + 1))));
        EXPECT_EQ(std::string_view(aaa_array[i].c),
          std::string_view("str" + std::to_string((i + 1) * 100)));
      }
    } catch (nprpc::Exception& ex) {
      FAIL() << "Exception in TestFixedSizeArrays: " << ex.what();
    }
  };
  exec_test(nprpc::ObjectActivationFlags::tcp);
  exec_test(nprpc::ObjectActivationFlags::ws);
  exec_test(nprpc::ObjectActivationFlags::wss);
  exec_test(nprpc::ObjectActivationFlags::shm);
  exec_test(nprpc::ObjectActivationFlags::quic);
}

// Zero-copy out-direct test — verifies OwnedSpan and OwnedDirect wrappers
TEST_F(NprpcTest, TestDirect)
{
#include "common/tests/direct.inl"
  TestDirectImpl servant;
  auto exec_test = [this, &servant](nprpc::ObjectActivationFlags flags) {
    try {
      auto obj = bind_and_resolve<nprpc::test::TestDirect>(servant, flags);

      // --- GetBytes: out direct vector<u8> → OwnedSpan<uint8_t> ---
      ::nprpc::flat::OwnedSpan<uint8_t> bytes;
      obj->GetBytes(256, bytes);
      EXPECT_EQ(bytes.size(), 256u);
      for (uint32_t i = 0; i < 256; ++i)
        EXPECT_EQ(bytes.data()[i], static_cast<uint8_t>(i % 256));

      // --- GetBytesFixedArray: out direct u8[256] → Span<uint8_t> ---
      obj->GetBytesFixedArray(bytes);
      EXPECT_EQ(bytes.size(), 256u);
      for (uint32_t i = 0; i < 256; ++i)
        EXPECT_EQ(bytes.data()[i], static_cast<uint8_t>(i % 256));

      // --- GetFlatStruct: out direct FlatStruct → OwnedDirect<FlatStruct_Direct> ---
      ::nprpc::flat::OwnedDirect<nprpc::test::flat::FlatStruct_Direct> fs;
      obj->GetFlatStruct(fs);
      EXPECT_TRUE(fs.valid());
      EXPECT_EQ(fs.get().a(), 42);
      EXPECT_EQ(fs.get().b(), 100u);
      EXPECT_EQ(fs.get().c(), 3.14f);

      // --- GetString: out direct string → OwnedDirect<String_Direct1> ---
      ::nprpc::flat::OwnedDirect<::nprpc::flat::String_Direct1> str;
      obj->GetString(str);
      EXPECT_TRUE(str.valid());
      EXPECT_EQ(std::string_view(str.get()()), "Hello, direct!"sv);

      // --- GetStructArray: out direct vector<SimpleStruct> → OwnedDirect<Vector_Direct2<...>> ---
      ::nprpc::flat::OwnedDirect<::nprpc::flat::Vector_Direct2<
          nprpc::test::flat::SimpleStruct,
          nprpc::test::flat::SimpleStruct_Direct>> arr;
      obj->GetStructArray(5, arr);
      EXPECT_TRUE(arr.valid());
      auto span = arr.get()();
      EXPECT_EQ(span.size(), 5u);
      for (uint32_t i = 0; i < 5; ++i)
        EXPECT_EQ((*span[i]).id(), i + 1);

      // --- GetFundamentalDirect: 'direct' on u32 is demoted to plain out ---
      // The compiler emits a warning; proxy/servant use a regular reference.
      uint32_t fund_val = 0;
      obj->GetFundamentalDirect(fund_val);
      EXPECT_EQ(fund_val, 777u);

    } catch (nprpc::Exception& ex) {
      FAIL() << "Exception in TestDirect: " << ex.what();
    }
  };
  exec_test(nprpc::ObjectActivationFlags::tcp);
  exec_test(nprpc::ObjectActivationFlags::ws);
  exec_test(nprpc::ObjectActivationFlags::wss);
  exec_test(nprpc::ObjectActivationFlags::shm);
  exec_test(nprpc::ObjectActivationFlags::quic);
}

// Large message test to verify async_write fix for messages >2.6MB
TEST_F(NprpcTest, TestLargeMessage)
{
  // Set test timeout to 120 seconds for large data transfer
  Test::RecordProperty("timeout", "120");
#include "common/tests/large_message.inl"
  TestLargeMessage servant;
  auto exec_test = [this, &servant](nprpc::ObjectActivationFlags flags) {
    try {
      auto obj =
          bind_and_resolve<nprpc::test::TestLargeMessage>(servant, flags);
      obj->set_timeout(5000); // Set a longer timeout for this test

      // Test sending 3MB of data
      std::vector<uint8_t> large_data(3 * 1024 * 1024);
      large_data[0] = 0xAB;
      large_data[large_data.size() - 1] = 0xCD;
      for (size_t i = 1; i < large_data.size() - 1; ++i) {
        large_data[i] = static_cast<uint8_t>(i % 256);
      }

      // This should work with our async_write fix
      EXPECT_TRUE(
          obj->In_(42, true, nprpc::flat::make_read_only_span(large_data)));

      // Test receiving 3MB of data
      uint32_t a;
      bool b;
      std::vector<uint8_t> received_data;

      obj->Out(a, b, received_data);

      EXPECT_EQ(a, 42u);
      EXPECT_TRUE(b);
      EXPECT_EQ(received_data.size(), 3 * 1024 * 1024u);
      EXPECT_EQ(received_data[0], 0xAB);
      EXPECT_EQ(received_data[received_data.size() - 1], 0xCD);
    } catch (nprpc::Exception& ex) {
      FAIL() << "Exception in TestLargeMessage: " << ex.what();
    }
  };
  exec_test(nprpc::ObjectActivationFlags::tcp);
  exec_test(nprpc::ObjectActivationFlags::ws);
  exec_test(nprpc::ObjectActivationFlags::wss);
  exec_test(nprpc::ObjectActivationFlags::shm);
  exec_test(nprpc::ObjectActivationFlags::quic);
}

TEST_F(NprpcTest, UserSuppliedObjectIdPolicy)
{
  struct StaticIdServant : nprpc::ObjectServant {
    std::string_view get_class() const noexcept override
    {
      return "StaticIdServant";
    }

    void dispatch(::nprpc::SessionContext&, bool) override
    {
      throw nprpc::Exception("Not implemented");
    }
  } servant_one, servant_two, servant_three;

  auto custom_poa =
      rpc->create_poa()
          .with_max_objects(4)
          .with_lifespan(nprpc::PoaPolicy::Lifespan::Persistent)
          .with_object_id_policy(nprpc::PoaPolicy::ObjectIdPolicy::UserSupplied)
          .build();

  // User-supplied IDs must be in range [0, max_objects)
  const nprpc::oid_t manual_id = 2;

  auto oid = custom_poa->activate_object_with_id(
      manual_id, &servant_one, nprpc::ObjectActivationFlags::tcp);

  EXPECT_EQ(oid.object_id(), manual_id);

  // Duplicate ID should fail
  EXPECT_THROW(
      custom_poa->activate_object_with_id(
          manual_id, &servant_two, nprpc::ObjectActivationFlags::tcp),
      nprpc::Exception);

  // activate_object should fail on UserSupplied policy
  EXPECT_THROW(custom_poa->activate_object(
                   &servant_two, nprpc::ObjectActivationFlags::tcp),
               nprpc::Exception);

  // ID out of range should fail
  EXPECT_THROW(custom_poa->activate_object_with_id(
                   100, // exceeds max_objects (4)
                   &servant_three, nprpc::ObjectActivationFlags::tcp),
               nprpc::Exception);

  custom_poa->deactivate_object(manual_id);
  rpc->destroy_poa(custom_poa);
}

// POA DispatchExecutor: servant runs on a dedicated worker (post+wait).
TEST_F(NprpcTest, PoaDispatchExecutor)
{
#include "common/tests/basic.inl"

  struct QueueCtx {
    std::mutex m;
    std::condition_variable cv;
    std::deque<std::function<void()>> jobs;
    std::atomic<bool> stop{false};
    std::thread::id worker_id{};
  } qctx;

  std::thread worker([&] {
    qctx.worker_id = std::this_thread::get_id();
    for (;;) {
      std::function<void()> job;
      {
        std::unique_lock lk(qctx.m);
        qctx.cv.wait(lk, [&] { return qctx.stop.load() || !qctx.jobs.empty(); });
        if (qctx.stop.load() && qctx.jobs.empty())
          return;
        job = std::move(qctx.jobs.front());
        qctx.jobs.pop_front();
      }
      job();
    }
  });

  // Wait until worker_id is set
  while (qctx.worker_id == std::thread::id{})
    std::this_thread::yield();

  nprpc::DispatchExecutor ex;
  ex.ctx = &qctx;
  ex.post = [](void* c, void (*fn)(void*), void* arg) {
    auto* x = static_cast<QueueCtx*>(c);
    {
      std::lock_guard lk(x->m);
      x->jobs.push_back([fn, arg] { fn(arg); });
    }
    x->cv.notify_one();
  };
  ex.is_running_on = [](void* c) {
    auto* x = static_cast<QueueCtx*>(c);
    return std::this_thread::get_id() == x->worker_id;
  };

  // invoke_sync unit check (no network)
  {
    std::thread::id ran_on{};
    ex.invoke_sync([&] { ran_on = std::this_thread::get_id(); });
    EXPECT_EQ(ran_on, qctx.worker_id);
    EXPECT_NE(ran_on, std::this_thread::get_id());
  }

  auto* custom_poa =
      rpc->create_poa()
          .with_max_objects(8)
          .with_lifespan(nprpc::PoaPolicy::Lifespan::Persistent)
          .with_dispatch_executor(ex)
          .build();

  EXPECT_TRUE(custom_poa->dispatch_executor());

  std::atomic<std::thread::id> servant_tid{};
  class TrackingServant : public TestBasicImpl {
  public:
    std::atomic<std::thread::id>* tid;
    uint32_t ReturnU32() override
    {
      tid->store(std::this_thread::get_id());
      return 42;
    }
  } servant;
  servant.tid = &servant_tid;

  try {
    auto oid = custom_poa->activate_object(
        &servant, nprpc::ObjectActivationFlags::tcp);
    auto nameserver = rpc->get_nameserver("127.0.0.1");
    nameserver->Bind(oid, "nprpc_dispatch_executor_test");

    nprpc::Object* raw = nullptr;
    ASSERT_TRUE(nameserver->Resolve("nprpc_dispatch_executor_test", raw));
    auto obj = nprpc::ObjectPtr(nprpc::narrow<nprpc::test::TestBasic>(raw));
    obj->set_preferred_transport(nprpc::EndPointType::Tcp);
    ASSERT_TRUE(obj->select_endpoint());

    EXPECT_EQ(obj->ReturnU32(), 42u);
    EXPECT_EQ(servant_tid.load(), qctx.worker_id);
  } catch (nprpc::Exception& ex) {
    FAIL() << "Exception in PoaDispatchExecutor: " << ex.what();
  }

  custom_poa->deactivate_object(servant.oid());
  rpc->destroy_poa(custom_poa);

  {
    std::lock_guard lk(qctx.m);
    qctx.stop = true;
  }
  qctx.cv.notify_one();
  worker.join();
}

// A POA that asks for it runs off the transport thread on every transport: a
// servant stuck on its executor doesn't take an I/O thread with it.
TEST_F(NprpcTest, HandedOffCallLeavesTransportFree)
{
#include "common/tests/basic.inl"

  struct Target {
    nprpc::EndPointType type;
    nprpc::EndPointType probe;
  };
  std::vector<Target> targets = {
    {nprpc::EndPointType::Tcp, nprpc::EndPointType::WebSocket},
    {nprpc::EndPointType::WebSocket, nprpc::EndPointType::Tcp},
    {nprpc::EndPointType::SharedMemory, nprpc::EndPointType::Tcp},
#if defined(NPRPC_HAS_QUIC) || defined(NPRPC_QUIC_ENABLED)
    {nprpc::EndPointType::Quic, nprpc::EndPointType::Tcp},
#endif
  };
  const auto flags = nprpc::ObjectActivationFlags::tcp |
                     nprpc::ObjectActivationFlags::ws |
                     nprpc::ObjectActivationFlags::shm
#if defined(NPRPC_HAS_QUIC) || defined(NPRPC_QUIC_ENABLED)
                     | nprpc::ObjectActivationFlags::quic
#endif
      ;

  struct Queue {
    std::mutex m;
    std::condition_variable cv;
    std::deque<std::function<void()>> jobs;
    bool stop = false;
    std::atomic<std::thread::id> worker_id{};
  } queue;
  std::thread worker([&] {
    queue.worker_id = std::this_thread::get_id();
    std::unique_lock lk(queue.m);
    for (;;) {
      queue.cv.wait(lk, [&] { return queue.stop || !queue.jobs.empty(); });
      if (queue.jobs.empty())
        return;
      auto job = std::move(queue.jobs.front());
      queue.jobs.pop_front();
      lk.unlock();
      job();
      lk.lock();
    }
  });
  while (queue.worker_id.load() == std::thread::id{})
    std::this_thread::yield();

  nprpc::DispatchExecutor ex;
  ex.ctx = &queue;
  ex.post = [](void* c, void (*fn)(void*), void* arg) {
    auto* q = static_cast<Queue*>(c);
    {
      std::lock_guard lk(q->m);
      q->jobs.push_back([fn, arg] { fn(arg); });
    }
    q->cv.notify_one();
  };
  ex.is_running_on = [](void* c) {
    return std::this_thread::get_id() == static_cast<Queue*>(c)->worker_id.load();
  };
  auto* stuck_poa = rpc->create_poa()
                        .with_max_objects(8)
                        .with_lifespan(nprpc::PoaPolicy::Lifespan::Persistent)
                        .with_dispatch_executor(ex)
                        .build();

  class StuckServant : public TestBasicImpl {
  public:
    std::promise<std::thread::id> entered;
    std::shared_future<void> release;
    uint32_t ReturnU32() override
    {
      entered.set_value(std::this_thread::get_id());
      release.wait();
      return 42;
    }
  };

  auto resolve = [&](nprpc::Poa* p, nprpc::ObjectServant& servant, const char* name,
                     nprpc::EndPointType type) {
    auto oid = p->activate_object(&servant, flags);
    auto nameserver = rpc->get_nameserver("127.0.0.1");
    nameserver->Bind(oid, name);
    nprpc::Object* raw = nullptr;
    EXPECT_TRUE(nameserver->Resolve(name, raw));
    auto obj = nprpc::ObjectPtr(nprpc::narrow<nprpc::test::TestBasic>(raw));
    obj->set_preferred_transport(type);
    EXPECT_TRUE(obj->select_endpoint());
    EXPECT_EQ(obj->get_endpoint().type(), type);
    return obj;
  };

  for (const auto& target : targets) {
    SCOPED_TRACE(static_cast<int>(target.type));
    StuckServant stuck;
    TestBasicImpl plain;
    std::promise<void> release;
    stuck.release = release.get_future().share();
    auto entered = stuck.entered.get_future();

    auto stuck_obj = resolve(stuck_poa, stuck, "nprpc_handoff_stuck", target.type);
    auto probe_obj = resolve(poa, plain, "nprpc_handoff_probe", target.probe);
    ASSERT_EQ(probe_obj->ReturnU32(), 42u); // connects before the pool shrinks

    // Leave the shared I/O pool (4 threads) a single thread. A reader blocked
    // on the stuck servant would then starve the probe.
    std::promise<void> free_pool;
    auto pool_freed = free_pool.get_future().share();
    std::latch occupied{3};
    for (int i = 0; i < 3; ++i) {
      boost::asio::post(nprpc::get_io_context(), [&occupied, pool_freed] {
        occupied.count_down();
        pool_freed.wait();
      });
    }
    occupied.wait();

    auto stuck_call = std::async(std::launch::async, [&] { return stuck_obj->ReturnU32(); });
    ASSERT_EQ(entered.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    EXPECT_EQ(entered.get(), queue.worker_id.load());

    auto probe_call = std::async(std::launch::async, [&] { return probe_obj->ReturnU32(); });
    const auto probe_status = probe_call.wait_for(std::chrono::seconds(3));
    EXPECT_EQ(probe_status, std::future_status::ready)
        << "the stuck servant is holding an I/O thread";

    free_pool.set_value();
    release.set_value();
    EXPECT_EQ(stuck_call.get(), 42u);
    EXPECT_EQ(probe_call.get(), 42u);

    stuck_poa->deactivate_object(stuck.oid());
    poa->deactivate_object(plain.oid());
  }

  rpc->destroy_poa(stuck_poa);
  {
    std::lock_guard lk(queue.m);
    queue.stop = true;
  }
  queue.cv.notify_one();
  worker.join();
}

// NeverBlockTransport on every transport: calls from many client threads, mixed
// with calls to a default POA over the same session, run one at a time and each
// get their own reply.
TEST_F(NprpcTest, HandedOffCallsStaySerial)
{
#include "common/tests/basic.inl"

  std::vector<nprpc::EndPointType> transports = {
    nprpc::EndPointType::Tcp,
    nprpc::EndPointType::WebSocket,
    nprpc::EndPointType::SharedMemory,
#if defined(NPRPC_HAS_QUIC) || defined(NPRPC_QUIC_ENABLED)
    nprpc::EndPointType::Quic,
#endif
  };
  const auto flags = nprpc::ObjectActivationFlags::tcp |
                     nprpc::ObjectActivationFlags::ws |
                     nprpc::ObjectActivationFlags::shm
#if defined(NPRPC_HAS_QUIC) || defined(NPRPC_QUIC_ENABLED)
                     | nprpc::ObjectActivationFlags::quic
#endif
      ;

  auto* pool_poa = rpc->create_poa()
                       .with_max_objects(8)
                       .with_lifespan(nprpc::PoaPolicy::Lifespan::Persistent)
                       .with_transport_affinity(
                           nprpc::PoaPolicy::TransportAffinity::NeverBlockTransport)
                       .build();

  class CountingServant : public TestBasicImpl {
  public:
    std::atomic<int>* running;
    std::atomic<int>* overlaps;
    uint32_t ReturnU32() override
    {
      if (running->fetch_add(1) != 0)
        overlaps->fetch_add(1);
      std::this_thread::sleep_for(std::chrono::microseconds(50));
      running->fetch_sub(1);
      return 42;
    }
  };

  for (auto type : transports) {
    SCOPED_TRACE(static_cast<int>(type));
    std::atomic<int> running{0}, overlaps{0};
    CountingServant pooled, inlined;
    pooled.running = inlined.running = &running;
    pooled.overlaps = inlined.overlaps = &overlaps;

    auto resolve = [&](nprpc::Poa* p, nprpc::ObjectServant& servant, const char* name) {
      auto oid = p->activate_object(&servant, flags);
      auto nameserver = rpc->get_nameserver("127.0.0.1");
      nameserver->Bind(oid, name);
      nprpc::Object* raw = nullptr;
      EXPECT_TRUE(nameserver->Resolve(name, raw));
      auto obj = nprpc::ObjectPtr(nprpc::narrow<nprpc::test::TestBasic>(raw));
      obj->set_preferred_transport(type);
      EXPECT_TRUE(obj->select_endpoint());
      return obj;
    };
    auto pooled_obj = resolve(pool_poa, pooled, "nprpc_handoff_pooled");
    auto inlined_obj = resolve(poa, inlined, "nprpc_handoff_inlined");

    std::atomic<int> wrong{0};
    std::vector<std::thread> callers;
    for (int t = 0; t < 4; ++t) {
      callers.emplace_back([&, t] {
        for (int i = 0; i < 100; ++i) {
          auto& obj = (i + t) % 2 ? pooled_obj : inlined_obj;
          if (obj->ReturnU32() != 42u)
            wrong.fetch_add(1);
        }
      });
    }
    for (auto& caller : callers)
      caller.join();

    EXPECT_EQ(wrong.load(), 0);
    EXPECT_EQ(overlaps.load(), 0);

    pool_poa->deactivate_object(pooled.oid());
    poa->deactivate_object(inlined.oid());
  }

  rpc->destroy_poa(pool_poa);
}

// Bad input validation test
TEST_F(NprpcTest, TestBadInput)
{
  class TestBadInputImpl : public nprpc::test::ITestBadInput_Servant
  {
  public:
    void In_(::nprpc::flat::Span<uint8_t> a) override {}
    bool InStrings (::nprpc::flat::Span<char> a, ::nprpc::flat::Span<char> b) override { return true; }
    bool Send (::nprpc::test::flat::ChatMessage_Direct msg) override { return true; }
    bool SendObject (::nprpc::Object* o) override { return true; }
    ::nprpc::Task<> UploadBadStream(std::vector<uint8_t> a, nprpc::StreamReader<uint8_t> data) override { co_return; }
  } servant;

  auto exec_test = [this, &servant](nprpc::ObjectActivationFlags flags) {
    try {
      auto obj = bind_and_resolve<nprpc::test::TestBadInput>(servant, flags);

      nprpc::flat_buffer buf;
      auto mb = buf.prepare(2048);
      buf.commit(40);
      static_cast<::nprpc::impl::Header*>(mb.data())->msg_id =
          ::nprpc::impl::MessageId::FunctionCall;
      static_cast<::nprpc::impl::Header*>(mb.data())->msg_type =
          ::nprpc::impl::MessageType::Request;
      ::nprpc::impl::flat::CallHeader_Direct __ch(
          buf, sizeof(::nprpc::impl::Header));
      __ch.object_id() = obj->object_id();
      __ch.poa_idx() = obj->poa_idx();
      __ch.interface_idx() = 0; // single interface
      __ch.function_idx() = 0;  // single function

      buf.commit(1024);
      // Set correct size in header
      static_cast<::nprpc::impl::Header*>(buf.data().data())->size =
          static_cast<uint32_t>(buf.size());
      auto vec_begin = static_cast<std::byte*>(buf.data().data()) + 32;
      // Set size of the vector to be larger than the buffer size
      *reinterpret_cast<uint32_t*>(vec_begin) = 0xDEADBEEF;

      ::nprpc::impl::g_rpc->call(obj->get_endpoint(), buf, obj->get_timeout());
      auto std_reply = nprpc::impl::handle_standart_reply(buf);
      if (std_reply != 0) {
        throw nprpc::Exception("Unknown Error");
      }

      FAIL() << "Expected nprpc::ExceptionBadInput to be thrown";
    } catch (nprpc::ExceptionBadInput&) {
      // Expected exception - test passed
      SUCCEED();
    } catch (nprpc::Exception& ex) {
      FAIL() << "Unexpected exception in TestBadInput: " << ex.what();
    }
  };

  auto exec_stream_test = [this, &servant](nprpc::ObjectActivationFlags flags) {
    try {
      auto obj = bind_and_resolve<nprpc::test::TestBadInput>(servant, flags);

      nprpc::flat_buffer buf;
      auto mb = buf.prepare(2048);
      buf.commit(48);
      static_cast<::nprpc::impl::Header*>(mb.data())->msg_id =
          ::nprpc::impl::MessageId::StreamInitialization;
      static_cast<::nprpc::impl::Header*>(mb.data())->msg_type =
          ::nprpc::impl::MessageType::Request;
      ::nprpc::impl::flat::StreamInit_Direct init(
          buf, sizeof(::nprpc::impl::Header));
      init.stream_id() = 0x1234;
      init.poa_idx() = obj->poa_idx();
      init.interface_idx() = 0;
      init.object_id() = obj->object_id();
      init.func_idx() = 4;
      init.stream_kind() = ::nprpc::impl::StreamKind::Client;

        auto vec_begin = reinterpret_cast<uint32_t*>(
          static_cast<std::byte*>(mb.data()) + 48);
        vec_begin[0] = 8;
        vec_begin[1] = 1;

      static_cast<::nprpc::impl::Header*>(buf.data().data())->size =
          static_cast<uint32_t>(buf.size());

      ::nprpc::impl::g_rpc->call(obj->get_endpoint(), buf, obj->get_timeout());
      auto std_reply = nprpc::impl::handle_standart_reply(buf);
      if (std_reply != 0) {
        throw nprpc::Exception("Unknown Error");
      }

      FAIL() << "Expected nprpc::ExceptionBadInput to be thrown for stream init";
    } catch (nprpc::ExceptionBadInput&) {
      SUCCEED();
    } catch (nprpc::Exception& ex) {
      FAIL() << "Unexpected exception in TestBadInput stream path: " << ex.what();
    }
  };

  exec_test(nprpc::ObjectActivationFlags::tcp);
  exec_test(nprpc::ObjectActivationFlags::ws);
  exec_test(nprpc::ObjectActivationFlags::wss);
  exec_test(nprpc::ObjectActivationFlags::shm);
  exec_test(nprpc::ObjectActivationFlags::quic);
  exec_stream_test(nprpc::ObjectActivationFlags::tcp);
  exec_stream_test(nprpc::ObjectActivationFlags::ws);
  exec_stream_test(nprpc::ObjectActivationFlags::wss);
  exec_stream_test(nprpc::ObjectActivationFlags::shm);
  exec_stream_test(nprpc::ObjectActivationFlags::quic);
}

#ifdef NPRPC_HAS_QUIC
// QUIC transport test - basic RPC over QUIC streams
TEST_F(NprpcTest, TestQuicBasic)
{
#include "common/tests/basic.inl"
  TestBasicImpl servant;

  try {
    auto obj = bind_and_resolve<nprpc::test::TestBasic>(
        servant, nprpc::ObjectActivationFlags::quic, "quic_basic_test");

    // ReturnBoolean test
    EXPECT_TRUE(obj->ReturnBoolean());

    // In/Out test
    std::vector<uint8_t> ints;
    ints.reserve(256);
    boost::push_back(ints, boost::irange(0, 255));

    EXPECT_TRUE(obj->In_(100, true, nprpc::flat::make_read_only_span(ints)));

    uint32_t a;
    bool b;

    obj->Out(a, b, ints);

    EXPECT_EQ(a, 100u);
    EXPECT_TRUE(b);

    uint8_t ix = 0;
    for (auto i : ints) {
      EXPECT_EQ(ix++, i);
    }

    // ReturnU32 test
    EXPECT_EQ(obj->ReturnU32(), 42u);
  } catch (nprpc::Exception& ex) {
    FAIL() << "Exception in TestQuicBasic: " << ex.what();
  }
}

// QUIC transport test - unreliable delivery via DATAGRAM
TEST_F(NprpcTest, TestQuicUnreliable)
{
  class TestUnreliableImpl : public nprpc::test::ITestUnreliable_Servant
  {
  public:
    std::atomic<int> fire_and_forget_count{0};
    std::atomic<uint32_t> last_a{0};
    std::atomic<bool> last_b{false};
    std::mutex mtx_;
    std::condition_variable cv_;

    void FireAndForget(uint32_t a,
                       ::nprpc::flat::Boolean b,
                       ::nprpc::flat::Span<uint8_t> c) override
    {
      last_a = a;
      last_b = static_cast<bool>(b);
      fire_and_forget_count++;
      cv_.notify_all();
    }

    bool ReliableCall(uint32_t a,
                      ::nprpc::flat::Boolean b,
                      ::nprpc::flat::Span<uint8_t> c) override
    {
      return a == 42 && static_cast<bool>(b) == true && c.size() == 10;
    }

    bool wait_for_count(int expected, int timeout_ms = 2000)
    {
      std::unique_lock<std::mutex> lock(mtx_);
      return cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                          [&] { return fire_and_forget_count >= expected; });
    }
  } servant;

  try {
    auto obj = bind_and_resolve<nprpc::test::TestUnreliable>(
        servant, nprpc::ObjectActivationFlags::quic, "quic_unreliable_test");

    // Test reliable call first (over QUIC stream)
    std::vector<uint8_t> data(10, 0x55);
    bool result =
        obj->ReliableCall(42, true, nprpc::flat::make_read_only_span(data));
    EXPECT_TRUE(result);

    // Test fire-and-forget (over QUIC DATAGRAM)
    const auto datagrams_before = nprpc::impl::quic_transport_stats().datagrams_sent.load();
    for (int i = 0; i < 10; i++) {
      obj->FireAndForget(i, i % 2 == 0, nprpc::flat::make_read_only_span(data));
    }

    // Wait for messages to be processed
    EXPECT_TRUE(servant.wait_for_count(10));
    EXPECT_EQ(servant.fire_and_forget_count.load(), 10);
    EXPECT_EQ(servant.last_a.load(), 9u); // Last call had a=9
    EXPECT_FALSE(servant.last_b.load());  // 9 % 2 != 0
    EXPECT_EQ(nprpc::impl::quic_transport_stats().datagrams_sent.load(), datagrams_before + 10);

    // Too large for one packet: refused rather than sent, and nothing else minds.
    std::vector<uint8_t> oversized(64 * 1024, 0x55);
    obj->FireAndForget(100, true, nprpc::flat::make_read_only_span(oversized));
    EXPECT_EQ(nprpc::impl::quic_transport_stats().datagrams_sent.load(), datagrams_before + 10);
    EXPECT_FALSE(servant.wait_for_count(11, 100));
    EXPECT_TRUE(obj->ReliableCall(42, true, nprpc::flat::make_read_only_span(data)));
  } catch (nprpc::Exception& ex) {
    FAIL() << "Exception in TestQuicUnreliable: " << ex.what();
  }
}
#endif // NPRPC_HAS_QUIC

// Cancellation test: pre-cancel a stop_source and verify OperationCancelled
// is thrown from the generated *Async stub.  Only the TCP transport uses
// UringClientConnection which supports true coroutine cancellation via
// stop_token; the test binds over TCP explicitly.
TEST_F(NprpcTest, TestCancellation)
{
#include "common/tests/basic.inl"
  TestBasicImpl servant;
  try {
    auto obj = bind_and_resolve<nprpc::test::TestBasic>(
        servant, nprpc::ObjectActivationFlags::tcp, "cancel_test_object");

    // Request stop before the call, so send_receive_coro detects it immediately.
    std::stop_source ss;
    ss.request_stop();

    bool caught = false;
    try {
      auto task = obj->ReturnU32Async(ss.get_token());
      task.get();
    } catch (const nprpc::OperationCancelled&) {
      caught = true;
    }
    EXPECT_TRUE(caught);
  } catch (nprpc::Exception& ex) {
    FAIL() << "Unexpected exception in TestCancellation: " << ex.what();
  }
}

// A coroutine goes straight from one reply to its next call, on whatever thread
// resumed it. That must not be a thread still holding locks for the reply.
TEST_F(NprpcTest, TestCoroutineCallsInSequence)
{
#include "common/tests/basic.inl"
  const std::pair<const char*, nprpc::ObjectActivationFlags> transports[] = {
#ifdef NPRPC_ENABLE_TCP
    {"tcp", nprpc::ObjectActivationFlags::tcp},
#endif
#ifdef NPRPC_WEBSOCKET_ENABLED
    {"ws", nprpc::ObjectActivationFlags::ws},
#endif
    {"shm", nprpc::ObjectActivationFlags::shm},
    {"quic", nprpc::ObjectActivationFlags::quic},
  };
  for (const auto& [name, flag] : transports) {
    SCOPED_TRACE(name);
    TestBasicImpl servant;
    auto obj = bind_and_resolve<nprpc::test::TestBasic>(
        servant, flag, std::string("coroutine_sequence_") + name);
    auto calls = [](nprpc::test::TestBasic* proxy) -> nprpc::Task<uint32_t> {
      uint32_t sum = 0;
      for (int i = 0; i < 5; ++i) sum += co_await proxy->ReturnU32Async();
      co_return sum;
    };
    auto task = calls(obj.get());
    std::future<uint32_t> result = std::async(std::launch::async, [&] { return task.get(); });
    ASSERT_EQ(result.wait_for(std::chrono::seconds(10)), std::future_status::ready)
        << "deadlocked";
    EXPECT_EQ(result.get(), 5 * 42u);
  }
}

// A stop request ends the wait for a call already sent, on every client transport,
// and the reply that comes later isn't mistaken for the next call's.
TEST_F(NprpcTest, TestCancellationInFlight)
{
#include "common/tests/basic.inl"
  struct SlowServant : TestBasicImpl {
    std::atomic<bool> slow{true};
    uint32_t ReturnU32() override
    {
      if (slow.exchange(false)) std::this_thread::sleep_for(std::chrono::milliseconds(800));
      return 42;
    }
  };

  const std::pair<const char*, nprpc::ObjectActivationFlags> transports[] = {
#ifdef NPRPC_ENABLE_TCP
    {"tcp", nprpc::ObjectActivationFlags::tcp},
#endif
#ifdef NPRPC_WEBSOCKET_ENABLED
    {"ws", nprpc::ObjectActivationFlags::ws},
#endif
    {"shm", nprpc::ObjectActivationFlags::shm},
    {"quic", nprpc::ObjectActivationFlags::quic},
  };
  for (const auto& [name, flag] : transports) {
    SCOPED_TRACE(name);
    try {
    SlowServant servant;
    auto obj = bind_and_resolve<nprpc::test::TestBasic>(
        servant, flag, std::string("cancel_in_flight_") + name);
    obj->set_timeout(10000);

    std::stop_source stop;
    std::jthread canceller([&stop] {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      stop.request_stop();
    });

    const auto started = std::chrono::steady_clock::now();
    EXPECT_THROW(obj->ReturnU32Async(stop.get_token()).get(), nprpc::OperationCancelled);
    EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::milliseconds(600))
        << "should stop waiting without the reply";

    EXPECT_EQ(obj->ReturnU32Async().get(), 42u);
    } catch (const std::exception& e) {
      ADD_FAILURE() << "Unexpected exception: " << e.what();
    }
  }
}

// A cancelled call reaches the servant, on every transport that hands off.
TEST_F(NprpcTest, CancelledCallStopsServant)
{
#include "common/tests/basic.inl"
  struct PollingServant : TestBasicImpl {
    std::promise<void> entered;
    std::promise<bool> stopped;
    std::atomic<int> later_calls{0};
    uint32_t ReturnU32() override
    {
      entered.set_value();
      const auto token = nprpc::get_call_context().stop_token;
      const auto give_up = std::chrono::steady_clock::now() + std::chrono::seconds(5);
      while (!token.stop_requested() && std::chrono::steady_clock::now() < give_up)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      stopped.set_value(token.stop_requested());
      return 42;
    }
    bool ReturnBoolean() override
    {
      later_calls.fetch_add(1);
      return true;
    }
  };

  auto* pool_poa = rpc->create_poa()
                       .with_max_objects(8)
                       .with_lifespan(nprpc::PoaPolicy::Lifespan::Persistent)
                       .with_transport_affinity(
                           nprpc::PoaPolicy::TransportAffinity::NeverBlockTransport)
                       .build();

  const std::pair<const char*, nprpc::ObjectActivationFlags> transports[] = {
    {"tcp", nprpc::ObjectActivationFlags::tcp},
    {"ws", nprpc::ObjectActivationFlags::ws},
    {"shm", nprpc::ObjectActivationFlags::shm},
#if defined(NPRPC_HAS_QUIC) || defined(NPRPC_QUIC_ENABLED)
    {"quic", nprpc::ObjectActivationFlags::quic},
#endif
  };
  for (const auto& [name, flag] : transports) {
    SCOPED_TRACE(name);
    PollingServant servant;
    auto oid = pool_poa->activate_object(&servant, flag);
    nprpc::Object* raw = nprpc::Object::from_string(oid.to_string());
    auto obj = nprpc::ObjectPtr(nprpc::narrow<nprpc::test::TestBasic>(raw));
    ASSERT_TRUE(obj);
    obj->set_timeout(10000);
    ASSERT_TRUE(obj->select_endpoint());

    auto entered = servant.entered.get_future();
    auto stopped = servant.stopped.get_future();
    std::stop_source running, queued;

    auto running_call = std::async(std::launch::async, [&] {
      return obj->ReturnU32Async(running.get_token()).get();
    });
    ASSERT_EQ(entered.wait_for(std::chrono::seconds(5)), std::future_status::ready);

    // This one waits behind the first, so cancelling it means it never starts.
    auto queued_call = std::async(std::launch::async, [&] {
      return obj->ReturnBooleanAsync(queued.get_token()).get();
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    queued.request_stop();
    EXPECT_THROW(queued_call.get(), nprpc::OperationCancelled);

    // Give the cancel for the queued call time to arrive before the queue moves.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    running.request_stop();
    EXPECT_THROW(running_call.get(), nprpc::OperationCancelled);
    ASSERT_EQ(stopped.wait_for(std::chrono::seconds(6)), std::future_status::ready);
    EXPECT_TRUE(stopped.get()) << "the servant never saw the cancellation";

    // The session is still in step, and only what was cancelled was skipped.
    EXPECT_TRUE(obj->ReturnBooleanAsync().get());
    EXPECT_EQ(servant.later_calls.load(), 1);

    pool_poa->deactivate_object(servant.oid());
  }
  rpc->destroy_poa(pool_poa);
}

// Replies go to the call they name, whatever order they arrive in, and one that
// names nobody is dropped.
TEST_F(NprpcTest, SharedMemoryRepliesMatchById)
{
  namespace impl = nprpc::impl;
  using Frame = std::vector<std::byte>;
  constexpr size_t total = sizeof(impl::flat::Header) + sizeof(uint32_t);

  auto write_frame = [](impl::SharedMemoryChannel& channel, const void* data) {
    auto reservation = channel.reserve_write(total);
    ASSERT_TRUE(reservation);
    std::memcpy(reservation.data, data, total);
    channel.commit_write(reservation, total);
  };

  // Holds requests back until it has them all, then answers newest first, after a
  // reply for a call that doesn't exist.
  constexpr size_t calls = 3;
  std::mutex mutex;
  std::vector<Frame> received;
  std::unique_ptr<impl::SharedMemoryChannel> server;

  boost::asio::io_context listener_io;
  const auto name = "test_reply_match_" + std::to_string(::getpid());
  impl::SharedMemoryListener listener(
      listener_io, name, [&](std::unique_ptr<impl::SharedMemoryChannel> channel) {
        auto* raw = channel.get();
        raw->on_data_received_view = [&, raw](const impl::LockFreeRingBuffer::ReadView& view) {
          std::lock_guard lock(mutex);
          const auto* bytes = reinterpret_cast<const std::byte*>(view.data);
          received.emplace_back(bytes, bytes + view.size);
          if (received.size() != calls)
            return;
          Frame stray = received.back();
          reinterpret_cast<impl::flat::Header*>(stray.data())->request_id = 0x7fffffff;
          write_frame(*raw, stray.data());
          for (auto it = received.rbegin(); it != received.rend(); ++it)
            write_frame(*raw, it->data());
        };
        raw->start_reading();
        server = std::move(channel);
      });
  listener.start();

  auto client = std::make_shared<impl::SharedMemoryConnection>(
      nprpc::EndPoint(nprpc::EndPointType::SharedMemory, name, 0), nprpc::get_io_context());
  client->bind_self(client);

  std::atomic<int> right{0}, wrong{0};
  std::latch finished{calls};
  for (uint32_t marker = 1; marker <= calls; ++marker) {
    nprpc::flat_buffer request;
    request.prepare(total);
    request.commit(total);
    auto* header = static_cast<impl::flat::Header*>(request.data().data());
    header->size = total;
    header->msg_id = impl::MessageId::Success;
    header->msg_type = impl::MessageType::Answer;
    std::memcpy(static_cast<std::byte*>(request.data().data()) + sizeof(*header),
                &marker, sizeof(marker));
    client->send_receive_async(
        std::move(request),
        [&, marker](const boost::system::error_code& ec, nprpc::flat_buffer& reply) {
          uint32_t got = 0;
          if (!ec && reply.size() == total)
            std::memcpy(&got, static_cast<const std::byte*>(reply.cdata().data()) +
                                  sizeof(impl::flat::Header), sizeof(got));
          (got == marker ? right : wrong).fetch_add(1);
          finished.count_down();
        },
        5000);
  }

  finished.wait();
  EXPECT_EQ(right.load(), static_cast<int>(calls));
  EXPECT_EQ(wrong.load(), 0);

  client->shutdown();
  if (server)
    server->stop_reading();
}

namespace {
// Resumes whoever awaits it when opened, or when the token stops.
class Gate
{
  std::mutex mutex_;
  std::coroutine_handle<> waiting_;
  bool open_ = false;
public:
  void open()
  {
    std::coroutine_handle<> waiting;
    {
      std::lock_guard lock(mutex_);
      open_ = true;
      waiting = std::exchange(waiting_, {});
    }
    if (waiting) waiting.resume();
  }
  bool await_ready() noexcept { return false; }
  bool await_suspend(std::coroutine_handle<> h)
  {
    std::lock_guard lock(mutex_);
    if (open_) return false;
    waiting_ = h;
    return true;
  }
  void await_resume() noexcept {}
};

// The same wait, reached through each form of operator co_await.
struct GateWaiter {
  Gate& gate;
  bool await_ready() noexcept { return gate.await_ready(); }
  bool await_suspend(std::coroutine_handle<> h) { return gate.await_suspend(h); }
  void await_resume() noexcept {}
};
struct MemberCoAwait {
  Gate& gate;
  GateWaiter operator co_await() { return {gate}; }
};
struct FreeCoAwait {
  Gate& gate;
};
Gate& operator co_await(FreeCoAwait&& awaitable) { return awaitable.gate; }

class CoroutineServant : public nprpc::test::ITestCoroutine_Servant
{
public:
  std::shared_ptr<Gate> gate = std::make_shared<Gate>();
  std::promise<void> entered;
  std::atomic<bool> saw_stop{false};
  std::atomic<int> resumed_on_wrong_thread{0};
  std::atomic<std::thread::id> affinity{};
  nprpc::test::TestCoroutine* nested = nullptr;

  void check_thread()
  {
    if (affinity.load() != std::thread::id{} && affinity.load() != std::this_thread::get_id())
      resumed_on_wrong_thread.fetch_add(1);
  }

  nprpc::Task<uint32_t> Add(std::stop_token, uint32_t a, uint32_t b) override
  {
    check_thread();
    if (a == 0) throw nprpc::test::SimpleException{"zero", 7};
    if (nested) {
      const auto more = co_await nested->QuickAsync();
      check_thread();
      co_return a + b + more;
    }
    co_return a + b;
  }
  nprpc::Task<std::string> Echo(std::stop_token, ::nprpc::flat::Span<char> text) override
  {
    // Still readable after the session has moved on.
    const auto waiting_on = gate;
    if (std::string_view(text) == "member")
      co_await MemberCoAwait{*waiting_on};
    else if (std::string_view(text) == "free")
      co_await FreeCoAwait{*waiting_on};
    else
      co_await *waiting_on;
    check_thread();
    co_return std::string((std::string_view)text);
  }
  nprpc::Task<> Wait(std::stop_token stop) override
  {
    auto keep = gate;
    std::stop_callback on_stop(stop, [this, keep] {
      saw_stop = true;
      keep->open();
    });
    entered.set_value();
    co_await *keep;
    check_thread();
    if (stop.stop_requested()) throw nprpc::OperationCancelled{};
  }
  uint32_t Quick() override { return 5; }
};

const std::pair<const char*, nprpc::ObjectActivationFlags> coroutine_transports[] = {
  {"tcp", nprpc::ObjectActivationFlags::tcp},
  {"ws", nprpc::ObjectActivationFlags::ws},
  {"shm", nprpc::ObjectActivationFlags::shm},
#if defined(NPRPC_HAS_QUIC) || defined(NPRPC_QUIC_ENABLED)
  {"quic", nprpc::ObjectActivationFlags::quic},
#endif
};

nprpc::ObjectPtr<nprpc::test::TestCoroutine> resolve_coroutine(
    nprpc::Poa* poa, nprpc::ObjectServant& servant, nprpc::ObjectActivationFlags flag)
{
  auto oid = poa->activate_object(&servant, flag);
  nprpc::Object* raw = nprpc::Object::from_string(oid.to_string());
  auto obj = nprpc::ObjectPtr(nprpc::narrow<nprpc::test::TestCoroutine>(raw));
  obj->set_timeout(10000);
  EXPECT_TRUE(obj->select_endpoint());
  return obj;
}
} // namespace

// While a coroutine method waits, its session serves other calls, and the first
// call's reply arrives after theirs, intact.
TEST_F(NprpcTest, CoroutineMethodDoesNotHoldUpSession)
{
  for (const auto& [name, flag] : coroutine_transports) {
    SCOPED_TRACE(name);
    CoroutineServant servant;
    auto obj = resolve_coroutine(poa, servant, flag);

    EXPECT_EQ(obj->Add(2, 3), 5u);
    try {
      obj->Add(0, 1);
      ADD_FAILURE() << "expected SimpleException";
    } catch (const nprpc::test::SimpleException& e) {
      EXPECT_EQ(e.code, 7u);
    }

    const std::string text(5000, 'x');
    auto waiting = std::async(std::launch::async, [&] { return obj->Echo(text); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(waiting.wait_for(std::chrono::milliseconds(0)), std::future_status::timeout);

    for (int i = 0; i < 20; ++i)
      EXPECT_EQ(obj->Quick(), 5u);

    servant.gate->open();
    EXPECT_EQ(waiting.get(), text);

    poa->deactivate_object(servant.oid());
  }
}

// Cancelling reaches a coroutine method wherever it's suspended, on a default POA too.
TEST_F(NprpcTest, CoroutineMethodCancelled)
{
  for (const auto& [name, flag] : coroutine_transports) {
    SCOPED_TRACE(name);
    CoroutineServant servant;
    auto obj = resolve_coroutine(poa, servant, flag);

    auto entered = servant.entered.get_future();
    std::stop_source stop;
    auto call = std::async(std::launch::async, [&] {
      obj->WaitAsync(stop.get_token()).get();
    });
    ASSERT_EQ(entered.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    stop.request_stop();
    EXPECT_THROW(call.get(), nprpc::OperationCancelled);

    for (int i = 0; i < 500 && !servant.saw_stop; ++i)
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_TRUE(servant.saw_stop);
    EXPECT_EQ(obj->Quick(), 5u); // still in step

    poa->deactivate_object(servant.oid());
  }
}

// After every co_await, the method is back on its POA's executor.
TEST_F(NprpcTest, CoroutineMethodKeepsExecutor)
{
  struct Queue {
    std::mutex m;
    std::condition_variable cv;
    std::deque<std::function<void()>> jobs;
    bool stop = false;
    std::atomic<std::thread::id> worker_id{};
  } queue;
  std::thread worker([&] {
    queue.worker_id = std::this_thread::get_id();
    std::unique_lock lk(queue.m);
    for (;;) {
      queue.cv.wait(lk, [&] { return queue.stop || !queue.jobs.empty(); });
      if (queue.jobs.empty())
        return;
      auto job = std::move(queue.jobs.front());
      queue.jobs.pop_front();
      lk.unlock();
      job();
      lk.lock();
    }
  });
  while (queue.worker_id.load() == std::thread::id{})
    std::this_thread::yield();

  nprpc::DispatchExecutor ex;
  ex.ctx = &queue;
  ex.post = [](void* c, void (*fn)(void*), void* arg) {
    auto* q = static_cast<Queue*>(c);
    {
      std::lock_guard lk(q->m);
      q->jobs.push_back([fn, arg] { fn(arg); });
    }
    q->cv.notify_one();
  };
  ex.is_running_on = [](void* c) {
    return std::this_thread::get_id() == static_cast<Queue*>(c)->worker_id.load();
  };
  auto* exec_poa = rpc->create_poa()
                       .with_max_objects(8)
                       .with_lifespan(nprpc::PoaPolicy::Lifespan::Persistent)
                       .with_dispatch_executor(ex)
                       .build();

  for (const auto& [name, flag] : coroutine_transports) {
    SCOPED_TRACE(name);
    CoroutineServant inner, servant;
    auto inner_obj = resolve_coroutine(poa, inner, flag);
    auto obj = resolve_coroutine(exec_poa, servant, flag);
    servant.affinity = queue.worker_id.load();
    servant.nested = inner_obj.get();

    for (int i = 0; i < 20; ++i)
      EXPECT_EQ(obj->Add(1, 2), 8u);

    for (const std::string text : {"after a wait", "member", "free"}) {
      SCOPED_TRACE(text);
      servant.resumed_on_wrong_thread = 0;
      servant.gate = std::make_shared<Gate>();
      auto echo = std::async(std::launch::async, [&] { return obj->Echo(text); });
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      std::thread opener([gate = servant.gate] { gate->open(); }); // not the executor
      EXPECT_EQ(echo.get(), text);
      opener.join();
      EXPECT_EQ(servant.resumed_on_wrong_thread.load(), 0);
    }

    exec_poa->deactivate_object(servant.oid());
    poa->deactivate_object(inner.oid());
  }

  rpc->destroy_poa(exec_poa);
  {
    std::lock_guard lk(queue.m);
    queue.stop = true;
  }
  queue.cv.notify_one();
  worker.join();
}

// A reply too big for the ring fails the call at once, rather than timing it out.
TEST_F(NprpcTest, SharedMemoryReplyOverLimitFailsTheCall)
{
  constexpr size_t over_limit = 13 * 1024 * 1024; // the fixture's rings allow 12 MiB

  struct LargeReplies : CoroutineServant {
    nprpc::Task<std::string> Echo(std::stop_token, ::nprpc::flat::Span<char>) override
    {
      co_return std::string(over_limit, 'x');
    }
  };

  auto* poa = rpc->create_poa()
                  .with_max_objects(4)
                  .with_lifespan(nprpc::PoaPolicy::Lifespan::Persistent)
                  .build();
  LargeReplies servant;
  auto obj = resolve_coroutine(poa, servant, nprpc::ObjectActivationFlags::shm);
  obj->set_timeout(30'000);

  const auto started = std::chrono::steady_clock::now();
  EXPECT_THROW(obj->Echo("anything"), nprpc::ExceptionCommFailure);
  EXPECT_THROW(obj->EchoAsync("anything").get(), nprpc::ExceptionCommFailure);
  EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(10));

  // The session is still usable.
  EXPECT_EQ(obj->Quick(), 5u);

  poa->deactivate_object(servant.oid());
  rpc->destroy_poa(poa);
}

TEST_F(NprpcTest, SharedMemoryPlainReplyOverLimitFailsTheCall)
{
#include "common/tests/large_message.inl"
  struct LargeReplies : TestLargeMessage {
    void Out(uint32_t& a, ::nprpc::flat::Boolean& b,
             ::nprpc::flat::Vector_Direct1<uint8_t> c) override
    {
      a = 42;
      b = true;
      c.length(13 * 1024 * 1024); // the fixture's rings allow 12 MiB
    }
  };

  LargeReplies servant;
  auto obj = bind_and_resolve<nprpc::test::TestLargeMessage>(
      servant, nprpc::ObjectActivationFlags::shm);
  obj->set_timeout(30'000);

  const auto started = std::chrono::steady_clock::now();
  uint32_t a;
  bool b;
  std::vector<uint8_t> received;
  EXPECT_THROW(obj->Out(a, b, received), nprpc::ExceptionCommFailure);
  EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(10));

  // The session is still usable.
  std::vector<uint8_t> data(3 * 1024 * 1024);
  data.front() = 0xAB;
  data.back() = 0xCD;
  EXPECT_TRUE(obj->In_(42, true, nprpc::flat::make_read_only_span(data)));
}

// A client that disconnects stops the call it left running.
TEST_F(NprpcTest, DisconnectStopsServant)
{
#include "common/tests/basic.inl"
  struct PollingServant : TestBasicImpl {
    std::promise<void> entered;
    std::promise<bool> stopped;
    uint32_t ReturnU32() override
    {
      entered.set_value();
      const auto token = nprpc::get_call_context().stop_token;
      const auto give_up = std::chrono::steady_clock::now() + std::chrono::seconds(5);
      while (!token.stop_requested() && std::chrono::steady_clock::now() < give_up)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      stopped.set_value(token.stop_requested());
      return 42;
    }
  } servant;

  auto* pool_poa = rpc->create_poa()
                       .with_max_objects(8)
                       .with_lifespan(nprpc::PoaPolicy::Lifespan::Persistent)
                       .with_transport_affinity(
                           nprpc::PoaPolicy::TransportAffinity::NeverBlockTransport)
                       .build();
  auto oid = pool_poa->activate_object(&servant, nprpc::ObjectActivationFlags::tcp);
  nprpc::Object* raw = nprpc::Object::from_string(oid.to_string());
  auto obj = nprpc::ObjectPtr(nprpc::narrow<nprpc::test::TestBasic>(raw));
  ASSERT_TRUE(obj);
  ASSERT_TRUE(obj->select_endpoint());
  const auto port = obj->get_endpoint().port();

  // A connection of its own, so closing it leaves the runtime's sessions alone.
  constexpr size_t total = sizeof(nprpc::impl::flat::Header) + sizeof(nprpc::impl::flat::CallHeader);
  std::vector<std::byte> frame(total);
  auto* header = reinterpret_cast<nprpc::impl::flat::Header*>(frame.data());
  header->size = total;
  header->msg_id = nprpc::impl::MessageId::FunctionCall;
  header->msg_type = nprpc::impl::MessageType::Request;
  header->request_id = 7;
  auto* call = reinterpret_cast<nprpc::impl::flat::CallHeader*>(frame.data() + sizeof(*header));
  call->poa_idx = obj->poa_idx();
  call->interface_idx = 0;
  call->function_idx = 2; // ReturnU32
  call->object_id = obj->object_id();

  boost::asio::io_context io;
  boost::asio::ip::tcp::socket socket(io);
  socket.connect(boost::asio::ip::tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), port));
  boost::asio::write(socket, boost::asio::buffer(frame.data(), frame.size()));

  auto entered = servant.entered.get_future();
  auto stopped = servant.stopped.get_future();
  ASSERT_EQ(entered.wait_for(std::chrono::seconds(5)), std::future_status::ready);
  socket.close();
  ASSERT_EQ(stopped.wait_for(std::chrono::seconds(6)), std::future_status::ready);
  EXPECT_TRUE(stopped.get());

  pool_poa->deactivate_object(servant.oid());
  rpc->destroy_poa(pool_poa);
}

} // namespace nprpctest


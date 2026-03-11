#include <chrono>
#include <coroutine>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <numeric>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <condition_variable>
#include <filesystem>
#include <fstream>

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

      std::cout << "In/Out passed" << std::endl;

      uint32_t a;
      bool b;

      obj->Out(a, b, ints);

      EXPECT_EQ(a, 100u);
      EXPECT_TRUE(b);

      uint8_t ix = 0;
      for (auto i : ints) {
        EXPECT_EQ(ix++, i);
      }

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
  EXPECT_NE(text.find("\"urls\": \"ws://localhost:22223;wss://localhost:22223;http://localhost:22223;https://localhost:22223;\""),
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

      std::cout << "Testing large message transmission (3MB)..." << std::endl;

      // This should work with our async_write fix
      EXPECT_TRUE(
          obj->In_(42, true, nprpc::flat::make_read_only_span(large_data)));

      // Test receiving 3MB of data
      uint32_t a;
      bool b;
      std::vector<uint8_t> received_data;

      std::cout << "Testing large message reception (3MB)..." << std::endl;
      obj->Out(a, b, received_data);

      EXPECT_EQ(a, 42u);
      EXPECT_TRUE(b);
      EXPECT_EQ(received_data.size(), 3 * 1024 * 1024u);
      EXPECT_EQ(received_data[0], 0xAB);
      EXPECT_EQ(received_data[received_data.size() - 1], 0xCD);

      std::cout << "Large message test completed successfully!" << std::endl;

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
          static_cast<uint32_t>(buf.size() - 4);
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
  exec_test(nprpc::ObjectActivationFlags::tcp);
  exec_test(nprpc::ObjectActivationFlags::ws);
  exec_test(nprpc::ObjectActivationFlags::wss);
  exec_test(nprpc::ObjectActivationFlags::shm);
  exec_test(nprpc::ObjectActivationFlags::quic);
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

    std::cout << "QUIC basic test completed successfully!" << std::endl;

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
    for (int i = 0; i < 10; i++) {
      obj->FireAndForget(i, i % 2 == 0, nprpc::flat::make_read_only_span(data));
    }

    // Wait for messages to be processed
    EXPECT_TRUE(servant.wait_for_count(10));
    EXPECT_EQ(servant.fire_and_forget_count.load(), 10);
    EXPECT_EQ(servant.last_a.load(), 9u); // Last call had a=9
    EXPECT_FALSE(servant.last_b.load());  // 9 % 2 != 0

    std::cout << "QUIC unreliable test completed successfully!" << std::endl;

  } catch (nprpc::Exception& ex) {
    FAIL() << "Exception in TestQuicUnreliable: " << ex.what();
  }
}
#endif // NPRPC_HAS_QUIC

// Streaming test
TEST_F(NprpcTest, TestStreams)
{
#include "common/tests/streams.inl"
  TestStreamsImpl servant;
  auto exec_test = [this, &servant](nprpc::ObjectActivationFlags flags) {
    try {
      auto obj = bind_and_resolve<nprpc::test::TestStreams>(servant, flags, "streams_test");

      // Request a stream of 5 bytes
      auto reader = obj->GetByteStream(5);

      std::vector<uint8_t> received;
      received.reserve(5);

      // Read all chunks from the stream
      for (auto& chunk : reader) {
        std::cout << "[CLIENT] Received a byte." << std::endl;
        received.push_back(chunk);
      }

      // Verify we received all expected bytes
      EXPECT_EQ(received.size(), 5u);
      for (uint64_t i = 0; i < received.size(); ++i) {
        EXPECT_EQ(received[i], static_cast<uint8_t>(i & 0xFF));
      }

      std::cout << "Stream test passed for transport" << std::endl;

    } catch (nprpc::Exception& ex) {
      FAIL() << "Exception in TestStreams: " << ex.what();
    }
  };

  exec_test(nprpc::ObjectActivationFlags::ws);
  exec_test(nprpc::ObjectActivationFlags::quic);
  // TODO: TCP stream crashes...
  // exec_test(nprpc::ObjectActivationFlags::tcp);
}

TEST_F(NprpcTest, TestObjectStream)
{
#include "common/tests/streams.inl"
  TestStreamsImpl servant;
  auto exec_test = [this, &servant](nprpc::ObjectActivationFlags flags) {
    try {
      auto obj = bind_and_resolve<nprpc::test::TestStreams>(servant, flags, "object_stream_test");

      constexpr uint32_t kCount = 4;
      auto reader = obj->GetObjectStream(kCount);

      std::vector<nprpc::test::AAA> received;
      received.reserve(kCount);

      for (auto& item : reader) {
        received.push_back(item);
      }

      ASSERT_EQ(received.size(), kCount);
      for (uint32_t i = 0; i < kCount; ++i) {
        EXPECT_EQ(received[i].a, i);
        EXPECT_EQ(received[i].b, "name_" + std::to_string(i));
        EXPECT_EQ(received[i].c, "value_" + std::to_string(i));
      }

      // Test direct stream - verifies that OwnedDirect wrapper can be used with streaming APIs and that data is correctly received without corruption
      auto reader_direct = obj->GetObjectStreamDirect(kCount);

      std::vector<::nprpc::flat::OwnedDirect<nprpc::test::flat::AAA_Direct>> received_direct;
      received_direct.reserve(kCount);

      for (auto& item : reader_direct) {
        received_direct.push_back(item);
      }

      ASSERT_EQ(received_direct.size(), kCount);
      for (uint32_t i = 0; i < kCount; ++i) {
        EXPECT_EQ(received_direct[i].get().a(), i);
        EXPECT_EQ((std::string)received_direct[i].get().b(), "name_" + std::to_string(i));
        EXPECT_EQ((std::string)received_direct[i].get().c(), "value_" + std::to_string(i));
      }

      auto string_reader = obj->GetStringStream(3);
      std::vector<std::string> string_values;
      for (auto& value : string_reader) {
        string_values.push_back(value);
      }

      EXPECT_EQ(string_values, (std::vector<std::string>{"item_0", "item_1", "item_2"}));

      auto binary_reader = obj->GetBinaryStream(3);
      std::vector<std::vector<uint8_t>> binary_values;
      for (auto& value : binary_reader) {
        binary_values.push_back(value);
      }

      ASSERT_EQ(binary_values.size(), 3u);
      EXPECT_EQ(binary_values[0], (std::vector<uint8_t>{0, 1, 2}));
      EXPECT_EQ(binary_values[1], (std::vector<uint8_t>{1, 2, 3}));
      EXPECT_EQ(binary_values[2], (std::vector<uint8_t>{2, 3, 4}));

      auto aliased_binary_reader = obj->GetAliasedBinaryStream(2);
      std::vector<std::vector<uint8_t>> aliased_binary_values;
      for (auto& value : aliased_binary_reader) {
        aliased_binary_values.push_back(value);
      }

      ASSERT_EQ(aliased_binary_values.size(), 2u);
      EXPECT_EQ(aliased_binary_values[0], (std::vector<uint8_t>{10, 11, 12}));
      EXPECT_EQ(aliased_binary_values[1], (std::vector<uint8_t>{11, 12, 13}));

      auto u16_vector_reader = obj->GetU16VectorStream(3);
      std::vector<std::vector<uint16_t>> u16_vector_values;
      for (auto& value : u16_vector_reader) {
        u16_vector_values.push_back(value);
      }

      ASSERT_EQ(u16_vector_values.size(), 3u);
      EXPECT_EQ(u16_vector_values[0], (std::vector<uint16_t>{100, 200, 300}));
      EXPECT_EQ(u16_vector_values[1], (std::vector<uint16_t>{101, 201, 301}));
      EXPECT_EQ(u16_vector_values[2], (std::vector<uint16_t>{102, 202, 302}));

      auto object_vector_reader = obj->GetObjectVectorStream(2);
      std::vector<std::vector<nprpc::test::AAA>> object_vector_values;
      for (auto& value : object_vector_reader) {
        object_vector_values.push_back(value);
      }

      ASSERT_EQ(object_vector_values.size(), 2u);
      ASSERT_EQ(object_vector_values[0].size(), 2u);
      EXPECT_EQ(object_vector_values[0][0].a, 1u);
      EXPECT_EQ(object_vector_values[0][0].b, "vec_0_0");
      EXPECT_EQ(object_vector_values[0][0].c, "payload_0_0");
      EXPECT_EQ(object_vector_values[1][1].a, 12u);
      EXPECT_EQ(object_vector_values[1][1].b, "vec_1_1");
      EXPECT_EQ(object_vector_values[1][1].c, "payload_1_1");

      auto u16_array_reader = obj->GetU16ArrayStream(3);
      std::vector<std::array<uint16_t, 4>> u16_array_values;
      for (auto& value : u16_array_reader) {
        u16_array_values.push_back(value);
      }

      ASSERT_EQ(u16_array_values.size(), 3u);
      EXPECT_EQ(u16_array_values[0], (std::array<uint16_t, 4>{0, 10, 20, 30}));
      EXPECT_EQ(u16_array_values[1], (std::array<uint16_t, 4>{1, 11, 21, 31}));
      EXPECT_EQ(u16_array_values[2], (std::array<uint16_t, 4>{2, 12, 22, 32}));

      auto object_array_reader = obj->GetObjectArrayStream(2);
      std::vector<std::array<nprpc::test::AAA, 2>> object_array_values;
      for (auto& value : object_array_reader) {
        object_array_values.push_back(value);
      }

      ASSERT_EQ(object_array_values.size(), 2u);
      EXPECT_EQ(object_array_values[0][0].a, 1u);
      EXPECT_EQ(object_array_values[0][0].b, "arr_0_0");
      EXPECT_EQ(object_array_values[0][0].c, "item_0_0");
      EXPECT_EQ(object_array_values[1][1].a, 12u);
      EXPECT_EQ(object_array_values[1][1].b, "arr_1_1");
      EXPECT_EQ(object_array_values[1][1].c, "item_1_1");

      std::cout << "Object stream test passed for transport" << std::endl;

    } catch (nprpc::Exception& ex) {
      FAIL() << "Exception in TestObjectStream: " << ex.what();
    }
  };

  exec_test(nprpc::ObjectActivationFlags::ws);
  exec_test(nprpc::ObjectActivationFlags::quic);
}

TEST_F(NprpcTest, TestClientStream)
{
#include "common/tests/streams.inl"
  TestStreamsImpl servant;
  auto exec_test = [this, &servant](nprpc::ObjectActivationFlags flags) {
    try {
      auto obj = bind_and_resolve<nprpc::test::TestStreams>(servant, flags, "client_stream_test");

      std::vector<uint8_t> expected{1, 2, 3, 4, 5};
      auto writer = obj->UploadByteStream(expected.size());
      for (auto byte : expected) {
        writer.write(byte);
      }
      writer.close();

      EXPECT_TRUE(servant.wait_for_upload(expected));

      std::vector<nprpc::test::AAA> expected_objects{
        nprpc::test::AAA{.a = 1, .b = "first", .c = "one"},
        nprpc::test::AAA{.a = 2, .b = "second", .c = "two"},
        nprpc::test::AAA{.a = 3, .b = "third", .c = "three"},
      };

      auto object_writer = obj->UploadObjectStream(expected_objects.size());
      for (const auto& object : expected_objects) {
        object_writer.write(object);
      }
      object_writer.close();

      EXPECT_TRUE(servant.wait_for_object_upload(expected_objects));

      std::vector<std::string> expected_strings{"alpha", "beta", "gamma"};
      auto string_writer = obj->UploadStringStream(expected_strings.size());
      for (const auto& value : expected_strings) {
        string_writer.write(value);
      }
      string_writer.close();

      EXPECT_TRUE(servant.wait_for_string_upload(expected_strings));

      std::vector<std::vector<uint8_t>> expected_binary{
        {1, 2, 3},
        {4, 5},
        {6, 7, 8, 9},
      };
      auto binary_writer = obj->UploadBinaryStream(expected_binary.size());
      for (const auto& value : expected_binary) {
        binary_writer.write(value);
      }
      binary_writer.close();

      EXPECT_TRUE(servant.wait_for_binary_upload(expected_binary));

      auto aliased_binary_writer = obj->UploadAliasedBinaryStream(expected_binary.size());
      for (const auto& value : expected_binary) {
        aliased_binary_writer.write(value);
      }
      aliased_binary_writer.close();

      EXPECT_TRUE(servant.wait_for_binary_upload(expected_binary));

      std::vector<std::vector<uint16_t>> expected_u16_vectors{
        {10, 20, 30},
        {40, 50},
        {60, 70, 80, 90},
      };
      auto u16_vector_writer = obj->UploadU16VectorStream(expected_u16_vectors.size());
      for (const auto& value : expected_u16_vectors) {
        u16_vector_writer.write(value);
      }
      u16_vector_writer.close();

      EXPECT_TRUE(servant.wait_for_u16_vector_upload(expected_u16_vectors));

      std::vector<std::vector<nprpc::test::AAA>> expected_object_vectors{
        {
          nprpc::test::AAA{.a = 1, .b = "left_0", .c = "payload_0"},
          nprpc::test::AAA{.a = 2, .b = "left_1", .c = "payload_1"},
        },
        {
          nprpc::test::AAA{.a = 3, .b = "right_0", .c = "payload_2"},
          nprpc::test::AAA{.a = 4, .b = "right_1", .c = "payload_3"},
        },
      };
      auto object_vector_writer = obj->UploadObjectVectorStream(expected_object_vectors.size());
      for (const auto& value : expected_object_vectors) {
        object_vector_writer.write(value);
      }
      object_vector_writer.close();

      EXPECT_TRUE(servant.wait_for_object_vector_upload(expected_object_vectors));

      std::vector<std::array<uint16_t, 4>> expected_u16_arrays{
        std::array<uint16_t, 4>{1, 2, 3, 4},
        std::array<uint16_t, 4>{10, 20, 30, 40},
      };
      auto u16_array_writer = obj->UploadU16ArrayStream(expected_u16_arrays.size());
      for (const auto& value : expected_u16_arrays) {
        u16_array_writer.write(value);
      }
      u16_array_writer.close();

      EXPECT_TRUE(servant.wait_for_u16_array_upload(expected_u16_arrays));

      std::vector<std::array<nprpc::test::AAA, 2>> expected_object_arrays{
        std::array<nprpc::test::AAA, 2>{
          nprpc::test::AAA{.a = 5, .b = "array_0_0", .c = "item_0_0"},
          nprpc::test::AAA{.a = 6, .b = "array_0_1", .c = "item_0_1"},
        },
        std::array<nprpc::test::AAA, 2>{
          nprpc::test::AAA{.a = 7, .b = "array_1_0", .c = "item_1_0"},
          nprpc::test::AAA{.a = 8, .b = "array_1_1", .c = "item_1_1"},
        },
      };
      auto object_array_writer = obj->UploadObjectArrayStream(expected_object_arrays.size());
      for (const auto& value : expected_object_arrays) {
        object_array_writer.write(value);
      }
      object_array_writer.close();

      EXPECT_TRUE(servant.wait_for_object_array_upload(expected_object_arrays));
    } catch (nprpc::Exception& ex) {
      FAIL() << "Exception in TestClientStream: " << ex.what();
    }
  };

  exec_test(nprpc::ObjectActivationFlags::ws);
  exec_test(nprpc::ObjectActivationFlags::quic);
}

TEST_F(NprpcTest, TestBidiStream)
{
#include "common/tests/streams.inl"
  TestStreamsImpl servant;
  auto exec_test = [this, &servant](nprpc::ObjectActivationFlags flags) {
    try {
      auto obj = bind_and_resolve<nprpc::test::TestStreams>(servant, flags, "bidi_stream_test");

      std::vector<uint8_t> input{10, 11, 12, 13};
      constexpr uint8_t kMask = 0x5A;
      auto [writer, reader] = obj->EchoByteStream(kMask);

      for (auto byte : input) {
        writer.write(byte);
      }
      writer.close();

      std::vector<uint8_t> output;
      for (auto& byte : reader) {
        output.push_back(byte);
      }

      ASSERT_EQ(output.size(), input.size());
      for (size_t i = 0; i < input.size(); ++i) {
        EXPECT_EQ(output[i], static_cast<uint8_t>(input[i] ^ kMask));
      }

      std::vector<nprpc::test::AAA> object_input{
        nprpc::test::AAA{.a = 7, .b = "alpha", .c = "left"},
        nprpc::test::AAA{.a = 8, .b = "beta", .c = "right"},
      };
      const std::string suffix = "-ok";
      auto [object_writer, object_reader] = obj->EchoObjectStream(suffix);

      for (const auto& object : object_input) {
        object_writer.write(object);
      }
      object_writer.close();

      std::vector<nprpc::test::AAA> object_output;
      for (auto& object : object_reader) {
        object_output.push_back(object);
      }

      ASSERT_EQ(object_output.size(), object_input.size());
      for (size_t i = 0; i < object_input.size(); ++i) {
        EXPECT_EQ(object_output[i].a, object_input[i].a + 100);
        EXPECT_EQ(object_output[i].b, object_input[i].b + suffix);
        EXPECT_EQ(object_output[i].c, object_input[i].c + suffix);
      }

      auto [mixed_writer, mixed_reader] = obj->EchoObjectToDifferentObjectStream(suffix);
      for (const auto& object : object_input) {
        mixed_writer.write(object);
      }
      mixed_writer.close();

      std::vector<nprpc::test::CCC> mixed_output;
      for (auto& object : mixed_reader) {
        mixed_output.push_back(object);
      }

      ASSERT_EQ(mixed_output.size(), object_input.size());
      for (size_t i = 0; i < object_input.size(); ++i) {
        EXPECT_EQ(mixed_output[i].a, object_input[i].b + suffix);
        EXPECT_EQ(mixed_output[i].b, object_input[i].c + suffix);
        ASSERT_TRUE(mixed_output[i].c.has_value());
        EXPECT_EQ(mixed_output[i].c.value(), (object_input[i].a % 2u) == 0u);
      }

      auto [string_writer, string_reader] = obj->EchoStringStream("-ok");
      std::vector<std::string> string_input{"left", "right"};
      for (const auto& value : string_input) {
        string_writer.write(value);
      }
      string_writer.close();

      std::vector<std::string> string_output;
      for (auto& value : string_reader) {
        string_output.push_back(value);
      }

      EXPECT_EQ(string_output, (std::vector<std::string>{"left-ok", "right-ok"}));

      auto [binary_writer, binary_reader] = obj->EchoBinaryStream(kMask);
      std::vector<std::vector<uint8_t>> binary_input{
        {0, 1, 2},
        {10, 11},
      };
      for (const auto& value : binary_input) {
        binary_writer.write(value);
      }
      binary_writer.close();

      std::vector<std::vector<uint8_t>> binary_output;
      for (auto& value : binary_reader) {
        binary_output.push_back(value);
      }

      ASSERT_EQ(binary_output.size(), binary_input.size());
      for (size_t i = 0; i < binary_input.size(); ++i) {
        ASSERT_EQ(binary_output[i].size(), binary_input[i].size());
        for (size_t j = 0; j < binary_input[i].size(); ++j) {
          EXPECT_EQ(binary_output[i][j], static_cast<uint8_t>(binary_input[i][j] ^ kMask));
        }
      }

      auto [aliased_binary_writer, aliased_binary_reader] = obj->EchoAliasedBinaryStream(kMask);
      for (const auto& value : binary_input) {
        aliased_binary_writer.write(value);
      }
      aliased_binary_writer.close();

      std::vector<std::vector<uint8_t>> aliased_binary_output;
      for (auto& value : aliased_binary_reader) {
        aliased_binary_output.push_back(value);
      }

      ASSERT_EQ(aliased_binary_output.size(), binary_input.size());
      for (size_t i = 0; i < binary_input.size(); ++i) {
        ASSERT_EQ(aliased_binary_output[i].size(), binary_input[i].size());
        for (size_t j = 0; j < binary_input[i].size(); ++j) {
          EXPECT_EQ(aliased_binary_output[i][j], static_cast<uint8_t>(binary_input[i][j] ^ kMask));
        }
      }

      std::vector<std::vector<uint16_t>> u16_vector_input{
        {1, 2, 3},
        {100, 200},
      };
      constexpr uint16_t kDelta = 7;
      auto [u16_vector_writer, u16_vector_reader] = obj->EchoU16VectorStream(kDelta);
      for (const auto& value : u16_vector_input) {
        u16_vector_writer.write(value);
      }
      u16_vector_writer.close();

      std::vector<std::vector<uint16_t>> u16_vector_output;
      for (auto& value : u16_vector_reader) {
        u16_vector_output.push_back(value);
      }

      ASSERT_EQ(u16_vector_output.size(), u16_vector_input.size());
      for (size_t i = 0; i < u16_vector_input.size(); ++i) {
        ASSERT_EQ(u16_vector_output[i].size(), u16_vector_input[i].size());
        for (size_t j = 0; j < u16_vector_input[i].size(); ++j) {
          EXPECT_EQ(u16_vector_output[i][j], static_cast<uint16_t>(u16_vector_input[i][j] + kDelta));
        }
      }

      std::vector<std::vector<nprpc::test::AAA>> object_vector_input{
        {
          nprpc::test::AAA{.a = 1, .b = "vec_a", .c = "left"},
          nprpc::test::AAA{.a = 2, .b = "vec_b", .c = "right"},
        },
        {
          nprpc::test::AAA{.a = 3, .b = "vec_c", .c = "up"},
          nprpc::test::AAA{.a = 4, .b = "vec_d", .c = "down"},
        },
      };
      auto [object_vector_writer, object_vector_reader] = obj->EchoObjectVectorStream(suffix);
      for (const auto& value : object_vector_input) {
        object_vector_writer.write(value);
      }
      object_vector_writer.close();

      std::vector<std::vector<nprpc::test::AAA>> object_vector_output;
      for (auto& value : object_vector_reader) {
        object_vector_output.push_back(value);
      }

      ASSERT_EQ(object_vector_output.size(), object_vector_input.size());
      for (size_t i = 0; i < object_vector_input.size(); ++i) {
        ASSERT_EQ(object_vector_output[i].size(), object_vector_input[i].size());
        for (size_t j = 0; j < object_vector_input[i].size(); ++j) {
          EXPECT_EQ(object_vector_output[i][j].a, object_vector_input[i][j].a + 100);
          EXPECT_EQ(object_vector_output[i][j].b, object_vector_input[i][j].b + suffix);
          EXPECT_EQ(object_vector_output[i][j].c, object_vector_input[i][j].c + suffix);
        }
      }

      std::vector<std::array<uint16_t, 4>> u16_array_input{
        std::array<uint16_t, 4>{1, 2, 3, 4},
        std::array<uint16_t, 4>{10, 20, 30, 40},
      };
      auto [u16_array_writer, u16_array_reader] = obj->EchoU16ArrayStream(kDelta);
      for (const auto& value : u16_array_input) {
        u16_array_writer.write(value);
      }
      u16_array_writer.close();

      std::vector<std::array<uint16_t, 4>> u16_array_output;
      for (auto& value : u16_array_reader) {
        u16_array_output.push_back(value);
      }

      ASSERT_EQ(u16_array_output.size(), u16_array_input.size());
      for (size_t i = 0; i < u16_array_input.size(); ++i) {
        for (size_t j = 0; j < u16_array_input[i].size(); ++j) {
          EXPECT_EQ(u16_array_output[i][j], static_cast<uint16_t>(u16_array_input[i][j] + kDelta));
        }
      }

      std::vector<std::array<nprpc::test::AAA, 2>> object_array_input{
        std::array<nprpc::test::AAA, 2>{
          nprpc::test::AAA{.a = 11, .b = "arr_a", .c = "west"},
          nprpc::test::AAA{.a = 12, .b = "arr_b", .c = "east"},
        },
        std::array<nprpc::test::AAA, 2>{
          nprpc::test::AAA{.a = 13, .b = "arr_c", .c = "north"},
          nprpc::test::AAA{.a = 14, .b = "arr_d", .c = "south"},
        },
      };
      auto [object_array_writer, object_array_reader] = obj->EchoObjectArrayStream(suffix);
      for (const auto& value : object_array_input) {
        object_array_writer.write(value);
      }
      object_array_writer.close();

      std::vector<std::array<nprpc::test::AAA, 2>> object_array_output;
      for (auto& value : object_array_reader) {
        object_array_output.push_back(value);
      }

      ASSERT_EQ(object_array_output.size(), object_array_input.size());
      for (size_t i = 0; i < object_array_input.size(); ++i) {
        for (size_t j = 0; j < object_array_input[i].size(); ++j) {
          EXPECT_EQ(object_array_output[i][j].a, object_array_input[i][j].a + 100);
          EXPECT_EQ(object_array_output[i][j].b, object_array_input[i][j].b + suffix);
          EXPECT_EQ(object_array_output[i][j].c, object_array_input[i][j].c + suffix);
        }
      }

      auto [alias_writer, alias_reader] = obj->EchoAliasOptionalStream(suffix, 7);
      nprpc::test::AliasOptionalStreamPayload alias_payload_1{
        .id = 10,
        .ids = {1, 2, 3},
        .payload = {0, 1, 2},
        .label = std::optional<std::string>{"label"},
        .item = std::optional<nprpc::test::AAA>{nprpc::test::AAA{.a = 5, .b = "item", .c = "payload"}},
        .maybe_id = std::optional<uint32_t>{20},
        .maybe_ids = std::optional<std::vector<uint32_t>>{std::vector<uint32_t>{4, 5}},
        .maybe_payload = std::optional<std::vector<uint8_t>>{std::vector<uint8_t>{6, 7, 8}},
      };
      nprpc::test::AliasOptionalStreamPayload alias_payload_2{
        .id = 30,
        .ids = {9},
        .payload = {9, 8},
      };

      alias_writer.write(alias_payload_1);
      alias_writer.write(alias_payload_2);
      alias_writer.close();

      std::vector<nprpc::test::AliasOptionalStreamPayload> alias_output;
      for (auto& value : alias_reader) {
        alias_output.push_back(std::move(value));
      }

      ASSERT_EQ(alias_output.size(), 2u);
      EXPECT_EQ(alias_output[0].id, 17u);
      EXPECT_EQ(alias_output[0].ids, (std::vector<uint32_t>{8, 9, 10}));
      EXPECT_EQ(alias_output[0].payload, (std::vector<uint8_t>{7, 6, 5}));
      ASSERT_TRUE(alias_output[0].label.has_value());
      EXPECT_EQ(alias_output[0].label.value(), "label-ok");
      ASSERT_TRUE(alias_output[0].item.has_value());
      EXPECT_EQ(alias_output[0].item->a, 105u);
      EXPECT_EQ(alias_output[0].item->b, "item-ok");
      EXPECT_EQ(alias_output[0].item->c, "payload-ok");
      ASSERT_TRUE(alias_output[0].maybe_id.has_value());
      EXPECT_EQ(alias_output[0].maybe_id.value(), 27u);
      ASSERT_TRUE(alias_output[0].maybe_ids.has_value());
      EXPECT_EQ(alias_output[0].maybe_ids.value(), (std::vector<uint32_t>{11, 12}));
      ASSERT_TRUE(alias_output[0].maybe_payload.has_value());
      EXPECT_EQ(alias_output[0].maybe_payload.value(), (std::vector<uint8_t>{1, 0, 15}));

      EXPECT_EQ(alias_output[1].id, 37u);
      EXPECT_EQ(alias_output[1].ids, (std::vector<uint32_t>{16}));
      EXPECT_EQ(alias_output[1].payload, (std::vector<uint8_t>{14, 15}));
      EXPECT_FALSE(alias_output[1].label.has_value());
      EXPECT_FALSE(alias_output[1].item.has_value());
      EXPECT_FALSE(alias_output[1].maybe_id.has_value());
      EXPECT_FALSE(alias_output[1].maybe_ids.has_value());
      EXPECT_FALSE(alias_output[1].maybe_payload.has_value());
    } catch (nprpc::Exception& ex) {
      FAIL() << "Exception in TestBidiStream: " << ex.what();
    }
  };

  exec_test(nprpc::ObjectActivationFlags::ws);
  exec_test(nprpc::ObjectActivationFlags::quic);
}

} // namespace nprpctest

// ============================================================================
// High-Concurrency Load Test
//
// Spawns N concurrent threads, each driving its own resolved proxy against a
// shared servant. Measures end-to-end call latency and reports min/avg/p50/
// p99/max as well as total throughput (calls/sec). Failures are counted and
// reported — any failure causes the test to fail after all threads finish so
// the full metrics picture is visible even in the presence of errors.
//
// Two workloads per transport:
//   Workload A – small round-trip:  ReturnU32()         (no payload)
//   Workload B – mixed payload:     In_(payload var.)   (1–64 kB random size)
// ============================================================================

namespace {

struct LatencyStats {
  std::vector<double> samples_us; // raw per-call latency in microseconds

  void record(std::chrono::nanoseconds elapsed) {
    samples_us.push_back(
        static_cast<double>(elapsed.count()) / 1000.0);
  }

  void merge(LatencyStats&& other) {
    samples_us.insert(samples_us.end(),
                      std::make_move_iterator(other.samples_us.begin()),
                      std::make_move_iterator(other.samples_us.end()));
  }

  void print(const std::string& label) const {
    if (samples_us.empty()) {
      std::cout << label << ": no samples\n";
      return;
    }
    auto sorted = samples_us;
    std::sort(sorted.begin(), sorted.end());
    double sum = std::accumulate(sorted.begin(), sorted.end(), 0.0);
    double avg = sum / static_cast<double>(sorted.size());
    double p50 = sorted[sorted.size() * 50 / 100];
    double p99 = sorted[sorted.size() * 99 / 100];
    std::cout << label
              << "  n=" << sorted.size()
              << "  min=" << sorted.front() << " µs"
              << "  avg=" << avg << " µs"
              << "  p50=" << p50 << " µs"
              << "  p99=" << p99 << " µs"
              << "  max=" << sorted.back() << " µs\n";
  }
};

} // anonymous namespace

namespace nprpctest {

// Parameters – keep defaults conservative enough for CI; tune via environment
// variables or rebuild constants for local load testing.
static constexpr int    kLoadConcurrency  = 64;   // parallel worker threads
static constexpr int    kCallsPerWorker   = 128;  // calls each worker makes
static constexpr size_t kSmallPayloadSize = 64;   // bytes for workload A
static constexpr size_t kMaxPayloadSize   = 64 * 1024; // max for workload B

TEST_F(NprpcTest, HighConcurrencyLoad)
{
  Test::RecordProperty("timeout", "300");

  // Suppress trace-level logging for the duration of this test.
  // The single ASIO-io_context thread writes to std::clog under a mutex on
  // every call; with 64 concurrent workers the pipe fills and blocks the ASIO
  // thread, deadlocking all waiting workers.
  auto* saved_rdbuf = std::clog.rdbuf(nullptr);
  auto restore_clog = [&]() noexcept { std::clog.rdbuf(saved_rdbuf); };

#include "common/tests/basic.inl"
  TestBasicImpl servant;

  auto run_transport = [&](nprpc::ObjectActivationFlags flags,
                           const std::string& transport_name)
  {
    // -----------------------------------------------------------------------
    // Workload A: small round-trip – ReturnU32()
    // -----------------------------------------------------------------------
    {
      auto obj = bind_and_resolve<nprpc::test::TestBasic>(
          servant, flags, "load_test_small");
      obj->set_timeout(10000);

      std::atomic<int> failures{0};
      std::vector<LatencyStats> per_thread_stats(kLoadConcurrency);
      std::vector<std::thread> workers;
      workers.reserve(kLoadConcurrency);

      auto wall_start = std::chrono::steady_clock::now();

      for (int t = 0; t < kLoadConcurrency; ++t) {
        workers.emplace_back([&, t]() {
          auto& stats = per_thread_stats[t];
          stats.samples_us.reserve(kCallsPerWorker);
          // Each thread resolves its own proxy to stress connection handling.
          nprpc::Object* raw_obj = nullptr;
          auto nameserver = rpc->get_nameserver("127.0.0.1");
          if (!nameserver->Resolve("load_test_small", raw_obj) || !raw_obj) {
            ++failures;
            return;
          }
          nprpc::ObjectPtr<nprpc::test::TestBasic> local_obj(
              nprpc::narrow<nprpc::test::TestBasic>(raw_obj));
          if (!local_obj) { ++failures; return; }
          local_obj->set_timeout(10000);

          for (int i = 0; i < kCallsPerWorker; ++i) {
            auto t0 = std::chrono::steady_clock::now();
            try {
              uint32_t v = local_obj->ReturnU32();
              if (v != 42u) ++failures;
            } catch (...) {
              ++failures;
            }
            stats.record(std::chrono::steady_clock::now() - t0);
          }
        });
      }

      for (auto& w : workers) w.join();

      auto wall_elapsed = std::chrono::steady_clock::now() - wall_start;
      double wall_sec = std::chrono::duration<double>(wall_elapsed).count();
      int total_calls = kLoadConcurrency * kCallsPerWorker;
      double throughput = total_calls / wall_sec;

      LatencyStats merged;
      merged.samples_us.reserve(total_calls);
      for (auto& s : per_thread_stats) merged.merge(std::move(s));

      std::cout << "\n[Load/" << transport_name << "/SmallRPC] "
                << "threads=" << kLoadConcurrency
                << " calls_per_thread=" << kCallsPerWorker
                << " total=" << total_calls
                << " failures=" << failures.load()
                << " throughput=" << throughput << " calls/s\n";
      merged.print("[Load/" + transport_name + "/SmallRPC/latency]");

      EXPECT_EQ(failures.load(), 0)
          << transport_name << " small-RPC workload had failures";
    }

    // -----------------------------------------------------------------------
    // Workload B: mixed payload sizes – In_()
    // -----------------------------------------------------------------------
    {
      auto obj = bind_and_resolve<nprpc::test::TestBasic>(
          servant, flags, "load_test_mixed");
      obj->set_timeout(10000);

      std::atomic<int> failures{0};
      std::vector<LatencyStats> per_thread_stats(kLoadConcurrency);
      std::vector<std::thread> workers;
      workers.reserve(kLoadConcurrency);

      auto wall_start = std::chrono::steady_clock::now();

      for (int t = 0; t < kLoadConcurrency; ++t) {
        workers.emplace_back([&, t]() {
          auto& stats = per_thread_stats[t];
          stats.samples_us.reserve(kCallsPerWorker);
          nprpc::Object* raw_obj = nullptr;
          auto nameserver = rpc->get_nameserver("127.0.0.1");
          if (!nameserver->Resolve("load_test_mixed", raw_obj) || !raw_obj) {
            ++failures;
            return;
          }
          nprpc::ObjectPtr<nprpc::test::TestBasic> local_obj(
              nprpc::narrow<nprpc::test::TestBasic>(raw_obj));
          if (!local_obj) { ++failures; return; }
          local_obj->set_timeout(10000);

          // Deterministic pseudo-random size ladder per thread
          size_t payload_size = kSmallPayloadSize + static_cast<size_t>(t) * 1024;
          if (payload_size > kMaxPayloadSize) payload_size = kSmallPayloadSize;

          std::vector<uint8_t> payload(payload_size);
          std::iota(payload.begin(), payload.end(), 0);

          for (int i = 0; i < kCallsPerWorker; ++i) {
            // Vary size each call: small → medium → large → small …
            size_t sz = kSmallPayloadSize
                << (static_cast<unsigned>(i) % 10); // 64 B … 32 kB
            if (sz > kMaxPayloadSize) sz = kSmallPayloadSize;
            if (sz > payload.size()) sz = payload.size();

            auto t0 = std::chrono::steady_clock::now();
            try {
              // In_ expects a=100, b=true, c=iota vector
              std::vector<uint8_t> buf(sz);
              std::iota(buf.begin(), buf.end(), 0);
              bool ok = local_obj->In_(100, true,
                            nprpc::flat::make_read_only_span(buf));
              if (!ok) ++failures;
            } catch (...) {
              ++failures;
            }
            stats.record(std::chrono::steady_clock::now() - t0);
          }
        });
      }

      for (auto& w : workers) w.join();

      auto wall_elapsed = std::chrono::steady_clock::now() - wall_start;
      double wall_sec = std::chrono::duration<double>(wall_elapsed).count();
      int total_calls = kLoadConcurrency * kCallsPerWorker;
      double throughput = total_calls / wall_sec;

      LatencyStats merged;
      merged.samples_us.reserve(total_calls);
      for (auto& s : per_thread_stats) merged.merge(std::move(s));

      std::cout << "\n[Load/" << transport_name << "/MixedPayload] "
                << "threads=" << kLoadConcurrency
                << " calls_per_thread=" << kCallsPerWorker
                << " total=" << total_calls
                << " failures=" << failures.load()
                << " throughput=" << throughput << " calls/s\n";
      merged.print("[Load/" + transport_name + "/MixedPayload/latency]");

      EXPECT_EQ(failures.load(), 0)
          << transport_name << " mixed-payload workload had failures";
    }
  };

  run_transport(nprpc::ObjectActivationFlags::tcp,       "TCP");
  run_transport(nprpc::ObjectActivationFlags::ws, "WS");

  restore_clog(); // restore before final metric output
}

} // namespace nprpctest

int main(int argc, char** argv)
{
  ::testing::InitGoogleTest(&argc, argv);

  // Register the test environment
  ::testing::AddGlobalTestEnvironment(new nprpctest::NprpcTestEnvironment);

  return RUN_ALL_TESTS();
}

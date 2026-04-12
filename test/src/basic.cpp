#include <chrono>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <numeric>
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
    for (int i = 0; i < 10; i++) {
      obj->FireAndForget(i, i % 2 == 0, nprpc::flat::make_read_only_span(data));
    }

    // Wait for messages to be processed
    EXPECT_TRUE(servant.wait_for_count(10));
    EXPECT_EQ(servant.fire_and_forget_count.load(), 10);
    EXPECT_EQ(servant.last_a.load(), 9u); // Last call had a=9
    EXPECT_FALSE(servant.last_b.load());  // 9 % 2 != 0
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

} // namespace nprpctest


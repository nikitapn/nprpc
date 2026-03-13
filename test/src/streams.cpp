#include <cstdlib>
#include <future>
#include <sys/wait.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include <boost/asio/io_context.hpp>
#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/stream_reader.hpp>
#include <nprpc/stream_writer.hpp>
#include <nprpc_nameserver.hpp>
#include <nprpc_test.hpp>

#include <boost/range/algorithm_ext/push_back.hpp>
#include <boost/range/irange.hpp>

#include "common/helper.inl"

namespace nprpctest {
TEST_F(NprpcTest, TestEmptyStreamCompletion)
{
  boost::asio::io_context io_context;
  nprpc::SessionContext ctx;
  nprpc::impl::StreamManager stream_manager(ctx, io_context.get_executor());
  ctx.stream_manager = &stream_manager;

  constexpr uint64_t stream_id = 42;
  nprpc::StreamReader<uint8_t> reader(ctx, stream_id);

  auto future = std::async(std::launch::async, [&reader]() {
    return reader.read_next();
  });

  stream_manager.on_stream_complete(stream_id, nprpc::kEmptyStreamFinalSequence);

  const auto status = future.wait_for(std::chrono::seconds(1));
  if (status != std::future_status::ready) {
    nprpc::flat_buffer empty_fb;
    stream_manager.on_stream_error(stream_id, 1, std::move(empty_fb));
    FAIL() << "Empty stream completion did not unblock the reader";
  }

  auto result = future.get();
  EXPECT_FALSE(result.has_value());
}

TEST_F(NprpcTest, TestUnreliableStreamCompletionDoesNotWaitForMissingChunk)
{
  boost::asio::io_context io_context;
  nprpc::SessionContext ctx;
  nprpc::impl::StreamManager stream_manager(ctx, io_context.get_executor());
  ctx.stream_manager = &stream_manager;

  constexpr uint64_t stream_id = 43;
  nprpc::StreamReader<uint8_t> reader(ctx, stream_id);
  stream_manager.set_reader_unreliable(stream_id, true);

  auto future = std::async(std::launch::async, [&reader]() {
    return reader.read_next();
  });

  stream_manager.on_stream_complete(stream_id, 4);

  const auto status = future.wait_for(std::chrono::seconds(1));
  if (status != std::future_status::ready) {
    nprpc::flat_buffer empty_fb;
    stream_manager.on_stream_error(stream_id, 1, std::move(empty_fb));
    FAIL() << "Unreliable stream completion waited for a dropped final chunk";
  }

  auto result = future.get();
  EXPECT_FALSE(result.has_value());
}

// Streaming test
TEST_F(NprpcTest, TestStreams)
{
#include "common/tests/streams.inl"
  TestStreamsImpl servant;
  auto exec_test = [this, &servant](nprpc::ObjectActivationFlags flags) {
    try {
      auto obj = bind_and_resolve<nprpc::test::TestStreams>(servant, flags, "streams_test");

      constexpr uint64_t kStreamSize = 500;
      auto reader = obj->GetByteStream(kStreamSize);

      std::vector<uint8_t> received;
      received.reserve(kStreamSize);

      // Read all chunks from the stream
      for (auto& chunk : reader) {
        received.push_back(chunk);
      }

      // std::cout << "Received stream of " << received.size() << " bytes." << std::endl;
      // for (size_t i = 0; i < received.size(); ++i) {
      //   std::cout << "Byte " << i << ": " << static_cast<int>(received[i]) << std::endl;
      // }

      // Verify we received all expected bytes
      EXPECT_EQ(received.size(), kStreamSize);
      for (uint64_t i = 0; i < received.size(); ++i) {
        EXPECT_EQ(received[i], static_cast<uint8_t>(i & 0xFF));
      }
    } catch (nprpc::Exception& ex) {
      FAIL() << "Exception in TestStreams: " << ex.what();
    }
  };

  exec_test(nprpc::ObjectActivationFlags::ws);
  // exec_test(nprpc::ObjectActivationFlags::quic);
  // GetByteStream is marked [unreliable], so QUIC can reorder/drop datagrams.
  // Exact delivery assertions are covered only on reliable transports.
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

      try {
        obj->UploadObjectStreamCanThrow(expected_objects.size());
        FAIL() << "Expected UploadObjectStreamCanThrow to throw AssertionFailed";
      } catch (const nprpc::test::AssertionFailed& ex) {
        EXPECT_EQ(std::string_view(ex.message), "UploadObjectStreamCanThrow rejected"sv);
      }

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
  // exec_test(nprpc::ObjectActivationFlags::quic);
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
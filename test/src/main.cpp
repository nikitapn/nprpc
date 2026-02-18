#include <chrono>
#include <coroutine>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/stream_reader.hpp>
#include <nprpc/stream_writer.hpp>
#include <nprpc_nameserver.hpp>
#include <nprpc_test.hpp>
#include <test_udp.hpp>

#include <nprpc/impl/misc/thread_pool.hpp>

#include <boost/range/algorithm_ext/push_back.hpp>
#include <boost/range/irange.hpp>

#include "common/helper.inl"

namespace nprpctest {
// Basic functionality test
TEST_F(NprpcTest, TestBasic)
{
#include "common/tests/basic.inl"
  TestBasicImpl servant;
  auto exec_test = [this, &servant](nprpc::ObjectActivationFlags::Enum flags) {
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
    } catch (nprpc::Exception& ex) {
      FAIL() << "Exception in TestBasic: " << ex.what();
    }
  };

  // exec_test(nprpc::ObjectActivationFlags::Enum::ALLOW_TCP);
  // exec_test(nprpc::ObjectActivationFlags::Enum::ALLOW_WEBSOCKET);
  // exec_test(nprpc::ObjectActivationFlags::Enum::ALLOW_SSL_WEBSOCKET);
  exec_test(nprpc::ObjectActivationFlags::Enum::ALLOW_SHARED_MEMORY);
  // exec_test(nprpc::ObjectActivationFlags::Enum::ALLOW_QUIC);
}

// Basic exception handling
TEST_F(NprpcTest, TestException)
{
#include "common/tests/basic.inl"
  TestBasicImpl servant;
  auto exec_test = [this, &servant](nprpc::ObjectActivationFlags::Enum flags) {
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

  exec_test(nprpc::ObjectActivationFlags::Enum::ALLOW_TCP);
  exec_test(nprpc::ObjectActivationFlags::Enum::ALLOW_WEBSOCKET);
  exec_test(nprpc::ObjectActivationFlags::Enum::ALLOW_SSL_WEBSOCKET);
  // exec_test(nprpc::ObjectActivationFlags::Enum::ALLOW_SHARED_MEMORY);
  exec_test(nprpc::ObjectActivationFlags::Enum::ALLOW_QUIC);
}

// Optional types test
TEST_F(NprpcTest, TestOptional)
{
#include "common/tests/optional.inl"
  TestOptionalImpl servant;
  auto exec_test = [this, &servant](nprpc::ObjectActivationFlags::Enum flags) {
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
  exec_test(nprpc::ObjectActivationFlags::Enum::ALLOW_TCP);
  exec_test(nprpc::ObjectActivationFlags::Enum::ALLOW_WEBSOCKET);
  exec_test(nprpc::ObjectActivationFlags::Enum::ALLOW_SSL_WEBSOCKET);
  // exec_test(nprpc::ObjectActivationFlags::Enum::ALLOW_SHARED_MEMORY);
  exec_test(nprpc::ObjectActivationFlags::Enum::ALLOW_QUIC);
}

// Nested structures test
TEST_F(NprpcTest, TestNested)
{
  // set test timeout to 60 seconds
  Test::RecordProperty("timeout", "60");
#include "common/tests/nested.inl"
  TestNestedImpl servant;
  auto exec_test = [this, &servant](nprpc::ObjectActivationFlags::Enum flags) {
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
  exec_test(nprpc::ObjectActivationFlags::Enum::ALLOW_TCP);
  exec_test(nprpc::ObjectActivationFlags::Enum::ALLOW_WEBSOCKET);
  exec_test(nprpc::ObjectActivationFlags::Enum::ALLOW_SSL_WEBSOCKET);
  // exec_test(nprpc::ObjectActivationFlags::Enum::ALLOW_SHARED_MEMORY);
  exec_test(nprpc::ObjectActivationFlags::Enum::ALLOW_QUIC);
}

// Large message test to verify async_write fix for messages >2.6MB
TEST_F(NprpcTest, TestLargeMessage)
{
  // Set test timeout to 120 seconds for large data transfer
  Test::RecordProperty("timeout", "120");
#include "common/tests/large_message.inl"
  TestLargeMessage servant;
  auto exec_test = [this, &servant](nprpc::ObjectActivationFlags::Enum flags) {
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
  exec_test(nprpc::ObjectActivationFlags::Enum::ALLOW_TCP);
  exec_test(nprpc::ObjectActivationFlags::Enum::ALLOW_WEBSOCKET);
  exec_test(nprpc::ObjectActivationFlags::Enum::ALLOW_SSL_WEBSOCKET);
  // exec_test(nprpc::ObjectActivationFlags::Enum::ALLOW_SHARED_MEMORY);
  exec_test(nprpc::ObjectActivationFlags::Enum::ALLOW_QUIC);
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
      manual_id, &servant_one, nprpc::ObjectActivationFlags::ALLOW_TCP);

  EXPECT_EQ(oid.object_id(), manual_id);

  // Duplicate ID should fail
  EXPECT_THROW(
      custom_poa->activate_object_with_id(
          manual_id, &servant_two, nprpc::ObjectActivationFlags::ALLOW_TCP),
      nprpc::Exception);

  // activate_object should fail on UserSupplied policy
  EXPECT_THROW(custom_poa->activate_object(
                   &servant_two, nprpc::ObjectActivationFlags::ALLOW_TCP),
               nprpc::Exception);

  // ID out of range should fail
  EXPECT_THROW(custom_poa->activate_object_with_id(
                   100, // exceeds max_objects (4)
                   &servant_three, nprpc::ObjectActivationFlags::ALLOW_TCP),
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
    virtual void In_(::nprpc::flat::Span<uint8_t> a) {}
  } servant;

  auto exec_test = [this, &servant](nprpc::ObjectActivationFlags::Enum flags) {
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
  exec_test(nprpc::ObjectActivationFlags::Enum::ALLOW_TCP);
  exec_test(nprpc::ObjectActivationFlags::Enum::ALLOW_WEBSOCKET);
  exec_test(nprpc::ObjectActivationFlags::Enum::ALLOW_SSL_WEBSOCKET);
  // exec_test(nprpc::ObjectActivationFlags::Enum::ALLOW_SHARED_MEMORY);
  exec_test(nprpc::ObjectActivationFlags::Enum::ALLOW_QUIC);
}

// TEST_F(NprpcTest, TestUdpFireAndForget) {
//     // Test multiple fire-and-forget UDP calls

//     class GameSyncImpl : public test_udp::IGameSync_Servant {
//     public:
//         std::atomic<int> update_count{0};
//         std::atomic<int> fire_count{0};
//         std::atomic<int> sound_count{0};
//         std::mutex mtx_;
//         std::condition_variable cv_;

//         void UpdatePosition(uint32_t player_id, test_udp::flat::Vec3_Direct
//         pos, test_udp::flat::Quaternion_Direct rot) override {
//             update_count++;
//             cv_.notify_all();
//         }

//         void FireWeapon(uint32_t player_id, uint8_t weapon_id,
//         test_udp::flat::Vec3_Direct direction) override {
//             fire_count++;
//             cv_.notify_all();
//         }

//         void PlaySound(uint16_t sound_id, test_udp::flat::Vec3_Direct
//         position, float volume) override {
//             sound_count++;
//             cv_.notify_all();
//         }

//         bool ApplyDamage(uint32_t target_id, int32_t amount) override {
//             return true;
//         }

//         uint64_t SpawnEntity(uint16_t entity_type,
//         test_udp::flat::Vec3_Direct position) override {
//             return 0;
//         }

//         void SpawnEntity1(uint16_t entity_type, test_udp::flat::Vec3_Direct
//         position) override {
//             // async reliable - just log
//         }

//         bool wait_for_count(int expected_total, int timeout_ms = 2000) {
//             std::unique_lock<std::mutex> lock(mtx_);
//             return cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
//             [&] {
//                 return (update_count + fire_count + sound_count) >=
//                 expected_total;
//             });
//         }
//     } game_sync_servant;

//     try {
//         auto proxy_game = bind_and_resolve<test_udp::GameSync>(
//             game_sync_servant, nprpc::ObjectActivationFlags::ALLOW_UDP,
//             "udp_game_sync_ff");

//         // Send multiple fire-and-forget calls rapidly
//         for (int i = 0; i < 10; i++) {
//             proxy_game->UpdatePosition(i, {1.0f * i, 2.0f * i, 3.0f * i},
//             {0.0f, 0.0f, 0.0f, 1.0f});
//         }
//         for (int i = 0; i < 5; i++) {
//             proxy_game->FireWeapon(i, i % 3, {1.0f, 0.0f, 0.0f});
//         }
//         for (int i = 0; i < 5; i++) {
//             proxy_game->PlaySound(i * 100, {0.0f, 0.0f, 0.0f}, 0.8f);
//         }

//         // Wait for all messages to be processed
//         EXPECT_TRUE(game_sync_servant.wait_for_count(20));

//         EXPECT_EQ(game_sync_servant.update_count.load(), 10);
//         EXPECT_EQ(game_sync_servant.fire_count.load(), 5);
//         EXPECT_EQ(game_sync_servant.sound_count.load(), 5);

//     } catch (nprpc::Exception& ex) {
//         FAIL() << "Exception in TestUdpFireAndForget: " << ex.what();
//     }
// }

// TEST_F(NprpcTest, TestUdpAck) {
//     class ServerControlImpl : public nprpc::test::IServerControl_Servant {
//     public:
//         nprpc::ObjectPtr<nprpc::test::Ack> ack;

//         void Shutdown() override {}

//         void RegisterAckHandler(::nprpc::Object* handler) override {
//             auto obj = nprpc::narrow<nprpc::test::Ack>(handler);
//             if (!obj) {
//                 FAIL() << "RegisterAckHandler: Invalid object type";
//             }
//             ack.reset(obj);
//         }
//     } server_control_servant; // Remote

//     class GameSyncImpl : public test_udp::IGameSync_Servant {
//         ServerControlImpl& ctl;
//     public:
//         GameSyncImpl(ServerControlImpl& server_control_servant)
//             : ctl(server_control_servant) {}

//         void UpdatePosition (uint32_t player_id, test_udp::flat::Vec3_Direct
//         pos, test_udp::flat::Quaternion_Direct rot) override {
//             // Implementation of UpdatePosition
//             ctl.ack->Confirm({}, "UpdatePosition ACK");
//         }

//         void FireWeapon (uint32_t player_id, uint8_t weapon_id,
//         test_udp::flat::Vec3_Direct direction) override {
//             // Implementation of FireWeapon
//         }

//         void PlaySound (uint16_t sound_id, test_udp::flat::Vec3_Direct
//         position, float volume) override {
//             // Implementation of PlaySound
//         }

//         bool ApplyDamage (uint32_t target_id, int32_t amount) override {
//             // Implementation of ApplyDamage
//             return true; // Example return value
//         }

//         uint64_t SpawnEntity (uint16_t entity_type,
//         test_udp::flat::Vec3_Direct position) override {
//             // Implementation of SpawnEntity
//             return 0; // Example return value
//         }

//         void SpawnEntity1 (uint16_t entity_type, test_udp::flat::Vec3_Direct
//         position) override {
//             // Async reliable - no return value
//         }
//     } game_sync_servant(server_control_servant); // Remote

//     class TestUdpAckImpl : public nprpc::test::IAck_Servant {
//         std::mutex mtx_;
//         std::condition_variable cv_;
//         std::string last_msg_;
//     public:
//         void Confirm(::nprpc::flat::Span<char> what) override {
//             {
//                 std::lock_guard<std::mutex> lock(mtx_);
//                 last_msg_ = (std::string)what;
//             }
//             cv_.notify_all();
//         }

//         std::string wait_for_ack() {
//             constexpr auto max_timeout_ms = 1000;
//             std::unique_lock<std::mutex> lock(mtx_);
//             if (cv_.wait_for(lock, std::chrono::milliseconds(max_timeout_ms))
//             == std::cv_status::timeout) {
//                 throw nprpc::Exception("Timeout waiting for ACK");
//             }
//             return last_msg_;
//         }
//     } ack_servant; // Local

//     try {
//         auto proxy_game = bind_and_resolve<test_udp::GameSync>(
//             game_sync_servant, nprpc::ObjectActivationFlags::ALLOW_UDP,
//             "udp_game_sync");

//         auto proxy_server_control =
//         bind_and_resolve<nprpc::test::ServerControl>(
//             server_control_servant,
//             nprpc::ObjectActivationFlags::ALLOW_SHARED_MEMORY,
//             "udp_server_control");

//         // Important: Do not use SHARED_MEMORY for the callback object, as
//         the server and client both shares
//         // the same g_shared_memory_listener object, which can lead to
//         conflicts and unexpected behavior. auto oid =
//         poa->activate_object(&ack_servant,
//         nprpc::ObjectActivationFlags::ALLOW_TCP);
//         proxy_server_control->RegisterAckHandler(oid);

//         // Now invoke UpdatePosition which should trigger an ACK
//         proxy_game->UpdatePosition(1, {0.0f, 1.0f, 2.0f}, {0.0f, 0.0f,
//         0.0f, 1.0f}); auto ack_msg = ack_servant.wait_for_ack();
//         EXPECT_EQ(ack_msg, "UpdatePosition ACK");

//         // Clean up: release the server's reference to the client callback
//         // and deactivate the local servant before TearDown
//         server_control_servant.ack.reset();
//         poa->deactivate_object(oid.object_id());
//     } catch (nprpc::Exception& ex) {
//         FAIL() << "Exception in TestUdpAck: " << ex.what();
//     }
// }

// TEST_F(NprpcTest, TestUdpReliable) {
//     // Test reliable UDP calls with return values

//     class GameSyncImpl : public test_udp::IGameSync_Servant {
//     public:
//         std::atomic<int> damage_calls{0};
//         std::atomic<int> spawn_calls{0};
//         std::atomic<uint64_t> next_entity_id{1000};

//         void UpdatePosition(uint32_t player_id, test_udp::flat::Vec3_Direct
//         pos, test_udp::flat::Quaternion_Direct rot) override {} void
//         FireWeapon(uint32_t player_id, uint8_t weapon_id,
//         test_udp::flat::Vec3_Direct direction) override {} void
//         PlaySound(uint16_t sound_id, test_udp::flat::Vec3_Direct position,
//         float volume) override {}

//         bool ApplyDamage(uint32_t target_id, int32_t amount) override {
//             damage_calls++;
//             // Return true if target survives (amount < 100), false if killed
//             return amount < 100;
//         }

//         uint64_t SpawnEntity(uint16_t entity_type,
//         test_udp::flat::Vec3_Direct position) override {
//             spawn_calls++;
//             return next_entity_id++;
//         }

//         void SpawnEntity1(uint16_t entity_type, test_udp::flat::Vec3_Direct
//         position) override {
//             // Async reliable - no return value
//         }
//     } game_sync_servant;

//     try {
//         auto proxy_game = bind_and_resolve<test_udp::GameSync>(
//             game_sync_servant, nprpc::ObjectActivationFlags::ALLOW_UDP,
//             "udp_reliable_test");

//         // Test ApplyDamage with different amounts
//         bool survived1 = proxy_game->ApplyDamage(1, 50);
//         EXPECT_TRUE(survived1) << "Player should survive 50 damage";

//         bool survived2 = proxy_game->ApplyDamage(1, 99);
//         EXPECT_TRUE(survived2) << "Player should survive 99 damage";

//         bool survived3 = proxy_game->ApplyDamage(1, 100);
//         EXPECT_FALSE(survived3) << "Player should be killed by 100 damage";

//         bool survived4 = proxy_game->ApplyDamage(1, 150);
//         EXPECT_FALSE(survived4) << "Player should be killed by 150 damage";

//         EXPECT_EQ(game_sync_servant.damage_calls.load(), 4);

//         // Test SpawnEntity
//         uint64_t entity1 = proxy_game->SpawnEntity(1, {0.0f, 0.0f, 0.0f});
//         EXPECT_EQ(entity1, 1000);

//         uint64_t entity2 = proxy_game->SpawnEntity(2, {10.0f, 0.0f, 0.0f});
//         EXPECT_EQ(entity2, 1001);

//         uint64_t entity3 = proxy_game->SpawnEntity(3, {20.0f, 0.0f, 0.0f});
//         EXPECT_EQ(entity3, 1002);

//         EXPECT_EQ(game_sync_servant.spawn_calls.load(), 3);

//         std::cout << "Reliable UDP test completed successfully!" <<
//         std::endl;

//     } catch (nprpc::Exception& ex) {
//         FAIL() << "Exception in TestUdpReliable: " << ex.what();
//     }
// }

#ifdef NPRPC_HAS_QUIC
// QUIC transport test - basic RPC over QUIC streams
TEST_F(NprpcTest, TestQuicBasic)
{
#include "common/tests/basic.inl"
  TestBasicImpl servant;

  try {
    auto obj = bind_and_resolve<nprpc::test::TestBasic>(
        servant, nprpc::ObjectActivationFlags::ALLOW_QUIC, "quic_basic_test");

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
        servant, nprpc::ObjectActivationFlags::ALLOW_QUIC, "quic_unreliable_test");

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
  auto exec_test = [this, &servant](nprpc::ObjectActivationFlags::Enum flags) {
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

  exec_test(nprpc::ObjectActivationFlags::Enum::ALLOW_WEBSOCKET);
  exec_test(nprpc::ObjectActivationFlags::Enum::ALLOW_QUIC);
  // TODO: TCP stream crashes...
  // exec_test(nprpc::ObjectActivationFlags::Enum::ALLOW_TCP);
}

} // namespace nprpctest

int main(int argc, char** argv)
{
  ::testing::InitGoogleTest(&argc, argv);

  // Register the test environment
  ::testing::AddGlobalTestEnvironment(new nprpctest::NprpcTestEnvironment);

  return RUN_ALL_TESTS();
}

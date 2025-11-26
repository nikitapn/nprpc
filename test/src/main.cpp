#include <iostream>
#include <chrono>
#include <numeric>
#include <thread>
#include <future>
#include <cstdlib>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc_test.hpp>
#include <test_udp.hpp>
#include <nprpc_nameserver.hpp>

#include <nprpc/impl/misc/thread_pool.hpp>

#include <boost/range/algorithm_ext/push_back.hpp> 
#include <boost/range/irange.hpp>

#include "common/helper.inl"

namespace nprpctest {
// Basic functionality test
TEST_F(NprpcTest, TestBasic) {
    #include "common/tests/basic.inl"
    TestBasicImpl servant;
    auto exec_test = [this, &servant](nprpc::ObjectActivationFlags::Enum flags) { 
        try {
            auto obj = make_stuff_happen<nprpc::test::TestBasic>(servant, flags);

            // ReturnBoolean test
            EXPECT_TRUE(obj->ReturnBoolean());

            // In/Out test
            std::vector<uint8_t> ints;
            ints.reserve(256);
            boost::push_back(ints, boost::irange(0, 255));

            EXPECT_TRUE(obj->In(100, true, nprpc::flat::make_read_only_span(ints)));

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

            // InException test
            try {
              obj->InException();
              FAIL() << "Expected InException to throw SimpleException";
            } catch (const nprpc::test::SimpleException& ex) {
              EXPECT_EQ(std::string_view(ex.message), "This is a test exception"sv);
              EXPECT_EQ(ex.code, 123);
            }

            // OutScalarWithException test - tests flat output struct with exception handler
            // This verifies the fix where output parameters must be declared before try block
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
    exec_test(nprpc::ObjectActivationFlags::Enum::ALLOW_SHARED_MEMORY);
}

// Optional types test
TEST_F(NprpcTest, TestOptional) {
    #include "common/tests/optional.inl"
    TestOptionalImpl servant;
    auto exec_test = [this, &servant](nprpc::ObjectActivationFlags::Enum flags) { 
        try {
            auto obj = make_stuff_happen<nprpc::test::TestOptional>(servant, flags);

            EXPECT_TRUE(obj->InEmpty(std::nullopt));
            EXPECT_TRUE(obj->In(100, nprpc::test::AAA{ 100u, "test_b"s, "test_c"s }));

            std::optional<uint32_t> a;

            obj->OutEmpty(a);
            EXPECT_FALSE(a.has_value());

            obj->Out(a);
            EXPECT_TRUE(a.has_value());
            EXPECT_EQ(a.value(), 100u);

            auto opt = obj->ReturnOpt1();
            EXPECT_EQ(opt.str, "test_string");
            EXPECT_TRUE(opt.stream.has_value());
            EXPECT_EQ(opt.stream->size(), 10u);
            for (uint8_t i = 0; i < 10; ++i) {
                EXPECT_EQ(opt.stream->at(i), i);
            }

        } catch (nprpc::Exception& ex) {
            FAIL() << "Exception in TestOptional: " << ex.what();
        }
    };
    exec_test(nprpc::ObjectActivationFlags::Enum::ALLOW_TCP);
    exec_test(nprpc::ObjectActivationFlags::Enum::ALLOW_WEBSOCKET);
    exec_test(nprpc::ObjectActivationFlags::Enum::ALLOW_SSL_WEBSOCKET);
    exec_test(nprpc::ObjectActivationFlags::Enum::ALLOW_SHARED_MEMORY);
}

// Nested structures test
TEST_F(NprpcTest, TestNested) {
    // set test timeout to 60 seconds
    Test::RecordProperty("timeout", "60");
    #include "common/tests/nested.inl"
    TestNestedImpl servant;
    auto exec_test = [this, &servant](nprpc::ObjectActivationFlags::Enum flags) { 
        try {
            auto obj = make_stuff_happen<nprpc::test::TestNested>(servant, flags);
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
    exec_test(nprpc::ObjectActivationFlags::Enum::ALLOW_SHARED_MEMORY);
}

// Large message test to verify async_write fix for messages >2.6MB
TEST_F(NprpcTest, TestLargeMessage) {
    // Set test timeout to 120 seconds for large data transfer
    Test::RecordProperty("timeout", "120");
    #include "common/tests/large_message.inl"
    TestLargeMessage servant;
    auto exec_test = [this, &servant](nprpc::ObjectActivationFlags::Enum flags) { 
        try {   
            auto obj = make_stuff_happen<nprpc::test::TestLargeMessage>(servant, flags);
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
            EXPECT_TRUE(obj->In(42, true, nprpc::flat::make_read_only_span(large_data)));

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
    exec_test(nprpc::ObjectActivationFlags::Enum::ALLOW_SHARED_MEMORY);
}

// Bad input validation test
TEST_F(NprpcTest, TestBadInput) {
    class TestBadInputImpl : public nprpc::test::ITestBadInput_Servant {
    public:
        virtual void In(::nprpc::flat::Span<uint8_t> a) {}
    } servant;

    auto exec_test = [this, &servant](nprpc::ObjectActivationFlags::Enum flags) { 
        try {
            auto obj = make_stuff_happen<nprpc::test::TestBadInput>(servant, flags);

            nprpc::flat_buffer buf;
            auto mb = buf.prepare(2048);
            buf.commit(40);
            static_cast<::nprpc::impl::Header*>(mb.data())->msg_id = ::nprpc::impl::MessageId::FunctionCall;
            static_cast<::nprpc::impl::Header*>(mb.data())->msg_type = ::nprpc::impl::MessageType::Request;
            ::nprpc::impl::flat::CallHeader_Direct __ch(buf, sizeof(::nprpc::impl::Header));
            __ch.object_id() = obj->object_id();
            __ch.poa_idx() = obj->poa_idx();
            __ch.interface_idx() = 0; // single interface
            __ch.function_idx() = 0; // single function

            buf.commit(1024);
            // Set correct size in header
            static_cast<::nprpc::impl::Header*>(buf.data().data())->size = static_cast<uint32_t>(buf.size() - 4);
            auto vec_begin = static_cast<std::byte*>(buf.data().data()) + 32;
            // Set size of the vector to be larger than the buffer size
            *reinterpret_cast<uint32_t*>(vec_begin) = 0xDEADBEEF;
       
            ::nprpc::impl::g_orb->call(obj->get_endpoint(), buf, obj->get_timeout());
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
    exec_test(nprpc::ObjectActivationFlags::Enum::ALLOW_SHARED_MEMORY);
}

TEST_F(NprpcTest, TestUdpFireAndForget) {
    // Test multiple fire-and-forget UDP calls
    
    class GameSyncImpl : public test_udp::IGameSync_Servant {
    public:
        std::atomic<int> update_count{0};
        std::atomic<int> fire_count{0};
        std::atomic<int> sound_count{0};
        std::mutex mtx_;
        std::condition_variable cv_;

        void UpdatePosition(uint32_t player_id, test_udp::flat::Vec3_Direct pos, test_udp::flat::Quaternion_Direct rot) override {
            update_count++;
            cv_.notify_all();
        }

        void FireWeapon(uint32_t player_id, uint8_t weapon_id, test_udp::flat::Vec3_Direct direction) override {
            fire_count++;
            cv_.notify_all();
        }

        void PlaySound(uint16_t sound_id, test_udp::flat::Vec3_Direct position, float volume) override {
            sound_count++;
            cv_.notify_all();
        }

        bool ApplyDamage(uint32_t target_id, int32_t amount) override {
            return true;
        }

        uint64_t SpawnEntity(uint16_t entity_type, test_udp::flat::Vec3_Direct position) override {
            return 0;
        }

        bool wait_for_count(int expected_total, int timeout_ms = 2000) {
            std::unique_lock<std::mutex> lock(mtx_);
            return cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&] {
                return (update_count + fire_count + sound_count) >= expected_total;
            });
        }
    } game_sync_servant;

    try {
        auto proxy_game = make_stuff_happen<test_udp::GameSync>(
            game_sync_servant, nprpc::ObjectActivationFlags::ALLOW_UDP, "udp_game_sync_ff");

        // Send multiple fire-and-forget calls rapidly
        for (int i = 0; i < 10; i++) {
            proxy_game->UpdatePosition(i, {1.0f * i, 2.0f * i, 3.0f * i}, {0.0f, 0.0f, 0.0f, 1.0f});
        }
        for (int i = 0; i < 5; i++) {
            proxy_game->FireWeapon(i, i % 3, {1.0f, 0.0f, 0.0f});
        }
        for (int i = 0; i < 5; i++) {
            proxy_game->PlaySound(i * 100, {0.0f, 0.0f, 0.0f}, 0.8f);
        }

        // Wait for all messages to be processed
        EXPECT_TRUE(game_sync_servant.wait_for_count(20));
        
        EXPECT_EQ(game_sync_servant.update_count.load(), 10);
        EXPECT_EQ(game_sync_servant.fire_count.load(), 5);
        EXPECT_EQ(game_sync_servant.sound_count.load(), 5);

    } catch (nprpc::Exception& ex) {
        FAIL() << "Exception in TestUdpFireAndForget: " << ex.what();
    }
}

TEST_F(NprpcTest, TestUdpAck) {
    class ServerControlImpl : public nprpc::test::IServerControl_Servant {
    public:
        nprpc::ObjectPtr<nprpc::test::Ack> ack;

        void Shutdown() override {
            // noop for this test
        }

        void RegisterAckHandler(::nprpc::Object* handler) override {
            auto obj = nprpc::narrow<nprpc::test::Ack>(handler);
            if (!obj) {
                FAIL() << "RegisterAckHandler: Invalid object type";
            }
            ack.reset(obj);
        }
    } server_control_servant; // Remote

    class GameSyncImpl : public test_udp::IGameSync_Servant {
        ServerControlImpl& ctl;
    public:
        GameSyncImpl(ServerControlImpl& server_control_servant)
            : ctl(server_control_servant) {}

        void UpdatePosition (uint32_t player_id, test_udp::flat::Vec3_Direct pos, test_udp::flat::Quaternion_Direct rot) override {
            // Implementation of UpdatePosition
            ctl.ack->Confirm({}, "UpdatePosition ACK");
        }

        void FireWeapon (uint32_t player_id, uint8_t weapon_id, test_udp::flat::Vec3_Direct direction) override {
            // Implementation of FireWeapon
        }

        void PlaySound (uint16_t sound_id, test_udp::flat::Vec3_Direct position, float volume) override {
            // Implementation of PlaySound
        }

        bool ApplyDamage (uint32_t target_id, int32_t amount) override {
            // Implementation of ApplyDamage
            return true; // Example return value
        }

        uint64_t SpawnEntity (uint16_t entity_type, test_udp::flat::Vec3_Direct position) override {
            // Implementation of SpawnEntity
            return 0; // Example return value
        }
    } game_sync_servant(server_control_servant); // Remote

    class TestUdpAckImpl : public nprpc::test::IAck_Servant {
        std::mutex mtx_;
        std::condition_variable cv_;
        std::string last_msg_;
    public:
        void Confirm(::nprpc::flat::Span<char> what) override {
            {
                std::lock_guard<std::mutex> lock(mtx_);
                last_msg_ = (std::string)what;
            }
            cv_.notify_all();
        }

        std::string wait_for_ack() {
            constexpr auto max_timeout_ms = 5000;
            std::unique_lock<std::mutex> lock(mtx_);
            if (cv_.wait_for(lock, std::chrono::milliseconds(max_timeout_ms)) == std::cv_status::timeout) {
                throw nprpc::Exception("Timeout waiting for ACK");
            }
            return last_msg_;
        }
    } ack_servant; // Local

    try {
        auto proxy_game = make_stuff_happen<test_udp::GameSync>(
            game_sync_servant, nprpc::ObjectActivationFlags::ALLOW_UDP, "udp_game_sync");

        auto proxy_server_control = make_stuff_happen<nprpc::test::ServerControl>(
            server_control_servant, nprpc::ObjectActivationFlags::ALLOW_SHARED_MEMORY, "udp_server_control");

        auto oid = poa->activate_object(&ack_servant, nprpc::ObjectActivationFlags::ALLOW_SHARED_MEMORY);
        proxy_server_control->RegisterAckHandler(oid);

        // Now invoke UpdatePosition which should trigger an ACK
        proxy_game->UpdatePosition(1, {0.0f, 1.0f, 2.0f}, {0.0f, 0.0f, 0.0f, 1.0f});
        auto ack_msg = ack_servant.wait_for_ack();
        EXPECT_EQ(ack_msg, "UpdatePosition ACK");
    } catch (nprpc::Exception& ex) {
        FAIL() << "Exception in TestUdpAck: " << ex.what();
    }
}

} // namespace nprpctest


int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);

    // Register the test environment
    ::testing::AddGlobalTestEnvironment(new nprpctest::NprpcTestEnvironment);

    return RUN_ALL_TESTS();
}

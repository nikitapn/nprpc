#include <iostream>
#include <chrono>
#include <numeric>
#include <thread>
#include <future>
#include <cstdlib>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc_test.hpp>
#include <test_udp.hpp>
#include <nprpc_nameserver.hpp>

#include <nprpc/impl/misc/thread_pool.hpp>

#include <boost/range/algorithm_ext/push_back.hpp> 
#include <boost/range/irange.hpp>

#include "common/helper.inl"

namespace nprpctest {
void run() {
    class ServerControlImpl : public nprpc::test::IServerControl_Servant {
    public:
        nprpc::ObjectPtr<nprpc::test::Ack> ack;

        void Shutdown() override {}

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

        void SpawnEntity1(uint16_t entity_type, test_udp::flat::Vec3_Direct position) override {
            // Implementation of SpawnEntity1 (async version)
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
            constexpr auto max_timeout_ms = 1000;
            std::unique_lock<std::mutex> lock(mtx_);
            if (cv_.wait_for(lock, std::chrono::milliseconds(max_timeout_ms)) == std::cv_status::timeout) {
                throw nprpc::Exception("Timeout waiting for ACK");
            }
            return last_msg_;
        }
        ~TestUdpAckImpl() {
            std::cout << "TestUdpAckImpl destroyed" << std::endl;
        }
    } ack_servant; // Local

    try {
        auto proxy_game = make_stuff_happen<test_udp::GameSync>(
            game_sync_servant, nprpc::ObjectActivationFlags::ALLOW_UDP, "udp_game_sync");

        auto proxy_server_control = make_stuff_happen<nprpc::test::ServerControl>(
            server_control_servant, nprpc::ObjectActivationFlags::ALLOW_SHARED_MEMORY, "udp_server_control");

        // Important: Do not use SHARED_MEMORY for the callback object, as the server and client both shares
        // the same g_shared_memory_listener object, which can lead to conflicts and unexpected behavior.
        auto oid = poa->activate_object(&ack_servant, nprpc::ObjectActivationFlags::ALLOW_TCP);
        proxy_server_control->RegisterAckHandler(oid);

        // Now invoke UpdatePosition which should trigger an ACK
        proxy_game->UpdatePosition(1, {0.0f, 1.0f, 2.0f}, {0.0f, 0.0f, 0.0f, 1.0f});
        auto ack_msg = ack_servant.wait_for_ack();
        EXPECT_EQ(ack_msg, "UpdatePosition ACK");

        // Clean up: release the server's reference to the client callback
        // and deactivate the local servant before TearDown
        server_control_servant.ack.reset();
        poa->deactivate_object(oid.object_id());
    } catch (nprpc::Exception& ex) {
        FAIL() << "Exception in TestUdpAck: " << ex.what();
    }
}
} // namespace nprpctest

int main(int argc, char** argv) {
    nprpctest::NprpcTestEnvironment env;
    env.SetUp();
    nprpctest::run();
    env.TearDown();
    return 0;
}
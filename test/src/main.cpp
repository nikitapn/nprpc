#include "data.hpp"

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
#include <nprpc_test_stub/test.hpp>
#include <nprpc_stub/nprpc_nameserver.hpp>

#include <nplib/utils/thread_pool.hpp>

#include <boost/range/algorithm_ext/push_back.hpp> 
#include <boost/range/irange.hpp>
#include <boost/asio/thread_pool.hpp>

using namespace std::string_literals;

namespace nprpctest {

using thread_pool = nplib::thread_pool_1;

// Helper class to manage nameserver process
class NameserverManager {
private:
    pid_t nameserver_pid = -1;
    
public:
    bool start_nameserver() {
        // Fork a child process to run the nameserver
        nameserver_pid = fork();
        
        if (nameserver_pid == -1) {
            std::cerr << "Failed to fork nameserver process" << std::endl;
            return false;
        } else if (nameserver_pid == 0) {
            // Child process - run the nameserver
            // Try to find npnameserver in the build directory
            execl("./build/linux/bin/npnameserver", "npnameserver", nullptr);
            execl("../build/linux/bin/npnameserver", "npnameserver", nullptr);
            execl("../../build/linux/bin/npnameserver", "npnameserver", nullptr);
            execl("/home/nikita/projects/npsystem/build/linux/bin/npnameserver", "npnameserver", nullptr);
            
            // If all fail, exit with error
            std::cerr << "Failed to execute npnameserver" << std::endl;
            _exit(1);
        } else {
            // Parent process - wait a bit for nameserver to start
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            
            // Check if the child process is still alive
            int status;
            pid_t result = waitpid(nameserver_pid, &status, WNOHANG);
            if (result != 0) {
                std::cerr << "Nameserver process failed to start" << std::endl;
                nameserver_pid = -1;
                return false;
            }
            
            std::cout << "Nameserver started with PID: " << nameserver_pid << std::endl;
            return true;
        }
    }
    
    void stop_nameserver() {
        if (nameserver_pid > 0) {
            std::cout << "Stopping nameserver with PID: " << nameserver_pid << std::endl;
            kill(nameserver_pid, SIGTERM);
            
            // Wait for the process to terminate
            int status;
            waitpid(nameserver_pid, &status, 0);
            nameserver_pid = -1;
        }
    }
    
    ~NameserverManager() {
        stop_nameserver();
    }
};

nprpc::Rpc* rpc;
nprpc::Poa* poa;
NameserverManager nameserver_manager;

// Google Test Environment for setup and teardown
class NprpcTestEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        // Start the nameserver first
        if (!nameserver_manager.start_nameserver()) {
            FAIL() << "Failed to start nameserver process";
        }
        
        try {
            // Use the new RpcBuilder API
            rpc = nprpc::RpcBuilder()
                .set_debug_level(nprpc::DebugLevel::DebugLevel_Critical)
                .set_listen_tcp_port(22222)
                .set_listen_http_port(22223)
                .set_hostname("localhost")
                .enable_ssl_server(
                    "/home/nikita/projects/npsystem/certs/server.crt",
                    "/home/nikita/projects/npsystem/certs/server.key",
                    "/home/nikita/projects/npsystem/certs/dhparam.pem")
                .enable_ssl_client_self_signed_cert("/home/nikita/projects/npsystem/certs/server.crt")
                .build(thread_pool::get_instance().ctx());

            // Use the new PoaBuilder API  
            poa = rpc->create_poa()
                .with_max_objects(128)
                .with_lifespan(nprpc::PoaPolicy::Lifespan::Persistent)
                .build();

        } catch (nprpc::Exception& ex) {
            nameserver_manager.stop_nameserver();
            FAIL() << "Failed to initialize RPC: " << ex.what();
        }
    }

    void TearDown() override {
        thread_pool::get_instance().stop();
        if (rpc) {
            // thread_pool::get_instance().stop();
            rpc->destroy();
        } 
        // Stop the nameserver
        nameserver_manager.stop_nameserver();
    }
};

// Test fixture class for shared functionality
class NprpcTest : public ::testing::Test {
protected:
    template<typename T>
    auto make_stuff_happen(typename T::servant_t& servant, nprpc::ObjectActivationFlags::Enum flags) {
        static const std::string object_name = "nprpc_test_object";

        auto nameserver = rpc->get_nameserver("127.0.0.1");
        auto oid = poa->activate_object(&servant, flags);
        nameserver->Bind(oid, object_name);

        nprpc::Object* raw;
        EXPECT_TRUE(nameserver->Resolve(object_name, raw));

        return nprpc::ObjectPtr(nprpc::narrow<T>(raw));
    }
};

// Basic functionality test
TEST_F(NprpcTest, TestBasic) {
    class TestBasicImpl : public test::ITestBasic_Servant {
    public:
        virtual bool ReturnBoolean() {
            return true;
        }

        virtual bool In(uint32_t a, ::nprpc::flat::Boolean b, ::nprpc::flat::Span<uint8_t> c) {
            EXPECT_EQ(a, 100u);
            EXPECT_TRUE(b.get());

            uint8_t ix = 0;
            for (auto i : c) {
                EXPECT_EQ(ix++, i);
            }

            return true;
        }

        virtual void Out(uint32_t& a, ::nprpc::flat::Boolean& b, ::nprpc::flat::Vector_Direct1<uint8_t> c) {
            a = 100;
            b = true;

            c.length(256);
            auto span = c();
            std::iota(std::begin(span), std::end(span), 0);
        }
    } servant;

    auto exec_test = [this, &servant](nprpc::ObjectActivationFlags::Enum flags) { 
        try {
            auto obj = make_stuff_happen<test::TestBasic>(servant, flags);

            EXPECT_TRUE(obj->ReturnBoolean());

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
        } catch (nprpc::Exception& ex) {
            FAIL() << "Exception in TestBasic: " << ex.what();
        }
    };

    exec_test(nprpc::ObjectActivationFlags::Enum::ALLOW_TCP);
    exec_test(nprpc::ObjectActivationFlags::Enum::ALLOW_WEBSOCKET);
    exec_test(nprpc::ObjectActivationFlags::Enum::ALLOW_SSL_WEBSOCKET);
}

// Optional types test
TEST_F(NprpcTest, TestOptional) {
    class TestOptionalImpl : public test::ITestOptional_Servant {
    public:
        virtual bool InEmpty(::nprpc::flat::Optional_Direct<uint32_t> a) {
            EXPECT_FALSE(a.has_value());
            return true;
        }

        virtual bool In(::nprpc::flat::Optional_Direct<uint32_t> a, 
                       ::nprpc::flat::Optional_Direct<test::flat::AAA, test::flat::AAA_Direct> b) {
            EXPECT_TRUE(a.has_value());
            EXPECT_EQ(a.value(), 100u);
            EXPECT_TRUE(b.has_value());

            auto const& value = b.value();

            EXPECT_EQ(value.a(), 100u);
            EXPECT_EQ(std::string_view(value.b()), "test_b");
            EXPECT_EQ(std::string_view(value.c()), "test_c");

            return true;
        }

        virtual void OutEmpty(::nprpc::flat::Optional_Direct<uint32_t> a) {
            a.set_nullopt();
        }

        virtual void Out(::nprpc::flat::Optional_Direct<uint32_t> a) {
            a.alloc();
            a.value() = 100;
        }
    } servant;

    auto exec_test = [this, &servant](nprpc::ObjectActivationFlags::Enum flags) { 
        try {
            auto obj = make_stuff_happen<test::TestOptional>(servant, flags);

            EXPECT_TRUE(obj->InEmpty(std::nullopt));
            EXPECT_TRUE(obj->In(100, test::AAA{ 100u, "test_b"s, "test_c"s }));

            std::optional<uint32_t> a;

            obj->OutEmpty(a);
            EXPECT_FALSE(a.has_value());

            obj->Out(a);
            EXPECT_TRUE(a.has_value());
            EXPECT_EQ(a.value(), 100u);

        } catch (nprpc::Exception& ex) {
            FAIL() << "Exception in TestOptional: " << ex.what();
        }
    };
    exec_test(nprpc::ObjectActivationFlags::Enum::ALLOW_TCP);
    exec_test(nprpc::ObjectActivationFlags::Enum::ALLOW_WEBSOCKET);
}

// Nested structures test
TEST_F(NprpcTest, TestNested) {
    // set test timeout to 60 seconds
    Test::RecordProperty("timeout", "60");


    class TestNestedImpl : public test::ITestNested_Servant {
    public:
        virtual void Out(::nprpc::flat::Optional_Direct<test::flat::BBB, test::flat::BBB_Direct> a) {
            a.alloc();
            auto value = a.value();

            value.a(1024);

            {
                auto span = value.a();
                std::uint32_t ix = 0;
                for (auto i : span) {
                    i.a() = ix++;
                    i.b(tstr1);
                    i.c(tstr2);
                }
            }

            value.b(2048);

            auto span = value.b();
            bool b = false;
            for (auto i : span) {
                i.a(tstr1);
                i.b(tstr2);
                i.c().alloc();
                i.c().value() = (b ^= 1);
            }
        }
    } servant;

    auto exec_test = [this, &servant](nprpc::ObjectActivationFlags::Enum flags) { 
        try {
            auto obj = make_stuff_happen<test::TestNested>(servant, flags);
            obj->set_timeout(5000); // Set a longer timeout for this test

            std::optional<test::BBB> a;

            obj->Out(a);

            EXPECT_TRUE(a.has_value());
            auto& value = a.value();

            EXPECT_EQ(value.a.size(), 1024ull);

            std::uint32_t ix = 0;
            for (auto& i : value.a) {
                EXPECT_EQ(i.a, ix++);
                EXPECT_EQ(std::string_view(i.b), std::string_view(tstr1));
                EXPECT_EQ(std::string_view(i.c), std::string_view(tstr2));
            }

            EXPECT_EQ(value.b.size(), 2048ull);

            bool b = false;
            for (auto& i : value.b) {
                EXPECT_EQ(std::string_view(i.a), std::string_view(tstr1));
                EXPECT_EQ(std::string_view(i.b), std::string_view(tstr2));
                EXPECT_TRUE(i.c.has_value());
                EXPECT_EQ(i.c.value(), b ^= 1);
            }
        } catch (nprpc::Exception& ex) {
            FAIL() << "Exception in TestNested: " << ex.what();
        }
    };
    exec_test(nprpc::ObjectActivationFlags::Enum::ALLOW_TCP);
    exec_test(nprpc::ObjectActivationFlags::Enum::ALLOW_WEBSOCKET);
}

// Large message test to verify async_write fix for messages >2.6MB
TEST_F(NprpcTest, TestLargeMessage) {
    // Set test timeout to 120 seconds for large data transfer
    Test::RecordProperty("timeout", "120");

    class TestBasicImpl : public test::ITestBasic_Servant {
    public:
        virtual bool ReturnBoolean() {
            return true;
        }

        virtual bool In(uint32_t a, ::nprpc::flat::Boolean b, ::nprpc::flat::Span<uint8_t> c) {
            EXPECT_EQ(a, 42u);
            EXPECT_TRUE(b.get());

            // Verify we received exactly 3MB of data
            EXPECT_EQ(c.size(), 3 * 1024 * 1024u);

            // Verify pattern in first and last bytes
            EXPECT_EQ(c[0], 0xAB);
            EXPECT_EQ(c[c.size() - 1], 0xCD);

            return true;
        }

        virtual void Out(uint32_t& a, ::nprpc::flat::Boolean& b, ::nprpc::flat::Vector_Direct1<uint8_t> c) {
            a = 42;
            b = true;

            // Create 3MB of test data
            const size_t large_size = 3 * 1024 * 1024; // 3MB
            c.length(large_size);
            auto span = c();
            
            // Fill with test pattern - first byte 0xAB, last byte 0xCD, middle with sequence
            span[0] = 0xAB;
            span[large_size - 1] = 0xCD;
            for (size_t i = 1; i < large_size - 1; ++i) {
                span[i] = static_cast<uint8_t>(i % 256);
            }
        }
    } servant;

    auto exec_test = [this, &servant](nprpc::ObjectActivationFlags::Enum flags) { 
        try {   
            auto obj = make_stuff_happen<test::TestBasic>(servant, flags);
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
}

// Bad input validation test
TEST_F(NprpcTest, TestBadInput) {
    class TestBadInputImpl : public test::ITestBadInput_Servant {
    public:
        virtual void In(::nprpc::flat::Span<uint8_t> a) {}
    } servant;

    auto exec_test = [this, &servant](nprpc::ObjectActivationFlags::Enum flags) { 
        try {
            auto obj = make_stuff_happen<test::TestBadInput>(servant, flags);

            boost::beast::flat_buffer buf;
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
}


} // namespace nprpctest

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    
    // Register the test environment
    ::testing::AddGlobalTestEnvironment(new nprpctest::NprpcTestEnvironment);
    
    return RUN_ALL_TESTS();
}

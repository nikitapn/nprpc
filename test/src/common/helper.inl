#include "nprpc_base.hpp"
using namespace std::string_literals;
using namespace std::string_view_literals;

#ifndef NPRPC_USE_GTEST
# define FAIL() std::cerr
# define GTEST_OVERRIDE

inline void log(
    const std::string_view message,
    const std::source_location location = std::source_location::current())
{
    std::cerr << "file: "
              << location.file_name() << '('
              << location.line() << ':'
              << location.column() << ") `"
              << location.function_name() << "`: "
              << message << '\n';
}

# define EXPECT_TRUE(x) if (!(x)) {  \
    std::stringstream ss;             \
    ss << "EXPECT_TRUE failed: " #x;  \
    throw nprpc::test::AssertionFailed{ss.str()}; \
}
# define EXPECT_FALSE(x) if (x) {    \
    std::stringstream ss;             \
    ss << "EXPECT_FALSE failed: " #x; \
    throw nprpc::test::AssertionFailed{ss.str()}; \
}
# define EXPECT_EQ(x, y) if (!((x) == (y))) {                                                                    \
    std::stringstream ss;                                                                                        \
    ss <<"EXPECT_EQ failed: " #x " == " #y " (" << (x) << " != " << (y) << ")";                                 \
    throw nprpc::test::AssertionFailed{ss.str()};                                                                       \
}
#else
# define GTEST_OVERRIDE override
#endif

namespace nprpctest {
using thread_pool = nprpc::thread_pool_1;
// Helper class to manage nameserver process
class NameserverManager {
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
            // Use the build directory configured by CMake
#ifdef NPRPC_BUILD_DIR
            std::string nameserver_path = std::string(NPRPC_BUILD_DIR) + "/npnameserver";
            execl(nameserver_path.c_str(), "npnameserver", nullptr);
#else
            // Fallback: try common build directories
            execl("/home/nikita/projects/nprpc/.build_relwith_debinfo/npnameserver", "npnameserver", nullptr);
            execl("/home/nikita/projects/nprpc/.build_release/npnameserver", "npnameserver", nullptr);
            execl("/home/nikita/projects/nprpc/.build_debug/npnameserver", "npnameserver", nullptr);
#endif
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
class NprpcTestEnvironment
#ifdef NPRPC_USE_GTEST
: public ::testing::Environment
#endif
{
public:
    void SetUp() GTEST_OVERRIDE {
        // Start the nameserver first
        if (!nameserver_manager.start_nameserver()) {
            FAIL() << "Failed to start nameserver process";
        }

        try {
            // Use the new RpcBuilder API
            rpc = nprpc::RpcBuilder()
                .set_log_level(nprpc::LogLevel::trace)
                .with_hostname("localhost")
                .enable_ssl_client_self_signed_cert("/home/nikita/projects/nprpc/certs/out/localhost.crt")
                .with_tcp(22222)
                .with_udp(22224)
                .with_http(22223)
                    .root_dir("/home/nikita/projects/nprpc/test/http")
                    .ssl("/home/nikita/projects/nprpc/certs/out/localhost.crt",
                         "/home/nikita/projects/nprpc/certs/out/localhost.key")
#ifdef NPRPC_HTTP3_ENABLED
                    .enable_http3()
#endif
#if defined(NPRPC_HAS_QUIC) || defined(NPRPC_QUIC_ENABLED)
                .with_quic(22225)
                    .ssl("/home/nikita/projects/nprpc/certs/out/localhost.crt",
                         "/home/nikita/projects/nprpc/certs/out/localhost.key")
#endif
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

    void TearDown() GTEST_OVERRIDE {
        std::cout << "Tearing down test environment..." << std::endl;
        // Stop the nameserver FIRST - before we destroy shared memory resources
        // that the nameserver might be using
        nameserver_manager.stop_nameserver();
        std::cout << "Nameserver stopped." << std::endl;
        // Destroy rpc while io_context is still running
        // This allows pending async operations to complete
        if (rpc) {
            rpc->destroy();
            std::cout << "RPC destroyed." << std::endl;
            rpc = nullptr;
        }
        // Now stop the io_context and wait for threads
        std::cout << "About to stop io_context..." << std::endl;
        thread_pool::get_instance().stop();
        std::cout << "IO context stopped, about to wait..." << std::endl;
        thread_pool::get_instance().wait();
        std::cout << "Thread pool wait completed." << std::endl;
        std::cout << "Test environment torn down." << std::endl;
    }
};

// Test fixture class for shared functionality
#ifdef NPRPC_USE_GTEST
class NprpcTest : public ::testing::Test
{
protected:
#endif
    template<typename T>
#ifndef NPRPC_USE_GTEST
inline
#endif
    void bind(
        typename T::servant_t& servant,
        std::uint32_t flags,
        const std::string& object_name = "nprpc_test_object"
    ) {
        auto oid = poa->activate_object(&servant, flags);

        auto nameserver = rpc->get_nameserver("127.0.0.1");
        nameserver->Bind(oid, object_name);
    }

    template<typename T>
#ifndef NPRPC_USE_GTEST
inline
#endif
    auto bind_and_resolve(
        typename T::servant_t& servant,
        std::uint32_t flags,
        const std::string& object_name = "nprpc_test_object"
    ) {
        bind(servant, flags, object_name);

        nprpc::Object* raw;
        auto nameserver = rpc->get_nameserver("127.0.0.1");
        EXPECT_TRUE(nameserver->Resolve(object_name, raw));

        return nprpc::ObjectPtr(nprpc::narrow<T>(raw));
    }
#ifdef NPRPC_USE_GTEST
};
#endif
} // namespace nprpctest

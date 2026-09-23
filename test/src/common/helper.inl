#include "nprpc_base_ext.hpp"
#include <fcntl.h>
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
// Helper class to manage nameserver process
// NPRPC_BUILD_DIR and NPRPC_SOURCE_DIR come from test/CMakeLists.txt.
#define NPRPC_TEST_CERT NPRPC_SOURCE_DIR "/certs/out/localhost.crt"
#define NPRPC_TEST_KEY NPRPC_SOURCE_DIR "/certs/out/localhost.key"

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
            int stdout_fd = open("/tmp/npnameserver_stdout.log",
                                 O_WRONLY | O_CREAT | O_TRUNC,
                                 0644);
            int stderr_fd = open("/tmp/npnameserver_stderr.log",
                                 O_WRONLY | O_CREAT | O_TRUNC,
                                 0644);

            if (stdout_fd != -1) {
                dup2(stdout_fd, STDOUT_FILENO);
                close(stdout_fd);
            }

            if (stderr_fd != -1) {
                dup2(stderr_fd, STDERR_FILENO);
                close(stderr_fd);
            }

            // Child process - run the nameserver from this build, with the
            // repo's development certificate so browser tests can use HTTPS.
            std::string nameserver_path = std::string(NPRPC_BUILD_DIR) + "/npnameserver";
            execl(nameserver_path.c_str(), "npnameserver",
                  "--cert", NPRPC_TEST_CERT, "--key", NPRPC_TEST_KEY,
                  "--allow-origin", "https://localhost:24443", nullptr);
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
            // std::cout << "Stopping nameserver with PID: " << nameserver_pid << std::endl;
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

extern nprpc::Rpc* rpc;
extern nprpc::Poa* poa;
extern NameserverManager nameserver_manager;

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
                .enable_ssl_client_self_signed_cert(NPRPC_TEST_CERT)
                // TestLargeMessage and TestNested send megabytes in one call,
                // which the default ring is deliberately too small for — see
                // config_default.hpp.  Raised here alongside the WebSocket and
                // WebTransport limits, for the same reason and by the same
                // amount.
                .shm_channel_sizes(16 * 1024 * 1024, 12 * 1024 * 1024)
                .with_tcp(22222)
                .with_http(22223)
                    .max_request_body_size(10'000)
                    .max_websocket_message_size(24 * 1024 * 1024)
                    .max_webtransport_message_size(24 * 1024 * 1024)
                    .max_http_rpc_requests_per_ip_per_second(64, 64)
                    .root_dir(NPRPC_SOURCE_DIR "/test/http")
                    .allow_origins({"https://localhost:24443"})
                    .ssl(NPRPC_TEST_CERT,
                         NPRPC_TEST_KEY)
#ifdef NPRPC_HTTP3_ENABLED
                    .enable_http3()
#endif
#if defined(NPRPC_HAS_QUIC) || defined(NPRPC_QUIC_ENABLED)
                .with_quic(22225)
                    .ssl(NPRPC_TEST_CERT,
                         NPRPC_TEST_KEY)
#endif
                .build();

            rpc->start_thread_pool(4);

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
        if (rpc) {
            rpc->destroy();
            rpc = nullptr;
        }
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
        nprpc::ObjectActivationFlags flags,
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
        nprpc::ObjectActivationFlags flags,
        const std::string& object_name = "nprpc_test_object"
    ) {
        bind<T>(servant, flags, object_name);

        nprpc::Object* raw;
        auto nameserver = rpc->get_nameserver("127.0.0.1");
        EXPECT_TRUE(nameserver->Resolve(object_name, raw));

        return nprpc::ObjectPtr(nprpc::narrow<T>(raw));
    }
#ifdef NPRPC_USE_GTEST
};
#endif
} // namespace nprpctest

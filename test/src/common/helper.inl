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
    throw test::AssertionFailed{ss.str()}; \
}
# define EXPECT_FALSE(x) if (x) {    \
    std::stringstream ss;             \
    ss << "EXPECT_FALSE failed: " #x; \
    throw test::AssertionFailed{ss.str()}; \
}
# define EXPECT_EQ(x, y) if (!((x) == (y))) {                                                                    \
    std::stringstream ss;                                                                                        \
    ss <<"EXPECT_EQ failed: " #x " == " #y " (" << (x) << " != " << (y) << ")";                                 \
    throw test::AssertionFailed{ss.str()};                                                                       \
}
#else
# define GTEST_OVERRIDE override
#endif

namespace nprpctest {
using thread_pool = nplib::thread_pool_1;
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
class NprpcTestEnvironment
#ifdef NPRPC_USE_GTEST
: public ::testing::Environment
#endif
{
public:
    void SetUp() GTEST_OVERRIDE {
// #ifdef NPRPC_USE_GTEST
        // Start the nameserver first
        if (!nameserver_manager.start_nameserver()) {
            FAIL() << "Failed to start nameserver process";
        }
// #endif

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

    void TearDown() GTEST_OVERRIDE {
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
#ifdef NPRPC_USE_GTEST
class NprpcTest : public ::testing::Test
{
protected:
#endif
    template<typename T>
#ifndef NPRPC_USE_GTEST
inline
#endif
    auto make_stuff_happen(
        typename T::servant_t& servant,
        std::uint32_t flags,
        const std::string& object_name = "nprpc_test_object"
    ) {
        auto nameserver = rpc->get_nameserver("127.0.0.1");
        auto oid = poa->activate_object(&servant, flags);
        nameserver->Bind(oid, object_name);

        nprpc::Object* raw;
        EXPECT_TRUE(nameserver->Resolve(object_name, raw));

        return nprpc::ObjectPtr(nprpc::narrow<T>(raw));
    }
#ifdef NPRPC_USE_GTEST
};
#endif
} // namespace nprpctest
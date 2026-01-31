// Simple test server for Swift client testing
// Activates a ShapeService servant and prints the stringified object reference
// Swift can use NPRPCObject.fromString() to connect without needing nameserver

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <signal.h>
#include <thread>

#include <nprpc/nprpc.hpp>
#include <nprpc/impl/misc/thread_pool.hpp>

// Include generated code for ShapeService
#include "../Tests/NPRPCTests/Generated/basic_test.hpp"

using thread_pool = nprpc::thread_pool_1;

std::condition_variable cv;
std::mutex cv_m;
bool shutdown_requested = false;

// Simple ShapeService implementation
class ShapeServiceImpl : public swift::test::IShapeService_Servant {
    swift::test::Rectangle stored_rect_;
public:
    void getRectangle(uint32_t id, swift::test::flat::Rectangle_Direct rect) override {
        std::cout << "getRectangle called with id=" << id << std::endl;
        rect.topLeft().x() = stored_rect_.topLeft.x;
        rect.topLeft().y() = stored_rect_.topLeft.y;
        rect.bottomRight().x() = stored_rect_.bottomRight.x;
        rect.bottomRight().y() = stored_rect_.bottomRight.y;
        rect.color() = stored_rect_.color;
    }
    
    void setRectangle(uint32_t id, swift::test::flat::Rectangle_Direct rect) override {
        std::cout << "setRectangle called with id=" << id << std::endl;
        stored_rect_.topLeft.x = rect.topLeft().x();
        stored_rect_.topLeft.y = rect.topLeft().y();
        stored_rect_.bottomRight.x = rect.bottomRight().x();
        stored_rect_.bottomRight.y = rect.bottomRight().y();
        stored_rect_.color = rect.color();
    }
};

int main(int argc, char** argv) {
    try {
        // Initialize RPC
        auto* rpc = nprpc::RpcBuilder()
            .set_log_level(nprpc::LogLevel::trace)
            .set_hostname("localhost")
            .with_tcp(22222)
            .with_http(22223)
            .build(thread_pool::get_instance().ctx());

        // Create POA
        auto* poa = rpc->create_poa()
            .with_max_objects(128)
            .with_lifespan(nprpc::PoaPolicy::Lifespan::Persistent)
            .build();

        // Create and activate servant
        ShapeServiceImpl shape_service;
        
        using namespace nprpc::ObjectActivationFlags;
        constexpr auto flags = ALLOW_TCP | ALLOW_HTTP;
        
        auto oid = poa->activate_object(&shape_service, flags);
        
        // Print the stringified object reference
        std::string ior = oid.to_string();
        
        std::cout << "========================================" << std::endl;
        std::cout << "ShapeService activated!" << std::endl;
        std::cout << "Object Reference (IOR):" << std::endl;
        std::cout << ior << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "Press Ctrl+C to shutdown..." << std::endl;

        // Handle SIGINT for graceful shutdown
        signal(SIGINT, [](int) {
            std::cout << "\nShutdown requested..." << std::endl;
            {
                std::lock_guard lk(cv_m);
                shutdown_requested = true;
            }
            cv.notify_one();
        });

        // Wait for shutdown
        std::unique_lock lk(cv_m);
        cv.wait(lk, [] { return shutdown_requested; });

        // Cleanup
        std::cout << "Cleaning up..." << std::endl;
        rpc->destroy();
        thread_pool::get_instance().stop();
        thread_pool::get_instance().wait();
        
        std::cout << "Server stopped." << std::endl;
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}

#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/impl/shared_memory_listener.hpp>
#include <nprpc/impl/shared_memory_channel.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <iostream>
#include <memory>

namespace nprpc::impl {

// Forward declaration - implemented in shared_memory_server_session.cpp
std::shared_ptr<Session> create_shared_memory_server_session(
    boost::asio::io_context& ioc,
    std::unique_ptr<SharedMemoryChannel> channel);

// Per process unique identifier for shared memory listener
NPRPC_API std::string g_server_listener_uuid;

// Global shared memory listener - one per server process
static std::shared_ptr<SharedMemoryListener> g_shared_memory_listener;

void init_shared_memory_listener(boost::asio::io_context& ioc) {
  // Generate unique UUID for this server process's shared memory listener
  g_server_listener_uuid = boost::uuids::to_string(boost::uuids::random_generator()());

  std::cout << "Starting shared memory listener: " << g_server_listener_uuid << std::endl;

  // Create the global listener with the server's UUID
  g_shared_memory_listener = std::make_shared<SharedMemoryListener>(
    ioc,
    g_server_listener_uuid,
    [&ioc](std::unique_ptr<SharedMemoryChannel> channel) {
      // Accept handler - create a server session for this connection
      auto session = create_shared_memory_server_session(ioc, std::move(channel));
      
      // Add session to the global connection pool
      // This keeps the session alive and allows it to process requests
      g_orb->add_connection(std::move(session));
    });

  g_shared_memory_listener->start();
  std::cout << "Shared memory listener started successfully" << std::endl;
}

} // namespace nprpc::impl

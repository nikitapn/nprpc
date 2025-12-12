#include <atomic>
#include <boost/asio.hpp>
#include <chrono>
#include <gtest/gtest.h>
#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/impl/shared_memory_channel.hpp>
#include <nprpc/impl/shared_memory_listener.hpp>
#include <thread>

using namespace nprpc::impl;

// Simple echo test: client sends message, server echoes it back
TEST(SharedMemoryListener, AcceptAndCommunicate)
{
  boost::asio::io_context ioc;
  auto work_guard = boost::asio::make_work_guard(ioc); // Keep io_context alive

  std::atomic<bool> server_received{false};
  std::atomic<bool> client_received{false};

  std::string test_message = "Hello, Shared Memory!";
  std::string listener_name =
      "test_listener_" + std::to_string(std::time(nullptr));

  // Server side: create listener
  std::unique_ptr<SharedMemoryChannel> server_channel;

  SharedMemoryListener listener(
      ioc, listener_name, [&](std::unique_ptr<SharedMemoryChannel> channel) {
        server_channel = std::move(channel);

        // Set up receive handler on server
        server_channel->on_data_received = [&](std::vector<char>&& data) {
          std::string received(data.begin(), data.end());
          EXPECT_EQ(received, test_message);
          server_received = true;

          // Echo back
          server_channel->send(data.data(), data.size());
        };
      });

  listener.start();

  // Give listener time to start
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Run io_context in separate thread BEFORE connecting
  // This ensures handlers can be called immediately when data arrives
  std::thread io_thread([&]() { ioc.run(); });

  // Give io_context time to start
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // Client side: connect to listener
  auto client_channel = connect_to_shared_memory_listener(ioc, listener_name);

  client_channel->on_data_received = [&](std::vector<char>&& data) {
    std::string received(data.begin(), data.end());
    EXPECT_EQ(received, test_message);
    client_received = true;
    ioc.stop();
  };

  // Send message from client
  ASSERT_TRUE(client_channel->send(test_message.data(), test_message.size()));

  // Wait for communication to complete (with timeout)
  auto start = std::chrono::steady_clock::now();
  while (!server_received || !client_received) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    auto elapsed = std::chrono::steady_clock::now() - start;
    if (elapsed > std::chrono::seconds(5)) {
      FAIL() << "Timeout waiting for communication";
      break;
    }
  }

  listener.stop();
  ioc.stop();

  if (io_thread.joinable()) {
    io_thread.join();
  }

  EXPECT_TRUE(server_received);
  EXPECT_TRUE(client_received);
}

// Test multiple concurrent connections
TEST(SharedMemoryListener, MultipleConnections)
{
  boost::asio::io_context ioc;
  auto work_guard = boost::asio::make_work_guard(ioc); // Keep io_context alive

  std::string listener_name =
      "test_multi_" + std::to_string(std::time(nullptr));

  std::atomic<int> connections_accepted{0};
  std::vector<std::unique_ptr<SharedMemoryChannel>> server_channels;
  std::mutex channels_mutex;

  SharedMemoryListener listener(
      ioc, listener_name, [&](std::unique_ptr<SharedMemoryChannel> channel) {
        std::lock_guard<std::mutex> lock(channels_mutex);
        connections_accepted++;
        server_channels.push_back(std::move(channel));
      });

  listener.start();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Create 3 client connections
  const int num_clients = 3;
  std::vector<std::unique_ptr<SharedMemoryChannel>> client_channels;

  for (int i = 0; i < num_clients; ++i) {
    auto channel = connect_to_shared_memory_listener(ioc, listener_name);
    ASSERT_NE(channel, nullptr);
    client_channels.push_back(std::move(channel));
  }

  // Wait for all connections to be accepted
  auto start = std::chrono::steady_clock::now();
  while (connections_accepted < num_clients) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    auto elapsed = std::chrono::steady_clock::now() - start;
    if (elapsed > std::chrono::seconds(5)) {
      FAIL() << "Timeout waiting for connections. Accepted: "
             << connections_accepted;
      break;
    }
  }

  EXPECT_EQ(connections_accepted, num_clients);
  EXPECT_EQ(server_channels.size(), num_clients);
  EXPECT_EQ(client_channels.size(), num_clients);

  listener.stop();
}

// Test listener endpoint string format
TEST(SharedMemoryListener, EndpointFormat)
{
  boost::asio::io_context ioc;
  std::string listener_name = "my_service";

  SharedMemoryListener listener(ioc, listener_name,
                                [](std::unique_ptr<SharedMemoryChannel>) {});

  EXPECT_EQ(listener.listener_name(), "my_service");
  EXPECT_EQ(listener.endpoint_string(), "mem://my_service");
}

int main(int argc, char** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

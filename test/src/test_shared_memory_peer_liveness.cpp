// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT
//
// Peer-death detection for the shared memory transport.  A killed client
// leaves the rings mapped and silent, so the server can only notice by
// probing the peer: the graceful detach flag first, then whether the client
// process still exists.

#include <gtest/gtest.h>

#include <nprpc/impl/process_identity.hpp>
#include <nprpc/impl/shared_memory_channel.hpp>
#include <nprpc/impl/shared_memory_listener.hpp>

#include <boost/asio.hpp>

#include <chrono>
#include <csignal>
#include <ctime>
#include <string>
#include <thread>
#include <unistd.h>
#include <sys/wait.h>

using namespace nprpc::impl;

namespace {

std::string unique_listener_name(const char* tag)
{
  return std::string("test_liveness_") + tag + "_" +
         std::to_string(::getpid()) + "_" +
         std::to_string(std::time(nullptr));
}

// Poll `pred` until it holds or the deadline passes.
template <typename Pred>
bool wait_until(Pred pred, std::chrono::milliseconds timeout)
{
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (pred())
      return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return pred();
}

void write_all(int fd, const void* data, size_t size)
{
  const char* p = static_cast<const char*>(data);
  while (size > 0) {
    const ssize_t n = ::write(fd, p, size);
    if (n <= 0)
      return;
    p += n;
    size -= static_cast<size_t>(n);
  }
}

bool read_all(int fd, void* data, size_t size)
{
  char* p = static_cast<char*>(data);
  while (size > 0) {
    const ssize_t n = ::read(fd, p, size);
    if (n <= 0)
      return false;
    p += n;
    size -= static_cast<size_t>(n);
  }
  return true;
}

} // namespace

TEST(ProcessLiveness, CurrentProcessIsAliveAndIdentified)
{
  const auto self = current_process_identity();

  EXPECT_TRUE(self.valid());
  EXPECT_EQ(self.pid, static_cast<uint32_t>(::getpid()));
  EXPECT_TRUE(process_alive(self));
}

TEST(ProcessLiveness, UnknownPeerCountsAsAlive)
{
  // pid 0 means "peer never told us who it is" — reaping on that would kill
  // healthy sessions.
  EXPECT_TRUE(process_alive(ProcessIdentity{}));
}

TEST(ProcessLiveness, KilledProcessIsDetected)
{
  int id_pipe[2];
  ASSERT_EQ(::pipe(id_pipe), 0);

  const pid_t child = ::fork();
  ASSERT_NE(child, -1);

  if (child == 0) {
    ::close(id_pipe[0]);
    const auto id = current_process_identity();
    write_all(id_pipe[1], &id, sizeof(id));
    ::close(id_pipe[1]);
    for (;;)
      ::pause();
    ::_exit(0);
  }

  ::close(id_pipe[1]);
  ProcessIdentity child_id{};
  ASSERT_TRUE(read_all(id_pipe[0], &child_id, sizeof(child_id)));
  ::close(id_pipe[0]);

  EXPECT_EQ(child_id.pid, static_cast<uint32_t>(child));
  EXPECT_TRUE(process_alive(child_id));

  ASSERT_EQ(::kill(child, SIGKILL), 0);
  int status = 0;
  ASSERT_EQ(::waitpid(child, &status, 0), child);

  EXPECT_FALSE(process_alive(child_id));
}

TEST(ProcessLiveness, RecycledPidIsNotMistakenForThePeer)
{
  // Same pid, different start time: the original process is gone and an
  // unrelated one now wears its number.
  auto self = current_process_identity();
  ASSERT_NE(self.start_token, 0u)
      << "platform reports no process start time; pid reuse cannot be caught";

  self.start_token += 1;
  EXPECT_FALSE(process_alive(self));
}

// A client that closes its channel cleanly flags it, so the server sees the
// disconnect immediately instead of waiting for the process to exit.
TEST(SharedMemoryPeerLiveness, GracefulCloseIsDetected)
{
  boost::asio::io_context ioc;
  const auto listener_name = unique_listener_name("graceful");

  std::unique_ptr<SharedMemoryChannel> server_channel;
  SharedMemoryListener listener(
      ioc, listener_name, [&](std::unique_ptr<SharedMemoryChannel> channel) {
        server_channel = std::move(channel);
      });
  listener.start();

  auto client_channel = connect_to_shared_memory_listener(ioc, listener_name);
  ASSERT_TRUE(wait_until([&] { return server_channel != nullptr; },
                         std::chrono::seconds(5)));

  EXPECT_TRUE(server_channel->peer_alive());

  client_channel.reset(); // graceful close

  EXPECT_FALSE(server_channel->peer_alive());
}

// The case shared memory cannot see on its own: the client process is killed
// with the channel still mapped.  Nothing is written, nothing is closed —
// only the process probe catches it.
TEST(SharedMemoryPeerLiveness, KilledClientProcessIsDetected)
{
  const auto listener_name = unique_listener_name("killed");

  int to_child[2];
  int to_parent[2];
  ASSERT_EQ(::pipe(to_child), 0);
  ASSERT_EQ(::pipe(to_parent), 0);

  // Fork before any thread exists in this process: the child runs real
  // allocating code (mmap, shm_open), which is only safe if no other thread
  // could have been holding a lock at fork time.
  const pid_t child = ::fork();
  ASSERT_NE(child, -1);

  if (child == 0) {
    ::close(to_child[1]);
    ::close(to_parent[0]);

    char go = 0;
    if (!read_all(to_child[0], &go, 1))
      ::_exit(1);

    try {
      boost::asio::io_context child_ioc;
      // Never started reading and never closed: from here on this process
      // only exists, which is exactly what the server has to notice.
      auto channel =
          connect_to_shared_memory_listener(child_ioc, listener_name);
      const char ok = channel ? 'y' : 'n';
      write_all(to_parent[1], &ok, 1);
      for (;;)
        ::pause();
    } catch (...) {
      const char ok = 'n';
      write_all(to_parent[1], &ok, 1);
    }
    ::_exit(1);
  }

  ::close(to_child[0]);
  ::close(to_parent[1]);

  boost::asio::io_context ioc;
  std::unique_ptr<SharedMemoryChannel> server_channel;
  SharedMemoryListener listener(
      ioc, listener_name, [&](std::unique_ptr<SharedMemoryChannel> channel) {
        server_channel = std::move(channel);
      });
  listener.start();

  const char go = 'g';
  write_all(to_child[1], &go, 1);

  char connected = 0;
  ASSERT_TRUE(read_all(to_parent[0], &connected, 1)) << "child never connected";
  ASSERT_EQ(connected, 'y');
  ASSERT_TRUE(wait_until([&] { return server_channel != nullptr; },
                         std::chrono::seconds(5)));

  // The handshake told us who the client is.
  EXPECT_EQ(server_channel->peer_process().pid, static_cast<uint32_t>(child));
  EXPECT_TRUE(server_channel->peer_alive());

  ASSERT_EQ(::kill(child, SIGKILL), 0);
  int status = 0;
  ASSERT_EQ(::waitpid(child, &status, 0), child);

  EXPECT_FALSE(server_channel->peer_alive());

  ::close(to_child[1]);
  ::close(to_parent[0]);
}

int main(int argc, char** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

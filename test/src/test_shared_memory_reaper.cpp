// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT
//
// Shared memory outliving the process that made it.
//
// The server creates a client's rings and is the only side that removes
// them — which works right up to the case that matters, a server that is
// killed or exits without unwinding and so runs no destructor at all.  Its
// rings then hold resident memory until the machine reboots.  The sweep at
// listener startup is the answer, and these are the two things it has to get
// right: take what is dead, and leave everything else alone.

#include <gtest/gtest.h>

#include <nprpc/impl/lock_free_ring_buffer.hpp>
#include <nprpc/impl/process_identity.hpp>
#include <nprpc/impl/shared_memory_listener.hpp>

// Visibility is hidden and neither of these is part of the ABI, so they are
// compiled into the test rather than linked — the same arrangement
// test_lock_free_ring_buffer uses.
#include "../../src/shm/lock_free_ring_buffer.cpp"
#include "../../src/shm/shared_memory_reaper.cpp"

#include <ctime>
#include <filesystem>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

using namespace nprpc::impl;

namespace {

std::string unique_channel(const char* tag)
{
  return std::string("test_reaper_") + tag + "_" +
         std::to_string(::getpid()) + "_" +
         std::to_string(std::time(nullptr));
}

bool segment_exists(const std::string& shm_name)
{
  // shm_name carries the leading slash every ring name has; on Linux the
  // file itself is that name under /dev/shm.
  return std::filesystem::exists("/dev/shm" + shm_name);
}

// Create a ring in a child process that then dies without unwinding, which
// is the only way to produce the state under test: a live segment whose
// writer is provably gone.
void orphan_a_ring(const std::string& shm_name)
{
  const pid_t pid = ::fork();
  ASSERT_NE(pid, -1) << "fork failed";

  if (pid == 0) {
    auto ring = LockFreeRingBuffer::create(shm_name, 64 * 1024);
    const auto self = current_process_identity();
    ring->header()->writer_start_token.store(self.start_token);
    ring->header()->writer_pid.store(self.pid);
    // _exit, not exit: no destructors, no removal — exactly what a killed
    // server does.  The ring is deliberately leaked.
    (void)ring.release();
    ::_exit(0);
  }

  int status = 0;
  ASSERT_EQ(::waitpid(pid, &status, 0), pid);
}

} // namespace

// The whole point: a segment whose owner is gone does not survive the next
// listener.
TEST(SharedMemoryReaper, TakesARingWhoseProcessIsGone)
{
  const std::string name = "/nprpc_" + unique_channel("dead") + "_s2c";

  orphan_a_ring(name);
  ASSERT_TRUE(segment_exists(name)) << "child did not leave the ring behind";

  EXPECT_GT(reap_stale_shm_segments(), 0u);
  EXPECT_FALSE(segment_exists(name));
}

// The failure that would matter far more than the leak: sweeping away a ring
// somebody is still using.  This one's writer is this very process.
TEST(SharedMemoryReaper, LeavesARingWhoseProcessIsAlive)
{
  const std::string name = "/nprpc_" + unique_channel("alive") + "_s2c";

  auto ring = LockFreeRingBuffer::create(name, 64 * 1024);
  const auto self = current_process_identity();
  ring->header()->writer_start_token.store(self.start_token);
  ring->header()->writer_pid.store(self.pid);

  reap_stale_shm_segments();

  EXPECT_TRUE(segment_exists(name));
}

// A ring created but not yet claimed says nothing about anybody, and the
// server that has just made a pair for an arriving client is in exactly that
// state.  Reaping on "no owner recorded" would race every connection.
TEST(SharedMemoryReaper, LeavesARingNobodyHasClaimed)
{
  const std::string name = "/nprpc_" + unique_channel("unclaimed") + "_c2s";

  auto ring = LockFreeRingBuffer::create(name, 64 * 1024);
  ASSERT_EQ(ring->header()->writer_pid.load(), 0u);

  reap_stale_shm_segments();

  EXPECT_TRUE(segment_exists(name));
}

// Both rings of a channel go together, and the evidence may be on either
// one: here the client's half names a dead process while the server's half
// names nobody.  A per-segment rule would take one and leave the other.
TEST(SharedMemoryReaper, TakesBothHalvesOfAChannel)
{
  const std::string channel = "/nprpc_" + unique_channel("pair");
  const std::string c2s = channel + "_c2s";
  const std::string s2c = channel + "_s2c";

  orphan_a_ring(c2s);
  auto server_half = LockFreeRingBuffer::create(s2c, 64 * 1024);
  ASSERT_TRUE(segment_exists(c2s));
  ASSERT_TRUE(segment_exists(s2c));

  reap_stale_shm_segments();

  EXPECT_FALSE(segment_exists(c2s));
  EXPECT_FALSE(segment_exists(s2c));
}

// One live end protects the other: a client that died leaves a ring its
// still-running server has mapped, and pulling the name out from under it
// buys nothing — the server is about to notice and clean up itself.
TEST(SharedMemoryReaper, LeavesAChannelWithOneEndAlive)
{
  const std::string channel = "/nprpc_" + unique_channel("halflive");
  const std::string c2s = channel + "_c2s";
  const std::string s2c = channel + "_s2c";

  orphan_a_ring(c2s);
  auto server_half = LockFreeRingBuffer::create(s2c, 64 * 1024);
  const auto self = current_process_identity();
  server_half->header()->writer_start_token.store(self.start_token);
  server_half->header()->writer_pid.store(self.pid);

  reap_stale_shm_segments();

  EXPECT_TRUE(segment_exists(c2s));
  EXPECT_TRUE(segment_exists(s2c));

  // Not ours to leave lying about once the test is over.
  LockFreeRingBuffer::remove(c2s);
}

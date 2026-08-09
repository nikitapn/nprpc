// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT
//
// What a stream producer is told when its consumer stops consuming.
//
// A shared-memory ring is finite (kRingSlots headers, buffer_size payload
// bytes) and its consumer is another process, so "the peer stopped draining"
// is a state the producer reaches routinely: a client parked in a debugger, a
// client that got killed, or a client whose reader thread is simply slower
// than the producer.  Two questions matter and are asked separately below:
//
//   1. Does a write to a full ring block?  (It must not: the ring has no
//      blocking write path, and a producer that blocks holds up whatever
//      thread it runs on — in a compositor, the frame loop.)
//   2. Does the producer find out that the chunk went nowhere?  (It must: a
//      reliable stream that silently loses a chunk leaves the consumer with
//      a sequence gap and the producer believing it delivered.)
//
// The parked-write tests cover the other half of the same story: credit
// exhaustion.  A write with no credits is queued rather than sent, and the
// queue is drained by a window update from the consumer — which a dead
// consumer never sends.  Every queued write owns a callback that some caller
// is waiting on (the Swift `await writer.write(...)` resumes from it), so a
// queue entry that is dropped without its callback is a caller suspended
// forever.

#include <gtest/gtest.h>

#include <nprpc/flat_buffer.hpp>
#include <nprpc/impl/shared_memory_channel.hpp>
#include <nprpc/impl/stream_manager.hpp>
#include <nprpc/session_context.h>

#include <boost/asio/io_context.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <memory>
#include <string>
#include <unistd.h>
#include <vector>

using namespace nprpc;
using namespace nprpc::impl;
using namespace std::chrono_literals;

namespace {

std::string unique_channel_name(const char* tag)
{
  return std::string("test_backpressure_") + tag + "_" +
         std::to_string(::getpid()) + "_" + std::to_string(std::time(nullptr));
}

// A StreamManager with no Session behind it.
//
// Everything under test here lives between the writer and the transport
// callback, so the transport is the only thing worth faking — and passing a
// null self_cell is what makes that possible: dispatch_buffer only consults
// the cell when there is one, so a manager built this way behaves exactly
// like one whose session is alive.
struct BareManager {
  boost::asio::io_context ioc;
  SessionContext ctx;
  std::shared_ptr<impl::StreamManager> mgr;

  BareManager()
      : mgr(std::make_shared<impl::StreamManager>(ctx, nullptr,
                                                  ioc.get_executor()))
  {
    ctx.stream_manager = mgr.get();
  }

  // Streams only dispatch once started; a real one is started when the
  // StreamInit reply goes out, which is what these two calls stand in for.
  void start(uint64_t stream_id)
  {
    mgr->defer_stream_start(stream_id);
    mgr->on_reply_sent();
    ioc.restart();
    ioc.poll();
  }
};

const std::vector<uint8_t> kChunk(64, 0xAB);

} // namespace

// ── 1. The ring itself ───────────────────────────────────────────────────────

// A full ring refuses the write; it does not wait for space.
//
// Written against a real SharedMemoryChannel with no peer at all, which is
// the same thing the producer sees when the peer is alive but not reading:
// nobody advances read_cursor, so every slot stays claimed.
TEST(StreamBackpressure, FullRingFailsFastRatherThanBlocking)
{
  boost::asio::io_context ioc;
  SharedMemoryChannel channel(ioc, unique_channel_name("ringfull"),
                              /*is_server=*/true, /*create_rings=*/true);

  const std::vector<uint8_t> payload(1024, 0x5A);

  size_t accepted = 0;
  bool refused = false;
  const auto started = std::chrono::steady_clock::now();

  // Bounded by the slot ring (kRingSlots) long before the payload ring, so
  // this terminates quickly; the generous cap only guards a runaway.
  for (size_t i = 0; i < 100000; ++i) {
    if (!channel.send(payload.data(), static_cast<uint32_t>(payload.size()))) {
      refused = true;
      break;
    }
    ++accepted;
  }

  const auto elapsed = std::chrono::steady_clock::now() - started;

  EXPECT_TRUE(refused) << "ring never filled up";
  EXPECT_GT(accepted, 0u);
  // The point of the test: refusal, not a wait.  A blocking implementation
  // would sit in the failing send until something drained the ring, and
  // nothing ever will.
  EXPECT_LT(elapsed, 5s);

  // And it stays refused — no partial-progress illusion.
  EXPECT_FALSE(channel.send(payload.data(),
                            static_cast<uint32_t>(payload.size())));
}

// ── 2. What the writer is told ───────────────────────────────────────────────

// send_chunk() must report the transport's refusal, not just the session's
// death.  Before the fix it returned true unconditionally whenever a send
// callback existed, so a producer writing into a full ring saw every write
// succeed while the bytes went nowhere.
TEST(StreamBackpressure, SendChunkReportsFullRing)
{
  boost::asio::io_context ioc;
  SharedMemoryChannel channel(ioc, unique_channel_name("sendchunk"),
                              /*is_server=*/true, /*create_rings=*/true);

  BareManager m;
  // Mirrors SharedMemoryServerSession::send_stream_message.
  m.mgr->set_send_callback([&channel](flat_buffer&& fb) {
    return channel.send(fb.data().data(), static_cast<uint32_t>(fb.size()));
  });

  constexpr uint64_t kStream = 0x51;
  m.start(kStream);

  size_t sent = 0;
  bool reported_failure = false;
  for (size_t i = 0; i < 100000; ++i) {
    if (!m.mgr->send_chunk(kStream, kChunk, i)) {
      reported_failure = true;
      break;
    }
    ++sent;
  }

  EXPECT_GT(sent, 0u) << "nothing was ever accepted";
  EXPECT_TRUE(reported_failure)
      << "send_chunk kept reporting success into a full ring";
}

// ── 3. Parked writes ─────────────────────────────────────────────────────────

// A write parked for credits must not be forgotten when the stream goes away.
//
// The callback is how the caller learns the write finished; dropping it
// silently is not a lost message, it is a caller that never wakes up.
TEST(StreamBackpressure, CancelAllFailsParkedWrites)
{
  BareManager m;
  std::atomic<int> delivered{0};
  m.mgr->set_send_callback([&delivered](flat_buffer&&) {
    ++delivered;
    return true;
  });

  constexpr uint64_t kStream = 0x52;
  m.start(kStream);
  m.mgr->register_external_writer(kStream, /*initial_credits=*/2);

  std::atomic<int> callbacks{0};
  std::atomic<int> failures{0};
  const auto note = [&callbacks, &failures](bool ok) {
    ++callbacks;
    if (!ok)
      ++failures;
  };

  // Two fit in the window; the rest park waiting for a window update that a
  // dead consumer will never send.
  for (uint64_t i = 0; i < 5; ++i)
    m.mgr->write_chunk_or_queue(kStream, kChunk, i, note);

  EXPECT_EQ(delivered.load(), 2);
  EXPECT_EQ(callbacks.load(), 2) << "parked writes should not have completed yet";

  m.mgr->cancel_all();

  EXPECT_EQ(callbacks.load(), 5)
      << "parked writes were dropped without telling their callers";
  EXPECT_EQ(failures.load(), 3) << "parked writes must be reported as failed";
}

// The same obligation when the peer cancels the stream rather than the whole
// session going down.
TEST(StreamBackpressure, StreamCancelFailsParkedWrites)
{
  BareManager m;
  m.mgr->set_send_callback([](flat_buffer&&) { return true; });

  constexpr uint64_t kStream = 0x53;
  m.start(kStream);
  m.mgr->register_external_writer(kStream, /*initial_credits=*/1);

  std::atomic<int> callbacks{0};
  std::atomic<int> failures{0};
  const auto note = [&callbacks, &failures](bool ok) {
    ++callbacks;
    if (!ok)
      ++failures;
  };

  for (uint64_t i = 0; i < 4; ++i)
    m.mgr->write_chunk_or_queue(kStream, kChunk, i, note);

  EXPECT_EQ(callbacks.load(), 1);

  m.mgr->on_stream_cancel(kStream);

  EXPECT_EQ(callbacks.load(), 4);
  EXPECT_EQ(failures.load(), 3);
}

// Parking is bounded.  A consumer that never grants credits would otherwise
// buy the producer an unbounded heap queue — every write copied and kept
// forever, with its caller suspended on a callback that cannot fire.
TEST(StreamBackpressure, ParkedWritesAreBounded)
{
  BareManager m;
  m.mgr->set_send_callback([](flat_buffer&&) { return true; });

  constexpr uint64_t kStream = 0x54;
  m.start(kStream);
  m.mgr->register_external_writer(kStream, /*initial_credits=*/1);

  std::atomic<int> callbacks{0};
  std::atomic<int> failures{0};
  const auto note = [&callbacks, &failures](bool ok) {
    ++callbacks;
    if (!ok)
      ++failures;
  };

  constexpr size_t kOverflow = 64;
  const size_t attempts = impl::StreamManager::kMaxParkedWrites + 1 + kOverflow;
  for (uint64_t i = 0; i < attempts; ++i)
    m.mgr->write_chunk_or_queue(kStream, kChunk, i, note);

  // One went out on the initial credit, kMaxParkedWrites parked, and
  // everything past that was refused straight away rather than queued —
  // which is the only outcome that keeps the queue's memory bounded.
  EXPECT_EQ(failures.load(), static_cast<int>(kOverflow));
  EXPECT_EQ(callbacks.load(), static_cast<int>(kOverflow) + 1);

  // Teardown still owes every parked caller an answer.
  m.mgr->cancel_all();
  EXPECT_EQ(callbacks.load(), static_cast<int>(attempts));
}

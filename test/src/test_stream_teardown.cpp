// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT
//
// What happens to a server-side stream handler when its client dies.
//
// A handler for a bidirectional stream is a coroutine that spends its whole
// life suspended on `co_await reader`, and the code after that await is where
// the service undoes what accepting the stream did: unsubscribe, release the
// resource the stream was a lease on, tell the rest of the process.  Nothing
// else runs it.  So "the session went away" has to reach that coroutine as a
// resumption — an error it can catch — and not as a frame that is simply
// deallocated underneath it, because destroying a suspended coroutine runs
// destructors and skips every catch block and every statement after the await.
//
// The concrete failure this was written for: a compositor whose client is
// killed keeps the window on screen forever, because the handler that would
// have destroyed it was thrown away mid-await while its `catch` still held the
// only call to `destroySurface`.

#include <gtest/gtest.h>

#include <nprpc/impl/stream_manager.hpp>
#include <nprpc/session_context.h>
#include <nprpc/stream_base.hpp>
#include <nprpc/stream_reader.hpp>
#include <nprpc/task.hpp>

#include <boost/asio/io_context.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

using namespace nprpc;
using namespace nprpc::impl;
using namespace std::chrono_literals;

namespace {

// A StreamManager with no Session behind it — see test_stream_backpressure.cpp
// for why a null self_cell is the right stand-in for a live session.
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

  // Runs whatever cancel_all posted (the reader resumptions) to completion.
  void drain()
  {
    ioc.restart();
    ioc.poll();
  }
};

// What every stream handler in this codebase looks like: read until the stream
// ends, and clean up on the way out however it ended.
struct Trace {
  std::atomic<bool> cleaned{false};
  std::atomic<bool> threw{false};
};

Task<> handler(SessionContext& ctx, uint64_t stream_id, Trace& trace)
{
  StreamReader<uint32_t> reader(ctx, stream_id);
  try {
    while (auto chunk = co_await reader) {
      (void)chunk;
    }
  } catch (...) {
    trace.threw = true;
    trace.cleaned = true;
    co_return;
  }
  trace.cleaned = true;
}

// Runs `work` on a thread of its own and says whether it finished in time.
//
// Detached rather than joined, and the flag is a shared_ptr rather than a
// capture, because the case being tested for is precisely the one where the
// thread never finishes: joining it — which is what a `std::future` from
// `std::async` does in its destructor — would turn a failed assertion into a
// test run that hangs, and hangs say nothing.
[[nodiscard]] bool finishes_within(std::chrono::milliseconds limit,
                                   std::function<void()> work)
{
  struct Signal {
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
  };
  auto signal = std::make_shared<Signal>();

  std::thread([signal, work = std::move(work)] {
    work();
    std::lock_guard lock(signal->mutex);
    signal->done = true;
    signal->cv.notify_all();
  }).detach();

  std::unique_lock lock(signal->mutex);
  return signal->cv.wait_for(lock, limit, [&] { return signal->done; });
}

} // namespace

// The whole bug in one test: a handler suspended on its reader when the
// session dies must be resumed, not deallocated.
TEST(StreamTeardown, DeadSessionResumesASuspendedHandler)
{
  BareManager m;
  Trace trace;

  auto task = handler(m.ctx, 1, trace);
  ASSERT_FALSE(task.done()) << "handler should be parked on co_await reader";
  m.mgr->start_task_after_reply(1, std::move(task));
  m.mgr->on_reply_sent();
  m.drain();
  ASSERT_FALSE(trace.cleaned) << "nothing has ended the stream yet";

  // What SharedMemoryServerSession::on_peer_dead() does when the client's
  // process is found to be gone.
  ASSERT_TRUE(finishes_within(5s, [&m] { m.mgr->cancel_all(); }))
      << "cancel_all deadlocked: a reader destroyed under the manager's own "
         "lock re-enters it through unregister_reader";
  m.drain();

  EXPECT_TRUE(trace.threw) << "the handler should see the death as an error";
  EXPECT_TRUE(trace.cleaned)
      << "the code after the await never ran — a stream handler's cleanup was "
         "skipped, which is a leaked subscription in every service using one";
}

// The same handler, ended the way a well-behaved client ends it. Here the
// coroutine falls out of the loop rather than catching, and this is the path
// that already worked — it is here so that a fix for the one above cannot be
// "resume everything with an error".
TEST(StreamTeardown, CompletionEndsAHandlerWithoutAnError)
{
  BareManager m;
  Trace trace;

  auto task = handler(m.ctx, 2, trace);
  m.mgr->start_task_after_reply(2, std::move(task));
  m.mgr->on_reply_sent();
  m.drain();

  // The sentinel for a stream that ended without carrying a chunk, which is
  // what a client that closes its half straight away sends. A real sequence
  // number here would mean "complete once you have caught up to it", and this
  // reader has nothing to catch up to.
  m.mgr->on_stream_complete(2, kEmptyStreamFinalSequence);
  m.drain();

  EXPECT_TRUE(trace.cleaned);
  EXPECT_FALSE(trace.threw);
}

// Two handlers on one session: killing the client must reach both, and the
// first one's teardown must not swallow the second's.
TEST(StreamTeardown, EveryHandlerOnADeadSessionIsResumed)
{
  BareManager m;
  Trace first, second;

  auto a = handler(m.ctx, 10, first);
  auto b = handler(m.ctx, 11, second);
  m.mgr->start_task_after_reply(10, std::move(a));
  m.mgr->start_task_after_reply(11, std::move(b));
  m.mgr->on_reply_sent();
  m.drain();

  ASSERT_TRUE(finishes_within(5s, [&m] { m.mgr->cancel_all(); }));
  m.drain();

  EXPECT_TRUE(first.cleaned);
  EXPECT_TRUE(second.cleaned);
}

// cancel_all() is called by ~StreamManager as well as by a dying session, and
// a manager that is being destroyed cannot resume anything — there will be no
// executor turn after this point. It must still not hang, and it must not
// leave the coroutine frames it owns undestroyed.
TEST(StreamTeardown, DestroyingTheManagerIsNotADeadlock)
{
  auto m = std::make_unique<BareManager>();
  Trace trace;

  auto task = handler(m->ctx, 3, trace);
  m->mgr->start_task_after_reply(3, std::move(task));
  m->mgr->on_reply_sent();
  m->drain();

  ASSERT_TRUE(finishes_within(5s, [&m] { m->mgr.reset(); }))
      << "~StreamManager deadlocked";
}

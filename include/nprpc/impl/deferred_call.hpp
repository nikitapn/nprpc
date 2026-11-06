// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <semaphore>
#include <stop_token>

#include <chrono>

#include <nprpc/export.hpp>
#include <nprpc/flat_buffer.hpp>
#include <nprpc/session_context.h>
#include <nprpc/task.hpp>

namespace nprpc::impl {

class Session;

// A `[coroutine]` call: owns everything the call needs after dispatch returns and
// the session has moved on, and sends the reply when it finishes.
class DeferredCall
{
public:
  // A context of the call's own, so generated marshalling code can use it like a
  // session's. It has no shared memory channel: replies are built on the heap.
  SessionContext ctx;
  flat_buffer rx, tx;
  std::shared_ptr<const detail::ResumeTarget> resume_target;

  // Sends the reply in `tx`, or an error if none was built. Runs once.
  NPRPC_API void finish();
  // What to answer for an exception that escaped the servant.
  NPRPC_API void fail(std::exception_ptr error);

  std::stop_token stop_token() const noexcept { return stop_.get_token(); }

  // Runs `on_stop` if the client cancels. Destroyed with the call.
  NPRPC_API void on_stop(std::function<void()> on_stop);

  // Takes the request out of the session's current dispatch and registers the
  // call. Null if this session has too many in flight; the client has been
  // answered then.
  NPRPC_API static std::shared_ptr<DeferredCall> begin(SessionContext& session_ctx);

  // Internal.
  explicit DeferredCall(std::shared_ptr<Session> session);
  ~DeferredCall();

private:
  friend class Session;

  std::shared_ptr<Session> session_;
  uint32_t id_ = 0;
  size_t received_ = 0;
  std::chrono::steady_clock::time_point started_;
  std::stop_source stop_;
  std::optional<std::stop_callback<std::function<void()>>> on_stop_;
  bool finished_ = false;
  // For a transport that has to reply before it returns.
  std::binary_semaphore done_{0};
};

// Runs a `[coroutine]` servant method: what generated dispatch calls.
NPRPC_API void start_deferred_call(
    SessionContext& session_ctx,
    std::function<Task<>(SessionContext&, std::stop_token)> body);

} // namespace nprpc::impl

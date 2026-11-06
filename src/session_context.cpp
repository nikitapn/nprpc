// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include <nprpc/impl/session.hpp>
#include <nprpc/session_context.h>

namespace nprpc {

static thread_local SessionContext* ctx;

/** Get the current session context.
 * This is used by the object servant to access the session context
 * and the remote endpoint information.
 * Accessing this context is only valid when handling a request
 * in the object servant, i.e., between `set_context()` and `reset_context()`.
 */
NPRPC_API SessionContext& get_context()
{
  if (ctx == nullptr) {
    throw nprpc::Exception("Session context is not set");
  }
  return (*ctx);
}

namespace detail {
NPRPC_API std::shared_ptr<const ResumeTarget>& current_resume_target() noexcept
{
  thread_local std::shared_ptr<const ResumeTarget> current;
  return current;
}

NPRPC_API void resume_on_target(std::shared_ptr<const ResumeTarget> target,
                                std::coroutine_handle<> handle) noexcept
{
  struct Resume {
    std::shared_ptr<const ResumeTarget> target;
    std::coroutine_handle<> handle;
    static void run(void* arg)
    {
      std::unique_ptr<Resume> self(static_cast<Resume*>(arg));
      auto& current = current_resume_target();
      auto previous = std::exchange(current, self->target);
      self->handle.resume();
      current = std::move(previous);
    }
  };
  const auto* raw = target.get();
  auto* resume = new Resume{std::move(target), handle};
  if (raw->is_running_on && raw->is_running_on(raw->ctx))
    Resume::run(resume);
  else
    raw->post(raw->ctx, &Resume::run, resume);
}
} // namespace detail

NPRPC_API const CallContext& get_call_context() { return get_context().call; }

/** Set the current session context.
 * This is called before calling `dispatch()` on the object servant
 * so that the servant can access the session context
 * and the remote endpoint information.
 */
void set_context(impl::Session& session) { ctx = &session.ctx(); }

/** Reset the current session context.
 * This is called after calling `dispatch()` on the object servant.
 */
void reset_context() { ctx = nullptr; }

} // namespace nprpc
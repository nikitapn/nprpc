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
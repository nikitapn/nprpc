// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT
//
// Adapter bridging nprpc::Task<T> into Boost.Asio coroutine machinery.
//
// nprpc::Task<T> uses its own coroutine promise (not Asio's awaitable<>), so
// it cannot be directly co_await-ed inside boost::asio::awaitable<> or passed
// to boost::asio::co_spawn.  This header provides two utilities:
//
//   nprpc::as_awaitable(task)
//     Returns a Boost.Asio awaitable wrapping the task.  Use inside
//     boost::asio::awaitable<> coroutines:
//
//       co_await nprpc::as_awaitable(obj->SomeRpcAsync());
//
//   nprpc::spawn_task(ioc, factory)
//     Posts a fire-and-forget boost::asio::awaitable<void> factory onto the
//     io_context via co_spawn.
//
//   nprpc::spawn_task(nprpc::get_io_context(),
//           [obj]() -> boost::asio::awaitable<void> {
//               co_await nprpc::as_awaitable(obj->SomeRpcAsync());
//           });

#pragma once

#include <memory>
#include <nprpc/task.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <exception>

namespace nprpc {

// ---------------------------------------------------------------------------
// as_awaitable — bridges nprpc::Task<T> into boost::asio::awaitable<void>
// ---------------------------------------------------------------------------

template<typename T>
auto as_awaitable(Task<T> task) {
  return boost::asio::async_initiate<
      const boost::asio::use_awaitable_t<>&,
      void(std::exception_ptr)
  >(
    // Wrap in unique_ptr so `task->set_completion_handler(arg)` is safe:
    // C++17 sequences the postfix expression (task->) before argument
    // evaluation, giving us a valid raw pointer before the unique_ptr is
    // moved into the completion handler lambda.  Without this, the argument
    // [task = std::move(task)] fires first, making task moved-from before
    // set_completion_handler is called — which silently drops the handler
    // and causes the coroutine frame to be prematurely destroyed.
    [task = std::make_unique<Task<T>>(std::move(task))](auto handler) mutable {
      auto ex = boost::asio::get_associated_executor(handler);
      task->set_completion_handler(
        [task = std::move(task), handler = std::move(handler), ex](std::exception_ptr ep) mutable {
          boost::asio::post(ex, [task = std::move(task), handler = std::move(handler), ep]() mutable {
            std::move(handler)(ep);
          });
        }
      );
    },
    boost::asio::use_awaitable
  );
}

// ---------------------------------------------------------------------------
// spawn_task — fire-and-forget a boost::asio::awaitable<void> on an ioc
// ---------------------------------------------------------------------------
//
// The factory must return boost::asio::awaitable<void>.  Use as_awaitable()
// inside it to await nprpc::Task results:
//
//   nprpc::spawn_task(nprpc::get_io_context(),
//       [obj]() -> boost::asio::awaitable<void> {
//           co_await nprpc::as_awaitable(obj->SetValueAsync(42));
//       });
//
// IMPORTANT: lambda coroutine lifetime.
//
// In C++, a lambda's operator() is a coroutine whose captures live in the
// *lambda object*, not in the coroutine frame.  The coroutine frame only
// stores a `this` pointer back to the lambda.  If the lambda is a local in
// spawn_task and spawn_task returns while the coroutine is still suspended,
// the `this` pointer dangles — any access to a capture is UB, and ObjectPtr
// destructors fire immediately, sending spurious ReleaseObject RPCs.
//
// The fix: call factory() from inside a *named* coroutine (spawn_task_impl)
// that takes the factory *by value*.  Value parameters of named coroutines
// ARE moved into the coroutine frame (C++20 [dcl.fct.def.coroutine]).
// spawn_task_impl suspends while the inner coroutine runs, so the factory
// (and its captures) stays alive in the heap frame for the full duration.

namespace detail {
template<typename Factory>
boost::asio::awaitable<void> spawn_task_impl(Factory factory) {
  co_await factory();
}
} // namespace detail

template<typename Factory>
void spawn_task(boost::asio::io_context& ioc, Factory factory) {
  boost::asio::co_spawn(ioc,
      detail::spawn_task_impl(std::move(factory)),
      boost::asio::detached);
}

} // namespace nprpc

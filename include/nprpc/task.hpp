// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT
//
// nprpc::Task<T> — a minimal, Asio-agnostic coroutine task type.
//
// Design goals:
//   - Zero dependency on Boost or any specific executor library.
//   - Usable as a coroutine return type: Task<T> fn() { co_return value; }
//   - Awaitable: co_await task; inside another coroutine.
//   - Blocking bridge: task.get(); for non-coroutine callers.
//   - Move-only, destructor destroys the coroutine frame cleanly.
//
// Lifetime:
//   final_suspend returns a custom awaiter that (a) unblocks any blocking
//   get() caller via a binary_semaphore, (b) resumes a registered continuation
//   coroutine if one was set by co_await, and (c) suspends the coroutine
//   (so the frame is kept alive until Task::~Task destroys it).

#pragma once

#include <nprpc/export.hpp>

#include <atomic>
#include <coroutine>
#include <exception>
#include <memory>
#include <type_traits>
#include <functional>
#include <semaphore>
#include <stdexcept>
#include <utility>
#include <variant>

namespace nprpc {

namespace detail {

// ---------------------------------------------------------------------------
// Result storage — specialised for void vs value types.
// ---------------------------------------------------------------------------

template<typename T>
struct TaskResult {
  std::variant<std::monostate, T, std::exception_ptr> storage;

  template<typename U>
  void set_value(U&& v) { storage.template emplace<1>(std::forward<U>(v)); }
  void set_exception(std::exception_ptr ep) { storage.template emplace<2>(std::move(ep)); }

  T get() {
    if (auto* ep = std::get_if<2>(&storage))
      std::rethrow_exception(*ep);
    return std::move(std::get<1>(storage));
  }

  std::exception_ptr get_exception() const {
    if (auto* ep = std::get_if<2>(&storage))
      return *ep;
    return {};
  }
};

template<>
struct TaskResult<void> {
  std::exception_ptr exception;

  void set_value() noexcept {}
  void set_exception(std::exception_ptr ep) noexcept { exception = std::move(ep); }

  void get() {
    if (exception) std::rethrow_exception(exception);
  }

  std::exception_ptr get_exception() const noexcept {
    return exception;
  }
};

// ---------------------------------------------------------------------------
// promise_type base — holds result + semaphore + continuation.
// ---------------------------------------------------------------------------

// Where a `[coroutine]` servant has to keep running: its POA's executor. Tasks
// started while one is current inherit it, and return to it after every co_await.
struct ResumeTarget {
  void (*post)(void* ctx, void (*fn)(void*), void* arg);
  // Optional; lets a resume already on the right thread skip the post.
  bool (*is_running_on)(void* ctx);
  void* ctx;
  std::shared_ptr<void> owner;
};

NPRPC_API std::shared_ptr<const ResumeTarget>& current_resume_target() noexcept;
NPRPC_API void resume_on_target(std::shared_ptr<const ResumeTarget> target,
                                std::coroutine_handle<> handle) noexcept;

// Passed to an awaited operation in place of the coroutine that awaits it.
struct ResumeHop {
  struct promise_type {
    ResumeHop get_return_object() {
      return {std::coroutine_handle<promise_type>::from_promise(*this)};
    }
    std::suspend_always initial_suspend() noexcept { return {}; }
    std::suspend_never final_suspend() noexcept { return {}; }
    void return_void() noexcept {}
    void unhandled_exception() noexcept { std::terminate(); }
  };
  std::coroutine_handle<promise_type> handle;
};

inline ResumeHop make_resume_hop(std::shared_ptr<const ResumeTarget> target,
                                 std::coroutine_handle<> awaiting) {
  resume_on_target(std::move(target), awaiting);
  co_return;
}

template<typename A>
concept HasAwaitMembers = requires(A& a) { a.await_ready(); };

template<typename A>
concept HasMemberCoAwait = requires(A&& a) { std::forward<A>(a).operator co_await(); };

template<typename A>
concept HasFreeCoAwait = requires(A&& a) { operator co_await(std::forward<A>(a)); };

template<typename A>
concept HasCoAwait = HasMemberCoAwait<A> || HasFreeCoAwait<A>;

// What the language would await for a type with operator co_await.
template<HasCoAwait A>
decltype(auto) get_awaiter(A&& awaitable) {
  if constexpr (HasMemberCoAwait<A>)
    return std::forward<A>(awaitable).operator co_await();
  else
    return operator co_await(std::forward<A>(awaitable));
}

// Aw is a reference to an awaiter that outlives this one, or an awaiter it owns.
template<typename Aw>
struct ResumingAwaiter {
  Aw awaitable;
  const std::shared_ptr<const ResumeTarget>& target;

  bool await_ready() { return awaitable.await_ready(); }

  bool await_suspend(std::coroutine_handle<> awaiting) {
    std::coroutine_handle<> next = awaiting;
    if (target)
      next = make_resume_hop(target, awaiting).handle;
    using Result = decltype(awaitable.await_suspend(next));
    if constexpr (std::is_void_v<Result>) {
      awaitable.await_suspend(next);
      return true;
    } else if constexpr (std::is_same_v<Result, bool>) {
      const bool suspended = awaitable.await_suspend(next);
      if (!suspended && target)
        next.destroy();
      return suspended;
    } else {
      awaitable.await_suspend(next).resume();
      return true;
    }
  }

  decltype(auto) await_resume() { return awaitable.await_resume(); }
};

template<typename T>
struct TaskPromiseBase {
  std::shared_ptr<const ResumeTarget> resume_target = current_resume_target();

  template<HasCoAwait A>
  auto await_transform(A&& awaitable) {
    using Awaiter = decltype(get_awaiter(std::forward<A>(awaitable)));
    return ResumingAwaiter<Awaiter>{get_awaiter(std::forward<A>(awaitable)), resume_target};
  }
  template<HasAwaitMembers A> requires (!HasCoAwait<A>)
  ResumingAwaiter<A&&> await_transform(A&& awaitable) noexcept {
    return {std::forward<A>(awaitable), resume_target};
  }

  TaskResult<T>           result;
  // Who is waiting. A task starts eagerly, so it can finish on another thread
  // while its caller is still getting to co_await or get(), and whichever of the
  // two comes second has to find out from this.
  std::atomic<void*>      waiter{nullptr};
  std::binary_semaphore   ready{0};      // released at end for a blocked get()
  std::move_only_function<void(std::exception_ptr)> completion_handler;

  // Called by final_suspend's awaiter after the coroutine body finishes.
  // Nothing may touch this promise after the waiter hears: it's free to destroy
  // the task.
  void on_final() noexcept {
    if (completion_handler)
      completion_handler(result.get_exception());
    void* const previous = waiter.exchange(finished(), std::memory_order_acq_rel);
    if (previous == blocked_in_get())
      ready.release();
    else if (previous)
      std::coroutine_handle<>::from_address(previous).resume();
  }

  // Markers no coroutine frame can be at: addresses inside this promise.
  void* finished() noexcept { return static_cast<void*>(&result); }
  void* blocked_in_get() noexcept { return static_cast<void*>(&ready); }

  struct FinalAwaiter {
    bool await_ready() noexcept { return false; }
    template<typename P>
    void await_suspend(std::coroutine_handle<P> h) noexcept {
      h.promise().on_final();
      // Returns void -> coroutine stays at final suspension point.
      // Frame is kept alive until Task::~Task calls handle_.destroy().
    }
    void await_resume() noexcept {}
  };

  // return_value / return_void are injected by derived specialisations.
  void unhandled_exception() {
    result.set_exception(std::current_exception());
  }
};

} // namespace detail

// ---------------------------------------------------------------------------
// Task<T>
// ---------------------------------------------------------------------------

/// A coroutine result: `Task<T> f() { co_return value; }`.
///
/// Starts running as soon as it is called (there is no initial suspension)
/// and needs no executor. Wait for it with `co_await task` inside another
/// coroutine, or `task.get()` from ordinary code. Generated proxies return
/// one from each `<method>Async`, and servants return one from streaming
/// methods.
///
/// Move-only; destroying a Task destroys its coroutine frame, so keep it
/// alive until it is done.
template<typename T = void>
class Task {
public:
  /// Coroutine machinery; not called directly.
  struct promise_type : detail::TaskPromiseBase<T> {
    Task get_return_object() noexcept {
      return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
    }
    std::suspend_never initial_suspend() noexcept { return {}; }
    typename detail::TaskPromiseBase<T>::FinalAwaiter final_suspend() noexcept { return {}; }

    // return_value for non-void T
    template<typename U = T, std::enable_if_t<!std::is_void_v<U>, int> = 0>
    void return_value(U&& v) { this->result.set_value(std::forward<U>(v)); }
  };

  /// The coroutine handle type.
  using handle_t = std::coroutine_handle<promise_type>;

  // --- Awaitable interface (used inside other coroutines) ------------------

  /// `co_await` support: whether the task has already finished.
  bool await_ready() noexcept { return false; }

  /// `co_await` support: resumes `h` when the task finishes, or carries straight
  /// on if it already has.
  bool await_suspend(std::coroutine_handle<> h) noexcept {
    void* expected = nullptr;
    return handle_.promise().waiter.compare_exchange_strong(
        expected, h.address(), std::memory_order_acq_rel, std::memory_order_acquire);
  }

  /// `co_await` support: the result, or the exception the task threw.
  decltype(auto) await_resume() { return handle_.promise().result.get(); }

  // --- Blocking bridge (non-coroutine callers) -----------------------------

  /// Blocks the calling thread until the task finishes, then returns its
  /// result or rethrows its exception. Do not call it on the thread the
  /// task needs in order to finish.
  decltype(auto) get() {
    auto& promise = handle_.promise();
    void* expected = nullptr;
    if (promise.waiter.compare_exchange_strong(expected, promise.blocked_in_get(),
                                               std::memory_order_acq_rel,
                                               std::memory_order_acquire))
      promise.ready.acquire();
    return handle_.promise().result.get();
  }

  /// Whether the task has finished (or holds no coroutine).
  bool done() const noexcept {
    return !handle_ || handle_.done();
  }

  /// Rethrows the task's exception if it finished with one.
  void rethrow_if_exception() const {
    if (!handle_)
      return;
    if (auto ep = handle_.promise().result.get_exception())
      std::rethrow_exception(ep);
  }

  /// Calls `handler` when the task finishes, with its exception or null.
  /// For fire-and-forget tasks that still need to report failure.
  void set_completion_handler(std::move_only_function<void(std::exception_ptr)> handler) {
    if (!handle_)
      return;
    handle_.promise().completion_handler = std::move(handler);
  }

  // --- Lifecycle -----------------------------------------------------------

  /// An empty task, holding no coroutine.
  Task() noexcept : handle_{} {}
  /// Adopts a coroutine; used by `promise_type`.
  explicit Task(handle_t h) noexcept : handle_(h) {}
  /// Takes `o`'s coroutine, leaving `o` empty.
  Task(Task&& o) noexcept : handle_(std::exchange(o.handle_, {})) {}
  /// Destroys the current coroutine and takes `o`'s.
  Task& operator=(Task&& o) noexcept {
    if (this != &o) {
      if (handle_) handle_.destroy();
      handle_ = std::exchange(o.handle_, {});
    }
    return *this;
  }
  Task(const Task&) = delete;
  Task& operator=(const Task&) = delete;

  ~Task() { if (handle_) handle_.destroy(); }

  /// Whether the task holds a coroutine.
  bool valid() const noexcept { return static_cast<bool>(handle_); }

private:
  handle_t handle_;
};

// ---------------------------------------------------------------------------
// Task<void> specialisation — promise uses return_void instead of return_value.
// ---------------------------------------------------------------------------

template<>
struct Task<void>::promise_type : detail::TaskPromiseBase<void> {
  Task<void> get_return_object() noexcept {
    return Task<void>{std::coroutine_handle<promise_type>::from_promise(*this)};
  }
  std::suspend_never initial_suspend() noexcept { return {}; }
  detail::TaskPromiseBase<void>::FinalAwaiter final_suspend() noexcept { return {}; }
  void return_void() noexcept { result.set_value(); }
};

} // namespace nprpc

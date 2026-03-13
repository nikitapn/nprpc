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

#include <coroutine>
#include <exception>
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

template<typename T>
struct TaskPromiseBase {
  TaskResult<T>           result;
  std::coroutine_handle<> continuation;  // set by co_await; resumed at end
  std::binary_semaphore   ready{0};      // released at end; used by get()
  std::function<void(std::exception_ptr)> completion_handler;

  // Called by final_suspend's awaiter after the coroutine body finishes.
  void on_final() noexcept {
    ready.release();                     // unblock any blocking get()
    if (completion_handler)
      completion_handler(result.get_exception());
    if (continuation) continuation.resume();
  }

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

template<typename T = void>
class Task {
public:
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

  using handle_t = std::coroutine_handle<promise_type>;

  // --- Awaitable interface (used inside other coroutines) ------------------

  bool await_ready() noexcept { return handle_.done(); }

  void await_suspend(std::coroutine_handle<> h) noexcept {
    handle_.promise().continuation = h;
    // If the task already finished (race), resume immediately.
    if (handle_.done()) h.resume();
  }

  decltype(auto) await_resume() { return handle_.promise().result.get(); }

  // --- Blocking bridge (non-coroutine callers) -----------------------------

  decltype(auto) get() {
    handle_.promise().ready.acquire();
    return handle_.promise().result.get();
  }

  bool done() const noexcept {
    return !handle_ || handle_.done();
  }

  void rethrow_if_exception() const {
    if (!handle_)
      return;
    if (auto ep = handle_.promise().result.get_exception())
      std::rethrow_exception(ep);
  }

  void set_completion_handler(std::function<void(std::exception_ptr)> handler) {
    if (!handle_)
      return;
    handle_.promise().completion_handler = std::move(handler);
  }

  // --- Lifecycle -----------------------------------------------------------

  Task() noexcept : handle_{} {}
  explicit Task(handle_t h) noexcept : handle_(h) {}
  Task(Task&& o) noexcept : handle_(std::exchange(o.handle_, {})) {}
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

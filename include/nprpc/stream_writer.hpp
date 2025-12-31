// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <coroutine>
#include <exception>
#include <mutex>
#include <optional>
#include <variant>

#include <nprpc/stream_base.hpp>
#include <nprpc/impl/stream_manager.hpp>

namespace nprpc {

// Typed stream writer with coroutine support
template <typename T>
class StreamWriter : public StreamWriterBase
{
public:
  struct promise_type;
  using handle_type = std::coroutine_handle<promise_type>;

  struct promise_type {
    T current_value_;
    bool has_value_ = false;
    bool completed_ = false;
    std::exception_ptr exception_;
    uint64_t stream_id_ = 0;
    impl::StreamManager* manager_ = nullptr;
    // Backpressure
    bool waiting_for_capacity_ = false;
    std::coroutine_handle<> continuation_;

    StreamWriter<T> get_return_object()
    {
      return StreamWriter<T>(handle_type::from_promise(*this));
    }

    std::suspend_always initial_suspend() noexcept { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }

    void return_void() { completed_ = true; }

    std::suspend_always yield_value(T&& value)
    {
      current_value_ = std::move(value);
      has_value_ = true;
      return {};
    }

    std::suspend_always yield_value(const T& value)
    {
      current_value_ = value;
      has_value_ = true;
      return {};
    }

    void unhandled_exception() { exception_ = std::current_exception(); }
  };

  explicit StreamWriter(handle_type h)
      : coro_(h)
  {
  }

  StreamWriter(StreamWriter&& other) noexcept
      : coro_(std::exchange(other.coro_, {}))
  {
  }

  ~StreamWriter() override
  {
    if (coro_)
      coro_.destroy();
  }

  // Resume execution (called by StreamManager when ready to send)
  void resume() override
  {
    if (coro_ && !coro_.done()) {
      coro_.resume();

      if (coro_.done()) {
        // Stream finished naturally
        if (coro_.promise().exception_) {
          // TODO: how to get exception data?
          // For now just generic error
          if (coro_.promise().manager_) {
            coro_.promise().manager_->send_error(coro_.promise().stream_id_, 1,
                                                 {});
          }
        } else {
          if (coro_.promise().manager_) {
            // TODO: track sequence number
            coro_.promise().manager_->send_complete(coro_.promise().stream_id_,
                                                    0);
          }
        }
      } else if (coro_.promise().has_value_) {
        // Yielded a value, send it
        if (coro_.promise().manager_) {
          // Serialize T to bytes depending on type
          if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
            // Vector of bytes - send directly
            coro_.promise().manager_->send_chunk(coro_.promise().stream_id_,
                                                 coro_.promise().current_value_,
                                                 0 // sequence
            );
          } else if constexpr (std::is_trivially_copyable_v<T>) {
            // Scalar or POD type - send as raw bytes
            const auto* data_ptr = reinterpret_cast<const uint8_t*>(&coro_.promise().current_value_);
            coro_.promise().manager_->send_chunk(coro_.promise().stream_id_,
                                                 std::span<const uint8_t>(data_ptr, sizeof(T)),
                                                 0 // sequence
            );
          } else {
            // TODO: Handle complex types via serialization
            static_assert(std::is_trivially_copyable_v<T>, 
                "StreamWriter<T>: T must be trivially copyable or std::vector<uint8_t> for now");
          }

          coro_.promise().has_value_ = false;
          // Resume again to continue (unless we want to yield per chunk)
          // If we yield per chunk, we should resume?
          // The coroutine is suspended at yield_value.
          // We just consumed the value.
          // We should NOT resume immediately if we want to give control back to
          // event loop? But usually we want to pump as fast as possible until
          // blocked. For now, let's just return and let StreamManager call
          // resume again? StreamManager doesn't have a loop. So we should
          // probably resume again immediately? But recursion risk? Better to
          // loop here.
          resume();
        }
      }
    }
  }

  bool is_done() const override { return !coro_ || coro_.done(); }

  void cancel() override
  {
    if (coro_ && !coro_.done()) {
      coro_.promise().completed_ = true;
      coro_.destroy();
      coro_ = {};
    }
  }

  void set_manager(impl::StreamManager* manager, uint64_t stream_id)
  {
    if (coro_) {
      coro_.promise().manager_ = manager;
      coro_.promise().stream_id_ = stream_id;
    }
  }

private:
  handle_type coro_;
};

} // namespace nprpc

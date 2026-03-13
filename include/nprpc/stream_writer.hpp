// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <coroutine>
#include <exception>
#include <mutex>
#include <optional>
#include <utility>
#include <variant>

#include <nprpc/session_context.h>
#include <nprpc/stream_base.hpp>
#include <nprpc/impl/stream_manager.hpp>

// Primary template for stream chunk serialization.
// npidl generates explicit specializations (inline, in the generated header)
// for each non-fundamental struct type used as a stream element.
namespace nprpc_stream {
template <typename T>
::nprpc::flat_buffer serialize(const T& value);
} // namespace nprpc_stream

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

  StreamWriter(SessionContext& session, uint64_t stream_id)
    : client_manager_(session.stream_manager)
    , client_stream_id_(stream_id)
    , client_closed_(false)
  {
  }

  StreamWriter(StreamWriter&& other) noexcept
      : coro_(std::exchange(other.coro_, {}))
      , client_manager_(std::exchange(other.client_manager_, nullptr))
      , client_stream_id_(std::exchange(other.client_stream_id_, 0))
      , sequence_(std::exchange(other.sequence_, 0))
      , client_closed_(std::exchange(other.client_closed_, true))
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
    if (!coro_)
      return;

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
            coro_.promise().manager_->send_complete(coro_.promise().stream_id_,
                                                    sequence_ == 0 ? 0 : sequence_ - 1);
          }
        }
      } else if (coro_.promise().has_value_) {
        if (auto* manager = coro_.promise().manager_) {
          send_value(*manager,
                     coro_.promise().stream_id_,
                     coro_.promise().current_value_);
          coro_.promise().has_value_ = false;
          resume();
        }
      }
    }
  }

  bool is_done() const override
  {
    if (coro_)
      return coro_.done();
    return client_closed_;
  }

  void cancel() override
  {
    if (coro_ && !coro_.done()) {
      coro_.promise().completed_ = true;
      coro_.destroy();
      coro_ = {};
      return;
    }

    if (client_manager_ && !client_closed_) {
      client_manager_->send_cancel(client_stream_id_);
      client_closed_ = true;
    }
  }

  void set_manager(impl::StreamManager* manager, uint64_t stream_id)
  {
    if (coro_) {
      coro_.promise().manager_ = manager;
      coro_.promise().stream_id_ = stream_id;
    }
  }

  void write(const T& value)
  {
    if (!client_manager_ || client_closed_)
      return;
    send_value(*client_manager_, client_stream_id_, value);
  }

  void write(T&& value)
  {
    write(static_cast<const T&>(value));
  }

  void close()
  {
    if (!client_manager_ || client_closed_)
      return;
    client_manager_->send_complete(client_stream_id_, sequence_ == 0 ? 0 : sequence_ - 1);
    client_closed_ = true;
  }

  void abort(uint32_t error_code = 1)
  {
    if (!client_manager_ || client_closed_)
      return;
    client_manager_->send_error(client_stream_id_, error_code, {});
    client_closed_ = true;
  }

private:
  handle_type coro_;
  impl::StreamManager* client_manager_ = nullptr;
  uint64_t client_stream_id_ = 0;
  uint64_t sequence_ = 0;
  bool client_closed_ = true;

  void send_value(impl::StreamManager& manager,
                  uint64_t stream_id,
                  const T& value)
  {
    if constexpr (std::is_trivially_copyable_v<T>) {
      const auto* data_ptr = reinterpret_cast<const uint8_t*>(&value);
      manager.send_chunk(stream_id,
                         std::span<const uint8_t>(data_ptr, sizeof(T)),
                         sequence_++);
    } else {
      // All non-trivial stream payloads use the npidl-generated codec so
      // strings, vectors, and structs share one consistent wire format.
      auto __buf = nprpc_stream::serialize<T>(value);
      auto __span = __buf.data();
      manager.send_chunk(
          stream_id,
          std::span<const uint8_t>(
              reinterpret_cast<const uint8_t*>(__span.data()),
              __span.size()),
          sequence_++);
    }
  }
};

} // namespace nprpc

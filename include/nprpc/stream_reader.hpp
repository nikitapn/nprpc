// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <cstring>
#include <condition_variable>
#include <coroutine>
#include <mutex>
#include <optional>
#include <queue>
#include <stdexcept>

#include <nprpc/flat_buffer.hpp>
#include <nprpc/session_context.h>
#include <nprpc/stream_base.hpp>
#include <nprpc/impl/stream_manager.hpp>
#include <nprpc_base.hpp>

namespace nprpc {

template <typename T>
class StreamReader : public StreamReaderBase
{
public:
  StreamReader(SessionContext& session, uint64_t stream_id)
      : session_(session)
      , stream_id_(stream_id)
  {
    // Register with session's stream manager
    if (session_.stream_manager) {
      session_.stream_manager->register_reader(stream_id_, this);
    }
  }

  // Move constructor - re-register with new 'this' pointer
  StreamReader(StreamReader&& other) noexcept
      : session_(other.session_)
      , stream_id_(other.stream_id_)
      , chunks_(std::move(other.chunks_))
      , completed_(other.completed_)
      , cancelled_(other.cancelled_)
      , error_(std::move(other.error_))
      , resume_handle_(other.resume_handle_)
      , window_size_(other.window_size_)
  {
    // Update the registration to point to new 'this'
    if (session_.stream_manager && !cancelled_) {
      session_.stream_manager->register_reader(stream_id_, this);
    }
    // Mark the moved-from object as cancelled so it doesn't unregister
    other.cancelled_ = true;
  }

  // Disable copy
  StreamReader(const StreamReader&) = delete;
  StreamReader& operator=(const StreamReader&) = delete;
  StreamReader& operator=(StreamReader&&) = delete;

  ~StreamReader() override { cancel(); }

  // Async iterator support
  struct iterator {
    StreamReader* reader_;
    std::optional<T> current_;
    bool done_ = false;

    iterator(StreamReader* reader, bool done)
        : reader_(reader)
        , done_(done)
    {
      if (!done_) {
        ++(*this); // Fetch first value
      }
    }

    bool operator!=(const iterator& other) const
    {
      return done_ != other.done_;
    }

    iterator& operator++()
    {
      current_ = reader_->read_next();
      if (!current_) {
        done_ = true;
      }
      return *this;
    }

    T& operator*() { return *current_; }
  };

  iterator begin() { return iterator(this, false); }
  iterator end() { return iterator(this, true); }

  // C++20 coroutine support: for co_await (auto& chunk : reader)
  auto operator co_await()
  {
    struct Awaitable {
      StreamReader* reader;

      bool await_ready() const noexcept { return reader->has_pending_chunk(); }

      void await_suspend(std::coroutine_handle<> h)
      {
        reader->set_resume_handle(h);
      }

      std::optional<T> await_resume() { return reader->read_next(); }
    };
    return Awaitable{this};
  }

  // Blocking read (for non-coroutine usage or fallback)
  std::optional<T> read_next()
  {
    std::unique_lock lock(mutex_);

    // Wait for chunk or completion
    cv_.wait(lock, [this] { return !chunks_.empty() || completed_ || error_; });

    if (error_) {
      std::rethrow_exception(error_);
    }

    if (chunks_.empty()) {
      return std::nullopt; // Stream complete
    }

    auto fb = std::move(chunks_.front());
    chunks_.pop();

    // Update window size and send to server
    window_size_++;
    send_window_update();

    // Deserialize T from the flat buffer
    // The buffer contains: Header + StreamChunk (stream_id, sequence, data, window_size)
    // Use StreamChunk_Direct to access the data vector
    impl::flat::StreamChunk_Direct chunk(fb, sizeof(impl::flat::Header));
    auto data_span = chunk.data();

    if constexpr (std::is_fundamental_v<T>) {
      // For primitive types, read directly from the data vector
      if (data_span.size() >= sizeof(T)) {
        T value;
        std::memcpy(&value, data_span.data(), sizeof(T));
        return value;
      }
      return std::nullopt;
    } else {
      // For complex types, use Direct accessor pattern
      // TODO: implement for complex types - need to return a wrapper
      // that holds the buffer and provides Direct-style access
      static_assert(std::is_fundamental_v<T>, 
          "Only fundamental types supported for streaming currently");
      return std::nullopt;
    }
  }

  // Called by StreamManager when chunk arrives
  void on_chunk_received(flat_buffer fb) override
  {
    {
      std::lock_guard lock(mutex_);
      chunks_.push(std::move(fb));
      window_size_--;
    }

    cv_.notify_one();

    if (resume_handle_) {
      auto h = resume_handle_;
      resume_handle_ = {};
      h.resume();
    }
  }

  void on_complete() override
  {
    {
      std::lock_guard lock(mutex_);
      completed_ = true;
    }

    cv_.notify_one();

    if (resume_handle_) {
      auto h = resume_handle_;
      resume_handle_ = {};
      h.resume();
    }
  }

  void on_error(uint32_t error_code, flat_buffer error_data) override
  {
    {
      std::lock_guard lock(mutex_);
      // TODO: create proper exception from code/data
      error_ = std::make_exception_ptr(
          std::runtime_error("Stream error: " + std::to_string(error_code)));
    }

    cv_.notify_one();

    if (resume_handle_) {
      auto h = resume_handle_;
      resume_handle_ = {};
      h.resume();
    }
  }

  void cancel()
  {
    if (cancelled_)
      return;
    cancelled_ = true;

    // Send cancel message to server
    if (session_.stream_manager) {
      session_.stream_manager->send_cancel(stream_id_);
    }
  }

  bool is_complete() const
  {
    std::lock_guard lock(mutex_);
    return completed_ && chunks_.empty();
  }

private:
  SessionContext& session_;
  uint64_t stream_id_;

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  // Guarded by mutex_
  std::queue<flat_buffer> chunks_;

  bool completed_ = false;
  bool cancelled_ = false;
  std::exception_ptr error_;

  // Backpressure: window-based flow control
  // Guarded by mutex_
  size_t window_size_ = 16; // Number of chunks client can buffer

  // Coroutine support
  std::coroutine_handle<> resume_handle_;

  void send_window_update()
  {
    // TODO: implement window update message
  }

  bool has_pending_chunk() const
  {
    std::lock_guard lock(mutex_);
    return !chunks_.empty() || completed_ || error_;
  }

  void set_resume_handle(std::coroutine_handle<> h)
  {
    resume_handle_ = h;
  }
};

} // namespace nprpc

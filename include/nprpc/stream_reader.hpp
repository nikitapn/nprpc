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
#include <nprpc_base_ext.hpp>

// Primary template for stream chunk deserialization.
// npidl generates explicit specializations (inline, in the generated header)
// for each non-fundamental struct type used as a stream element.
namespace nprpc_stream {
template <typename T>
T deserialize(::nprpc::flat_buffer& buf);
} // namespace nprpc_stream

namespace nprpc {

template <typename T>
class StreamReader : public StreamReaderBase
{
public:
  // producer_window is the credit pool the remote producer is working with:
  // - client-side readers advertise kDefaultReaderWindow via
  //   StreamInit.initial_credits, so stubs pass that value here;
  // - server-side readers (client/bidi uploads) have no advertisement
  //   channel, so the producer sits on the legacy kInitialWindowSize —
  //   the default keeps the grant threshold below that window.
  StreamReader(SessionContext& session, uint64_t stream_id,
               uint32_t producer_window =
                   static_cast<uint32_t>(impl::StreamManager::kInitialWindowSize))
      : session_(session)
      , stream_id_(stream_id)
      , grant_threshold_(producer_window / 2 > 0 ? producer_window / 2 : 1)
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
      , grant_threshold_(other.grant_threshold_)
      , consumed_since_grant_(other.consumed_since_grant_)
  {
    // Update the registration to point to new 'this'
    if (session_.stream_manager && !cancelled_) {
      session_.stream_manager->register_reader(stream_id_, this);
    }
    // Mark the moved-from object as cancelled so it doesn't unregister
    other.cancelled_ = true;
    other.resume_handle_ = {};
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

      bool await_suspend(std::coroutine_handle<> h)
      {
        return reader->arm_resume_handle(h);
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

    // Watermark-batched flow control: grant credits back to the producer
    // once per grant_threshold_ consumed chunks instead of per chunk.
    // Liveness: if the producer ever stalls at zero credits, everything it
    // sent is buffered here, so consumed_since_grant_ must reach the
    // threshold and fire a refill.
    if (++consumed_since_grant_ >= grant_threshold_) {
      send_window_update(consumed_since_grant_);
      consumed_since_grant_ = 0;
    }

    // Deserialize T from the flat buffer
    // The buffer contains: Header + StreamChunk (stream_id, sequence, data)
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
    } else if constexpr (::nprpc::flat::is_owned_direct_v<T>) {
      // For stream<direct Foo>: move the whole chunk flat_buffer into an owning
      // shared_ptr — zero copy. Compute the offset of the payload data within
      // fb before the move (the pointer stays valid until fb is destroyed).
      const auto offset = static_cast<uint32_t>(
          reinterpret_cast<const uint8_t*>(data_span.data()) -
          reinterpret_cast<const uint8_t*>(fb.data().data()));
      auto owned_buf = std::make_shared<flat_buffer>(std::move(fb));
      return T(std::move(owned_buf), offset);
    } else {
      // For all non-fundamental, non-direct stream payloads, call the
      // npidl-generated deserialization free function in namespace
      // nprpc_stream. This keeps string, vector<T>, and struct payloads on a
      // single wire format instead of introducing runtime-only special cases.
      return nprpc_stream::deserialize<T>(fb);
    }
  }

  // Called by StreamManager when chunk arrives
  void on_chunk_received(flat_buffer fb) override
  {
    std::coroutine_handle<> resume_handle;
    {
      std::lock_guard lock(mutex_);
      chunks_.push(std::move(fb));
      resume_handle = std::exchange(resume_handle_, {});
    }

    cv_.notify_one();

    if (resume_handle) {
      if (session_.stream_manager) {
        session_.stream_manager->post([resume_handle]() mutable { resume_handle.resume(); });
      } else {
        resume_handle.resume();
      }
    }
  }

  void on_complete() override
  {
    std::coroutine_handle<> resume_handle;
    {
      std::lock_guard lock(mutex_);
      completed_ = true;
      resume_handle = std::exchange(resume_handle_, {});
    }

    cv_.notify_one();

    if (resume_handle) {
      if (session_.stream_manager) {
        session_.stream_manager->post([resume_handle]() mutable { resume_handle.resume(); });
      } else {
        resume_handle.resume();
      }
    }
  }

  void on_error(uint32_t error_code, flat_buffer error_data) override
  {
    std::coroutine_handle<> resume_handle;
    {
      std::lock_guard lock(mutex_);
      // TODO: create proper exception from code/data
      error_ = std::make_exception_ptr(
          std::runtime_error("Stream error: " + std::to_string(error_code)));
      resume_handle = std::exchange(resume_handle_, {});
    }

    cv_.notify_one();

    if (resume_handle) {
      if (session_.stream_manager) {
        session_.stream_manager->post([resume_handle]() mutable { resume_handle.resume(); });
      } else {
        resume_handle.resume();
      }
    }
  }

  void cancel()
  {
    bool should_send_cancel = false;
    {
      std::lock_guard lock(mutex_);
      if (cancelled_)
        return;
      should_send_cancel = !completed_;
      cancelled_ = true;
    }
    if (session_.stream_manager) {
      session_.stream_manager->unregister_reader(stream_id_, this);
      if (should_send_cancel) {
        session_.stream_manager->send_cancel(stream_id_);
      }
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

  // Backpressure: watermark-batched credit grants (guarded by mutex_).
  // grant_threshold_ = producer_window / 2 — must not exceed the producer's
  // window or the stream deadlocks with the producer out of credits and the
  // consumer under threshold.
  uint32_t grant_threshold_;
  uint32_t consumed_since_grant_ = 0;

  // Coroutine support
  std::coroutine_handle<> resume_handle_;

  void send_window_update(uint32_t credits)
  {
    if (session_.stream_manager) {
      session_.stream_manager->send_window_update(stream_id_, credits);
    }
  }

  bool has_pending_chunk() const
  {
    std::lock_guard lock(mutex_);
    return !chunks_.empty() || completed_ || error_;
  }

  bool arm_resume_handle(std::coroutine_handle<> h)
  {
    std::lock_guard lock(mutex_);
    if (!chunks_.empty() || completed_ || error_) {
      return false;
    }
    resume_handle_ = h;
    return true;
  }
};

} // namespace nprpc

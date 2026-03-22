// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include "logging.hpp"
#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/impl/session.hpp>
#include <nprpc/impl/shared_memory_channel.hpp>
#include <nprpc/impl/stream_manager.hpp>
#include <nprpc/session_context.h>
#include <nprpc/stream_base.hpp>

#include <atomic>
#include <random>

#define NPRPC_ENABLE_STREAM_MANAGER_TRACE 0

#if NPRPC_ENABLE_STREAM_MANAGER_TRACE
# define NPRPC_STREAM_MANAGER_LOG_TRACE(format, ...)              \
  NPRPC_LOG_TRACE(                                                \
    "[StreamManager] {:p} {} " #format __VA_OPT__(, )__VA_ARGS__)
#else
# define NPRPC_STREAM_MANAGER_LOG_TRACE(format, ...) do {} while(0)
#endif

namespace nprpc::impl {

uint64_t StreamManager::generate_stream_id()
{
  static std::atomic<uint64_t> counter{0};
  static const uint64_t random_base = []() {
    std::random_device rd;
    return (static_cast<uint64_t>(rd()) << 32) | rd();
  }();

  return random_base ^ (++counter);
}

StreamManager::StreamManager(SessionContext& session, boost::asio::any_io_executor stream_executor)
    : session_(session), stream_executor_(stream_executor)
{
}

StreamManager::~StreamManager() { cancel_all(); }

bool StreamManager::is_stream_started(uint64_t stream_id) const
{
  return started_streams_.contains(stream_id);
}

void StreamManager::dispatch_buffer(uint64_t stream_id, flat_buffer&& fb)
{
  auto* header = reinterpret_cast<flat::Header*>(fb.data().data());
  if (header->msg_id == MessageId::StreamDataChunk) {
    const bool unreliable = is_stream_unreliable(stream_id);
    if (unreliable && send_datagram_callback_) {
      if (!send_datagram_callback_(std::move(fb))) {
        NPRPC_LOG_WARN("StreamManager::send_chunk: datagram send failed (may be too large)");
      }
    } else if (send_native_stream_callback_) {
      send_native_stream_callback_(std::move(fb));
    } else if (send_callback_) {
      send_callback_(std::move(fb));
    } else {
      NPRPC_LOG_ERROR("StreamManager::dispatch_buffer: no send callback set for stream chunk");
    }
    return;
  }

  if (send_callback_) {
    send_callback_(std::move(fb));
  } else {
    NPRPC_LOG_ERROR("StreamManager::dispatch_buffer: no send callback set");
  }
}

void StreamManager::defer_stream_start(uint64_t stream_id)
{
  std::lock_guard<std::mutex> lock(mutex_);
  pending_stream_starts_.push_back(stream_id);
}

void StreamManager::start_task_after_reply(uint64_t stream_id, ::nprpc::Task<> task)
{
  task.set_completion_handler([this, stream_id](std::exception_ptr ep) {
    this->post([this, stream_id, ep]() mutable {
      ::nprpc::Task<> completed_task;

      if (ep) {
        try {
          std::rethrow_exception(ep);
        } catch (const std::exception& e) {
          NPRPC_LOG_ERROR("StreamManager: async stream task {} failed: {}", stream_id, e.what());
          send_error(stream_id, 1, {});
        } catch (...) {
          NPRPC_LOG_ERROR("StreamManager: async stream task {} failed with unknown exception", stream_id);
          send_error(stream_id, 1, {});
        }
      }

      {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = active_tasks_.find(stream_id);
        if (it != active_tasks_.end()) {
          completed_task = std::move(it->second);
          active_tasks_.erase(it);
        }
      }
    });
  });

  const bool completed_inline = task.done();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    active_tasks_[stream_id] = std::move(task);
    pending_stream_starts_.push_back(stream_id);
  }

  if (completed_inline) {
    post([this, stream_id]() { start_stream(stream_id); });
  }
}

void StreamManager::on_reply_sent()
{
  std::vector<uint64_t> streams;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    streams.swap(pending_stream_starts_);
  }

  for (auto stream_id : streams) {
    post([this, stream_id]() { start_stream(stream_id); });
  }
}

void StreamManager::start_stream(uint64_t stream_id)
{
  StreamWriterBase* raw_writer = nullptr;
  std::vector<flat_buffer> pending;
  bool has_task = false;
  bool task_done = false;
  ::nprpc::Task<> completed_task;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!started_streams_.insert(stream_id).second)
      return;

    if (auto it = pending_messages_.find(stream_id); it != pending_messages_.end()) {
      pending = std::move(it->second);
      pending_messages_.erase(it);
    }

    if (auto writer_it = writers_.find(stream_id); writer_it != writers_.end()) {
      raw_writer = writer_it->second.writer.get();
    }

    if (auto task_it = active_tasks_.find(stream_id); task_it != active_tasks_.end()) {
      has_task = true;
      task_done = task_it->second.done();
    }
  }

  for (auto& fb : pending) {
    dispatch_buffer(stream_id, std::move(fb));
  }

  if (raw_writer) {
    boost::asio::co_spawn(stream_executor_,
      [this, stream_id, raw_writer]() -> boost::asio::awaitable<void> {
        // Per-chunk credit-based flow control.
        // The coroutine parks itself on a steady_timer when credits run out.
        // on_window_update() cancels the timer to wake it back up.
        boost::asio::steady_timer credit_timer(stream_executor_,
            boost::asio::steady_timer::time_point::max());

        while (true) {
          // Grab one credit or register the timer to wait for one.
          bool need_wait = false;
          {
            std::lock_guard lock(mutex_);
            auto it = writers_.find(stream_id);
            if (it == writers_.end()) break; // writer was cancelled

            if (it->second.credits > 0) {
              --it->second.credits;
            } else {
              // Arm the timer and register the pointer while holding the lock.
              // This eliminates the race where on_window_update() arrives and
              // cancels the timer before we co_await it.
              credit_timer.expires_at(
                  boost::asio::steady_timer::time_point::max());
              it->second.credit_timer = &credit_timer;
              need_wait = true;
            }
          }

          if (need_wait) {
            try {
              co_await credit_timer.async_wait(boost::asio::use_awaitable);
            } catch (const boost::system::system_error&) {
              // operation_aborted: on_window_update() cancelled the timer.
            }
            // Loop back to re-check credits; credit_timer pointer was already
            // cleared by on_window_update().
            continue;
          }

          if (raw_writer->is_done()) break;
          raw_writer->resume();
          if (raw_writer->is_done()) break;

          co_await boost::asio::post(stream_executor_, boost::asio::use_awaitable);
        }

        // Tear down: remove timer pointer and erase the writer entry
        {
          std::lock_guard lock(mutex_);
          auto it = writers_.find(stream_id);
          if (it != writers_.end()) {
            it->second.credit_timer = nullptr;
            writers_.erase(it);
          }
        }
      },
      boost::asio::detached);
  }

  if (has_task && task_done) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = active_tasks_.find(stream_id);
    if (it != active_tasks_.end()) {
      completed_task = std::move(it->second);
      active_tasks_.erase(it);
    }
  }
}

void StreamManager::register_stream(uint64_t stream_id,
                                    std::unique_ptr<StreamWriterBase> writer,
                                    bool unreliable)
{
  {
    std::lock_guard<std::mutex> lock(mutex_);
    writers_[stream_id] = StreamInfo{std::move(writer), unreliable};
  }

  NPRPC_STREAM_MANAGER_LOG_TRACE("Stream registered: {} (unreliable={})", stream_id, unreliable);
}

bool StreamManager::is_stream_unreliable(uint64_t stream_id) const
{
  // Note: mutex should be held by caller or this should be called from a safe context
  auto it = writers_.find(stream_id);
  if (it != writers_.end()) {
    return it->second.unreliable;
  }
  return false;
}

void StreamManager::register_reader(uint64_t stream_id,
                                    StreamReaderBase* reader)
{
  std::lock_guard<std::mutex> lock(mutex_);
  auto& state = readers_[stream_id];
  state.reader = reader;
}

void StreamManager::unregister_reader(uint64_t stream_id,
                                      StreamReaderBase* reader)
{
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = readers_.find(stream_id);
  if (it != readers_.end() && it->second.reader == reader) {
    readers_.erase(it);
  }
}

void StreamManager::set_reader_unreliable(uint64_t stream_id, bool unreliable)
{
  std::lock_guard<std::mutex> lock(mutex_);
  auto& state = readers_[stream_id];
  state.unreliable = unreliable;
}

void StreamManager::on_chunk_received(flat_buffer&& fb)
{
  flat::StreamChunk_Direct chunk(fb, sizeof(flat::Header));
  const auto stream_id = chunk.stream_id();
  const auto sequence = chunk.sequence();

  StreamReaderBase* reader = nullptr;
  bool should_complete = false;
  std::vector<flat_buffer> ready_chunks;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = readers_.find(stream_id);
    if (it == readers_.end() || it->second.reader == nullptr) {
      NPRPC_LOG_WARN("Received chunk for unknown stream: {}", stream_id);
      return;
    }

    reader = it->second.reader;
    auto& state = it->second;
    if (state.unreliable) {
      ready_chunks.push_back(std::move(fb));
    } else if (sequence >= state.next_expected_sequence) {
      state.pending_chunks.emplace(sequence, std::move(fb));
      auto ready_it = state.pending_chunks.find(state.next_expected_sequence);
      while (ready_it != state.pending_chunks.end()) {
        ready_chunks.push_back(std::move(ready_it->second));
        state.pending_chunks.erase(ready_it);
        ++state.next_expected_sequence;
        ready_it = state.pending_chunks.find(state.next_expected_sequence);
      }
    }

    if (state.final_sequence && state.next_expected_sequence > *state.final_sequence) {
      should_complete = true;
      readers_.erase(it);
    }
  }

  for (auto& ready_fb : ready_chunks) {
    reader->on_chunk_received(std::move(ready_fb));
  }
  if (should_complete) {
    reader->on_complete();
  }
}

void StreamManager::on_stream_complete(uint64_t stream_id, uint64_t final_sequence)
{
  StreamReaderBase* reader = nullptr;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = readers_.find(stream_id);
    if (it == readers_.end() || it->second.reader == nullptr) {
      return;
    }

    if (it->second.unreliable || final_sequence == kEmptyStreamFinalSequence) {
      reader = it->second.reader;
      readers_.erase(it);
    } else if (it->second.next_expected_sequence > final_sequence) {
      reader = it->second.reader;
      readers_.erase(it);
    } else {
      it->second.final_sequence = final_sequence;
    }
  }

  if (reader) {
    reader->on_complete();
  }
}

void StreamManager::on_stream_error(uint64_t stream_id, uint32_t error_code, flat_buffer&& error_data)
{
  StreamReaderBase* reader = nullptr;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = readers_.find(stream_id);
    if (it != readers_.end()) {
      reader = it->second.reader;
      readers_.erase(it);
    }
  }

  if (reader) {
    reader->on_error(error_code, std::move(error_data));
  }
}

void StreamManager::on_stream_cancel(uint64_t stream_id)
{
  StreamReaderBase* reader = nullptr;
  std::unique_ptr<StreamWriterBase> writer;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto reader_it = readers_.find(stream_id);
    if (reader_it != readers_.end()) {
      reader = reader_it->second.reader;
      readers_.erase(reader_it);
    }

    auto writer_it = writers_.find(stream_id);
    if (writer_it != writers_.end()) {
      writer = std::move(writer_it->second.writer);
      writers_.erase(writer_it);
    }
  }

  if (reader) {
    reader->on_complete();
  }

  if (writer) {
    writer->cancel();
  }
}

void StreamManager::send_chunk(uint64_t stream_id,
                               std::span<const uint8_t> data,
                               uint64_t sequence)
{
  // Check if this is an unreliable stream
  bool unreliable = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    unreliable = is_stream_unreliable(stream_id);
  }

  NPRPC_STREAM_MANAGER_LOG_TRACE("StreamManager::send_chunk called: stream_id={}, data.size()={}, sequence={}, unreliable={}",
                  stream_id, data.size(), sequence, unreliable);

  // TODO: Shared Memory Optimization - write directly to shared ring buffer
  if (session_.shm_channel) {
    // For now, fall through to regular transport
    // Future: use zero-copy path via shared memory channel
  }

  // Build the message - start with fixed-size header and fields
  flat_buffer fb;
  constexpr size_t header_size = sizeof(flat::Header);
  // StreamChunk fixed part: stream_id(8) + sequence(8) + Vector overhead(8) + window_size(4) = 28 bytes
  // Then variable data follows
  constexpr size_t chunk_fixed_size = sizeof(flat::StreamChunk);

  // Prepare initial buffer for header + fixed fields
  fb.prepare(header_size + chunk_fixed_size);
  fb.commit(header_size + chunk_fixed_size);

  // Fill in the header first (may need to update size after data allocation)
  auto* header = reinterpret_cast<flat::Header*>(fb.data().data());
  header->msg_id = MessageId::StreamDataChunk;
  header->msg_type = MessageType::Request;
  header->request_id = 0;

  // Fill in the chunk message
  flat::StreamChunk_Direct chunk_msg(fb, header_size);
  chunk_msg.stream_id() = stream_id;
  chunk_msg.sequence() = sequence;
  chunk_msg.window_size() = 0;

  // Allocate and copy data using the Vector allocation pattern
  // Note: data() may reallocate the buffer, so header pointer may be invalid after this
  chunk_msg.data(static_cast<uint32_t>(data.size()));
  if (data.size() > 0) {
    auto data_span = chunk_msg.data();
    std::memcpy(data_span.data(), data.data(), data.size());
  }

  // Re-fetch header pointer after potential reallocation and update final size
  header = reinterpret_cast<flat::Header*>(fb.data().data());
  header->size = static_cast<uint32_t>(fb.size());

  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!is_stream_started(stream_id)) {
      pending_messages_[stream_id].push_back(std::move(fb));
      return;
    }
  }

  dispatch_buffer(stream_id, std::move(fb));
}

void StreamManager::send_complete(uint64_t stream_id, uint64_t final_sequence)
{
  flat_buffer fb;
  constexpr size_t header_size = sizeof(flat::Header);
  constexpr size_t msg_size = sizeof(flat::StreamComplete);  // stream_id(8) + final_sequence(8)
  constexpr size_t total_size = header_size + msg_size;

  fb.prepare(total_size);
  fb.commit(total_size);

  auto* header = reinterpret_cast<flat::Header*>(fb.data().data());
  header->size = total_size;
  header->msg_id = MessageId::StreamCompletion;
  header->msg_type = MessageType::Request;
  header->request_id = 0;

  flat::StreamComplete_Direct msg(fb, header_size);
  msg.stream_id() = stream_id;
  msg.final_sequence() = final_sequence;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!is_stream_started(stream_id)) {
      pending_messages_[stream_id].push_back(std::move(fb));
      return;
    }
  }

  dispatch_buffer(stream_id, std::move(fb));
}

void StreamManager::send_error(uint64_t stream_id,
                               uint32_t error_code,
                               std::span<const uint8_t> error_data)
{
  flat_buffer fb;
  constexpr size_t header_size = sizeof(flat::Header);
  constexpr size_t error_fixed_size = sizeof(flat::StreamError);

  // Prepare initial buffer for header + fixed fields
  fb.prepare(header_size + error_fixed_size);
  fb.commit(header_size + error_fixed_size);

  // Fill in the header
  auto* header = reinterpret_cast<flat::Header*>(fb.data().data());
  header->msg_id = MessageId::StreamError;
  header->msg_type = MessageType::Request;
  header->request_id = 0;

  // Fill in the error message
  flat::StreamError_Direct msg(fb, header_size);
  msg.stream_id() = stream_id;
  msg.error_code() = error_code;

  // Allocate and copy error data (may reallocate buffer)
  msg.error_data(static_cast<uint32_t>(error_data.size()));
  if (error_data.size() > 0) {
    auto data_span = msg.error_data();
    std::memcpy(data_span.data(), error_data.data(), error_data.size());
  }

  // Re-fetch header pointer after potential reallocation and update final size
  header = reinterpret_cast<flat::Header*>(fb.data().data());
  header->size = static_cast<uint32_t>(fb.size());

  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!is_stream_started(stream_id)) {
      pending_messages_[stream_id].push_back(std::move(fb));
      return;
    }
  }

  dispatch_buffer(stream_id, std::move(fb));
}

void StreamManager::send_cancel(uint64_t stream_id)
{
  flat_buffer fb;
  constexpr size_t header_size = sizeof(flat::Header);
  constexpr size_t msg_size = sizeof(flat::StreamCancel);  // stream_id(8)
  constexpr size_t total_size = header_size + msg_size;

  fb.prepare(total_size);
  fb.commit(total_size);

  auto* header = reinterpret_cast<flat::Header*>(fb.data().data());
  header->size = total_size;
  header->msg_id = MessageId::StreamCancellation;
  header->msg_type = MessageType::Request;
  header->request_id = 0;

  flat::StreamCancel_Direct msg(fb, header_size);
  msg.stream_id() = stream_id;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!is_stream_started(stream_id)) {
      pending_messages_[stream_id].push_back(std::move(fb));
      return;
    }
  }

  dispatch_buffer(stream_id, std::move(fb));
}

void StreamManager::send_window_update(uint64_t stream_id, uint32_t credits)
{
  // Window updates flow from receiver to sender via the main reliable channel.
  flat_buffer fb;
  constexpr size_t header_size = sizeof(flat::Header);
  // stream_id (u64, 8 bytes) + credits (u32, 4 bytes) = 12 bytes payload;
  // sizeof(flat::StreamWindowUpdate) = 16 (including 4-byte padding).
  constexpr size_t msg_size = 12; // only the bytes we actually write
  constexpr size_t total_size = header_size + msg_size;

  fb.prepare(total_size);
  fb.commit(total_size);

  auto* header = reinterpret_cast<flat::Header*>(fb.data().data());
  header->size = total_size;
  header->msg_id = MessageId::StreamWindowUpdate;
  header->msg_type = MessageType::Request;
  header->request_id = 0;

  flat::StreamWindowUpdate_Direct msg(fb, header_size);
  msg.stream_id() = stream_id;
  msg.credits() = credits;

  // Window updates are always sent on the main (reliable) channel.
  if (send_callback_)
    send_callback_(std::move(fb));
}

void StreamManager::on_window_update(uint64_t stream_id, uint32_t credits)
{
  NPRPC_STREAM_MANAGER_LOG_TRACE("StreamManager::on_window_update stream_id={} credits={}", stream_id, credits);

  boost::asio::steady_timer* timer = nullptr;
  std::vector<std::pair<flat_buffer, std::function<void()>>> to_dispatch;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = writers_.find(stream_id);
    if (it == writers_.end()) return;

    it->second.credits += static_cast<int32_t>(credits);

    // Drain any pending external (Swift/C) writes using newly granted credits.
    while (it->second.credits > 0 && !it->second.pending_writes.empty()) {
      auto& pw = it->second.pending_writes.front();
      flat_buffer fb;
      constexpr size_t hdr = sizeof(flat::Header);
      constexpr size_t fixed = sizeof(flat::StreamChunk);
      fb.prepare(hdr + fixed);
      fb.commit(hdr + fixed);
      auto* header = reinterpret_cast<flat::Header*>(fb.data().data());
      header->msg_id = MessageId::StreamDataChunk;
      header->msg_type = MessageType::Request;
      header->request_id = 0;
      flat::StreamChunk_Direct msg(fb, hdr);
      msg.stream_id() = stream_id;
      msg.sequence() = pw.sequence;
      msg.window_size() = 0;
      msg.data(static_cast<uint32_t>(pw.data.size()));
      if (!pw.data.empty()) {
        auto span = msg.data();
        std::memcpy(span.data(), pw.data.data(), pw.data.size());
      }
      header = reinterpret_cast<flat::Header*>(fb.data().data());
      header->size = static_cast<uint32_t>(fb.size());
      to_dispatch.emplace_back(std::move(fb), std::move(pw.callback));
      it->second.pending_writes.pop_front();
      --it->second.credits;
    }

    timer = it->second.credit_timer;
    it->second.credit_timer = nullptr;
  }

  // Send queued external chunks and invoke their callbacks (outside the lock).
  for (auto& [fb, cb] : to_dispatch) {
    dispatch_buffer(stream_id, std::move(fb));
    if (cb) cb();
  }

  // Wake the co_spawn coroutine if it was parked waiting for credits.
  if (timer) {
    boost::asio::dispatch(timer->get_executor(),
        [timer]() { timer->cancel(); });
  }
}

void StreamManager::register_external_writer(uint64_t stream_id)
{
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = writers_.find(stream_id);
  if (it == writers_.end()) {
    // Create a StreamInfo entry with no coroutine writer but with credits.
    writers_[stream_id] = StreamInfo{};
  }
  // If already present (e.g. from a deferred stream start), leave credits alone.
}

void StreamManager::write_chunk_or_queue(uint64_t stream_id,
                                         std::span<const uint8_t> data,
                                         uint64_t sequence,
                                         std::function<void()> callback)
{
  flat_buffer fb_to_send;
  bool send_now = false;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = writers_.find(stream_id);
    if (it == writers_.end()) {
      // Stream not registered — just send and callback without credits.
      send_now = true;
    } else if (it->second.pending_writes.empty() && it->second.credits > 0) {
      --it->second.credits;
      send_now = true;
    } else {
      // No credits: enqueue.
      StreamInfo::PendingWrite pw;
      pw.data.assign(data.begin(), data.end());
      pw.sequence = sequence;
      pw.callback = std::move(callback);
      it->second.pending_writes.push_back(std::move(pw));
      return; // callback will be invoked by on_window_update
    }
  }

  if (send_now) {
    send_chunk(stream_id, data, sequence);
    if (callback) callback();
  }
}

void StreamManager::cancel_all()
{
  std::lock_guard<std::mutex> lock(mutex_);

  for (auto& [id, info] : writers_) {
    info.writer->cancel();
  }
  writers_.clear();
  active_tasks_.clear();
  pending_messages_.clear();
  pending_stream_starts_.clear();
  started_streams_.clear();

  for (auto& [id, reader] : readers_) {
    // Send empty error buffer to indicate connection closed
    flat_buffer empty_fb;
    if (reader.reader) {
      reader.reader->on_error(0, std::move(empty_fb));
    }
  }
  readers_.clear();
}

} // namespace nprpc::impl

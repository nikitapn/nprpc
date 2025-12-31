// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <unordered_map>

#include <nprpc_base.hpp>
#include <nprpc/export.hpp>

namespace nprpc {

class SessionContext;
class StreamWriterBase;
class StreamReaderBase;

namespace impl {

// Manages active streams per session
class NPRPC_API StreamManager
{
public:
  // Callback type for sending data back through the session
  using SendCallback = std::function<void(flat_buffer&&)>;
  // Callback type for posting async work
  using PostCallback = std::function<void(std::function<void()>)>;

  explicit StreamManager(SessionContext& session);
  ~StreamManager();

  // Set the callback for sending data (must be called by Session after construction)
  void set_send_callback(SendCallback callback) { send_callback_ = std::move(callback); }

  // Set the callback for posting async work (must be called by Session after construction)
  void set_post_callback(PostCallback callback) { post_callback_ = std::move(callback); }

  // Server-side: register outgoing stream
  void register_stream(uint64_t stream_id,
                       std::unique_ptr<StreamWriterBase> writer);

  // Client-side: register incoming stream
  void register_reader(uint64_t stream_id, StreamReaderBase* reader);

  // Handle incoming messages
  void on_chunk_received(flat_buffer&& fb);
  void on_stream_complete(uint64_t stream_id);
  void on_stream_error(uint64_t stream_id, uint32_t error_code, flat_buffer&& error_data);
  void on_stream_cancel(uint64_t stream_id);

  // Send methods
  void send_chunk(uint64_t stream_id,
                  std::span<const uint8_t> data,
                  uint64_t sequence);
  void send_complete(uint64_t stream_id, uint64_t final_sequence);
  void send_error(uint64_t stream_id,
                  uint32_t error_code,
                  std::span<const uint8_t> error_data);
  void send_cancel(uint64_t stream_id);

  // Cleanup
  void cancel_all();

private:
  SessionContext& session_;
  SendCallback send_callback_;
  PostCallback post_callback_;

  // Active outgoing streams (server-side)
  std::unordered_map<uint64_t, std::unique_ptr<StreamWriterBase>> writers_;

  // Active incoming streams (client-side)
  // Readers are owned by the client application (StreamReader<T>), so we hold
  // raw pointers
  std::unordered_map<uint64_t, StreamReaderBase*> readers_;

  // Mutex for thread-safe access
  std::mutex mutex_;
};

// Generate unique stream ID (client-side)
NPRPC_API uint64_t generate_stream_id();

}} // namespace impl

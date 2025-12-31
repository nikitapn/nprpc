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
  // Callback type for sending on main stream (control messages, always reliable)
  using SendCallback = std::function<void(flat_buffer&&)>;
  // Callback type for sending on native QUIC stream (reliable stream data)
  using SendNativeStreamCallback = std::function<void(flat_buffer&&)>;
  // Callback type for sending datagrams (unreliable stream data)
  using SendDatagramCallback = std::function<bool(flat_buffer&&)>;
  // Callback type for posting async work
  using PostCallback = std::function<void(std::function<void()>)>;

  explicit StreamManager(SessionContext& session);
  ~StreamManager();

  // Set the callback for sending on main stream (control messages)
  void set_send_callback(SendCallback callback) { send_callback_ = std::move(callback); }

  // Set the callback for sending on native QUIC streams (reliable stream data)
  void set_send_native_stream_callback(SendNativeStreamCallback callback) { send_native_stream_callback_ = std::move(callback); }

  // Set the callback for sending datagrams (unreliable stream data)
  void set_send_datagram_callback(SendDatagramCallback callback) { send_datagram_callback_ = std::move(callback); }

  // Set the callback for posting async work (must be called by Session after construction)
  void set_post_callback(PostCallback callback) { post_callback_ = std::move(callback); }

  // Server-side: register outgoing stream (with optional unreliable flag)
  void register_stream(uint64_t stream_id,
                       std::unique_ptr<StreamWriterBase> writer,
                       bool unreliable = false);

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
  SendNativeStreamCallback send_native_stream_callback_;
  SendDatagramCallback send_datagram_callback_;
  PostCallback post_callback_;

  // Active outgoing streams (server-side)
  struct StreamInfo {
    std::unique_ptr<StreamWriterBase> writer;
    bool unreliable = false;
  };
  std::unordered_map<uint64_t, StreamInfo> writers_;

  // Active incoming streams (client-side)
  // Readers are owned by the client application (StreamReader<T>), so we hold
  // raw pointers
  std::unordered_map<uint64_t, StreamReaderBase*> readers_;

  // Mutex for thread-safe access
  std::mutex mutex_;
  
  // Internal helper to determine if a stream is unreliable
  bool is_stream_unreliable(uint64_t stream_id) const;
};

// Generate unique stream ID (client-side)
NPRPC_API uint64_t generate_stream_id();

}} // namespace impl

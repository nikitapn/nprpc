// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

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
  explicit StreamManager(SessionContext& session);
  ~StreamManager();

  // Server-side: register outgoing stream
  void register_stream(uint64_t stream_id,
                       std::unique_ptr<StreamWriterBase> writer);

  // Client-side: register incoming stream
  void register_reader(uint64_t stream_id, StreamReaderBase* reader);

  // Handle incoming messages
  void on_chunk_received(flat_buffer&& fb);
  void on_stream_complete(const impl::flat::StreamComplete& msg);
  void on_stream_error(const impl::flat::StreamError& msg);
  void on_stream_cancel(const impl::flat::StreamCancel& msg);

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

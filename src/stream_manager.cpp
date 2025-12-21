// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include "logging.hpp"
#include "nprpc_base.hpp"
#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/impl/session.hpp>
#include <nprpc/impl/shared_memory_channel.hpp>
#include <nprpc/impl/stream_manager.hpp>
#include <nprpc/session_context.h>
#include <nprpc/stream_base.hpp>

#include <atomic>
#include <random>

namespace nprpc::impl {

// Generate unique stream ID using combination of random + counter
uint64_t generate_stream_id()
{
  static std::atomic<uint64_t> counter{0};
  static const uint64_t random_base = []() {
    std::random_device rd;
    return (static_cast<uint64_t>(rd()) << 32) | rd();
  }();

  return random_base ^ (++counter);
}

StreamManager::StreamManager(SessionContext& session)
    : session_(session)
{
}

StreamManager::~StreamManager() { cancel_all(); }

void StreamManager::register_stream(uint64_t stream_id,
                                    std::unique_ptr<StreamWriterBase> writer)
{
  std::lock_guard<std::mutex> lock(mutex_);
  auto* raw_writer = writer.get();
  writers_[stream_id] = std::move(writer);

  // Start the stream by resuming the coroutine
  raw_writer->resume();
}

void StreamManager::register_reader(uint64_t stream_id,
                                    StreamReaderBase* reader)
{
  std::lock_guard<std::mutex> lock(mutex_);
  readers_[stream_id] = reader;
}

void StreamManager::on_chunk_received(flat_buffer&& fb)
{
  flat::StreamChunk_Direct chunk(fb, sizeof(flat::Header));
  const auto stream_id = chunk.stream_id();

  std::lock_guard<std::mutex> lock(mutex_);
  auto it = readers_.find(stream_id);
  if (it != readers_.end()) {
    // Pass the buffer to the reader - it contains the chunk data
    it->second->on_chunk_received(std::move(fb));
  } else {
    NPRPC_LOG_WARN("Received chunk for unknown stream: {}", stream_id);
  }
}

void StreamManager::on_stream_complete(const flat::StreamComplete& msg)
{
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = readers_.find(msg.stream_id);
  if (it != readers_.end()) {
    it->second->on_complete();
    readers_.erase(it);
  }
}

void StreamManager::on_stream_error(const flat::StreamError& msg)
{
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = readers_.find(msg.stream_id);
  if (it != readers_.end()) {
    // Create a flat_buffer containing the error data for the reader
    flat_buffer error_fb;
    // The error_data is a flat::Vector - copy its contents
    auto error_span = static_cast<nprpc::flat::Span<const uint8_t>>(msg.error_data);
    if (error_span.size() > 0) {
      error_fb.prepare(error_span.size());
      error_fb.commit(error_span.size());
      std::memcpy(error_fb.data().data(), error_span.data(), error_span.size());
    }
    it->second->on_error(msg.error_code, std::move(error_fb));
    readers_.erase(it);
  }
}

void StreamManager::on_stream_cancel(const flat::StreamCancel& msg)
{
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = writers_.find(msg.stream_id);
  if (it != writers_.end()) {
    it->second->cancel();
    writers_.erase(it);
  }
}

void StreamManager::send_chunk(uint64_t stream_id,
                               std::span<const uint8_t> data,
                               uint64_t sequence)
{
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
  header->size = static_cast<uint32_t>(fb.size() - 4);

  // Send fire-and-forget
  if (auto session = g_rpc->get_session(session_.remote_endpoint)) {
    session->send_datagram(std::move(fb));
  }
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
  header->size = total_size - 4;
  header->msg_id = MessageId::StreamCompletion;
  header->msg_type = MessageType::Request;
  header->request_id = 0;

  flat::StreamComplete_Direct msg(fb, header_size);
  msg.stream_id() = stream_id;
  msg.final_sequence() = final_sequence;

  if (auto session = g_rpc->get_session(session_.remote_endpoint)) {
    session->send_datagram(std::move(fb));
  }
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
  header->size = static_cast<uint32_t>(fb.size() - 4);

  if (auto session = g_rpc->get_session(session_.remote_endpoint)) {
    session->send_datagram(std::move(fb));
  }
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
  header->size = total_size - 4;
  header->msg_id = MessageId::StreamCancellation;
  header->msg_type = MessageType::Request;
  header->request_id = 0;

  flat::StreamCancel_Direct msg(fb, header_size);
  msg.stream_id() = stream_id;

  if (auto session = g_rpc->get_session(session_.remote_endpoint)) {
    session->send_datagram(std::move(fb));
  }
}

void StreamManager::cancel_all()
{
  std::lock_guard<std::mutex> lock(mutex_);

  for (auto& [id, writer] : writers_) {
    writer->cancel();
  }
  writers_.clear();

  for (auto& [id, reader] : readers_) {
    // Send empty error buffer to indicate connection closed
    flat_buffer empty_fb;
    reader->on_error(0, std::move(empty_fb));
  }
  readers_.clear();
}

} // namespace nprpc::impl

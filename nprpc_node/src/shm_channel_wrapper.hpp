// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

// nprpc_node - Native Node.js addon for shared memory transport

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <napi.h>
#include <string>
#include <thread>
#include <uv.h> // libuv for async I/O

// Forward declaration - we'll use a simplified channel without boost::asio
namespace nprpc::impl {
class LockFreeRingBuffer;
}

namespace nprpc_node {

/**
 * @brief Simplified shared memory channel for Node.js
 *
 * This is a lightweight wrapper around LockFreeRingBuffer that doesn't
 * depend on boost::asio. Instead, it uses libuv (via N-API) for async I/O.
 */
class ShmChannel
{
public:
  static constexpr size_t RING_BUFFER_SIZE =
      16 * 1024 * 1024; // 16MB per direction
  static constexpr size_t MAX_MESSAGE_SIZE =
      32 * 1024 * 1024; // 32MB max message

  ShmChannel(const std::string& channel_id, bool is_server, bool create);
  ~ShmChannel();

  // Non-copyable
  ShmChannel(const ShmChannel&) = delete;
  ShmChannel& operator=(const ShmChannel&) = delete;

  bool is_open() const
  {
    return send_ring_ != nullptr && recv_ring_ != nullptr;
  }
  const std::string& channel_id() const { return channel_id_; }
  const std::string& error() const { return error_; }

  // Send data (may block briefly if buffer is full)
  bool send(const uint8_t* data, uint32_t size);

  // Try to receive data (non-blocking)
  // Returns number of bytes received, 0 if no data, -1 on error
  int32_t try_receive(uint8_t* buffer, size_t buffer_size);

  // Check if data is available to read
  bool has_data() const;

  // Get file descriptor for poll (used by libuv)
  int get_poll_fd() const { return eventfd_; }

private:
  std::string channel_id_;
  std::string send_ring_name_;
  std::string recv_ring_name_;

  std::unique_ptr<nprpc::impl::LockFreeRingBuffer> send_ring_;
  std::unique_ptr<nprpc::impl::LockFreeRingBuffer> recv_ring_;

  bool is_server_;
  int eventfd_ = -1; // For libuv polling
  std::string error_;
};

/**
 * @brief N-API wrapper for ShmChannel
 */
class ShmChannelWrapper : public Napi::ObjectWrap<ShmChannelWrapper>
{
public:
  static Napi::Object Init(Napi::Env env, Napi::Object exports);
  ShmChannelWrapper(const Napi::CallbackInfo& info);
  ~ShmChannelWrapper();

private:
  // JavaScript methods
  Napi::Value IsOpen(const Napi::CallbackInfo& info);
  Napi::Value GetChannelId(const Napi::CallbackInfo& info);
  Napi::Value GetError(const Napi::CallbackInfo& info);
  Napi::Value Send(const Napi::CallbackInfo& info);
  Napi::Value TryReceive(const Napi::CallbackInfo& info);
  Napi::Value HasData(const Napi::CallbackInfo& info);
  void Close(const Napi::CallbackInfo& info);

  // Async polling
  Napi::Value StartPolling(const Napi::CallbackInfo& info);
  Napi::Value StopPolling(const Napi::CallbackInfo& info);

  std::unique_ptr<ShmChannel> channel_;

  // For async polling
  uv_poll_t* poll_handle_ = nullptr;
  Napi::ThreadSafeFunction tsfn_;
  std::atomic<bool> polling_{false};
};

} // namespace nprpc_node

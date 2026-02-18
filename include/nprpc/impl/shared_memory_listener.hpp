// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <nprpc/export.hpp>
#include <nprpc/impl/lock_free_ring_buffer.hpp>

#include <atomic>
#include <boost/asio.hpp>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace nprpc::impl {

class SharedMemoryChannel;

/**
 * @brief Handshake message for establishing shared memory connections
 *
 * Client sends this to the listener's well-known queue.
 * Server responds with the same message, confirming the channel IDs.
 */
struct SharedMemoryHandshake {
  static constexpr uint32_t MAGIC = 0x534D454D; // "SMEM"
  static constexpr uint32_t VERSION = 1;

  uint32_t magic;
  uint32_t version;
  char channel_id[64];    // UUID for the dedicated client-server channel
  char ready_flag_shm[64]; // Name of the client-owned one-page shm with the ready atomic

  SharedMemoryHandshake()
      : magic(MAGIC)
      , version(VERSION)
  {
    channel_id[0]     = '\0';
    ready_flag_shm[0] = '\0';
  }

  bool is_valid() const
  {
    return magic == MAGIC && version == VERSION &&
           channel_id[0] != '\0' && ready_flag_shm[0] != '\0';
  }
};

/**
 * @brief Listener for accepting shared memory connections
 *
 * Similar to TCP socket listener, but uses a well-known message queue
 * for accepting connections. Each accepted connection gets its own
 * dedicated bidirectional channel.
 *
 * Protocol:
 * 1. Server creates listener with well-known name (e.g., "mem://server_name")
 * 2. Client generates UUID for dedicated channel
 * 3. Client sends handshake to well-known queue with channel UUID
 * 4. Server receives handshake, creates dedicated channel with that UUID
 * 5. Server sends handshake back to confirm
 * 6. Both sides now use the dedicated channel for RPC communication
 */
class NPRPC_API SharedMemoryListener
{
public:
  using AcceptHandler =
      std::function<void(std::unique_ptr<SharedMemoryChannel>)>;

private:
  std::string listener_name_;
  boost::asio::io_context& ioc_;

  std::unique_ptr<LockFreeRingBuffer> accept_ring_;
  std::unique_ptr<std::thread> accept_thread_;
  std::atomic<bool> running_{false};

  AcceptHandler accept_handler_;

public:
  /**
   * @brief Construct a new Shared Memory Listener
   *
   * @param ioc IO context for async operations
   * @param listener_name Well-known name for the listener (e.g.,
   * "nprpc_server")
   * @param accept_handler Callback invoked with each new connection
   */
  SharedMemoryListener(boost::asio::io_context& ioc,
                       const std::string& listener_name,
                       AcceptHandler accept_handler);

  ~SharedMemoryListener();

  // Non-copyable, non-movable
  SharedMemoryListener(const SharedMemoryListener&) = delete;
  SharedMemoryListener& operator=(const SharedMemoryListener&) = delete;
  SharedMemoryListener(SharedMemoryListener&&) = delete;
  SharedMemoryListener& operator=(SharedMemoryListener&&) = delete;

  /**
   * @brief Start accepting connections
   */
  void start();

  /**
   * @brief Stop accepting connections
   */
  void stop();

  /**
   * @brief Get the listener name (for clients to connect to)
   */
  const std::string& listener_name() const { return listener_name_; }

  /**
   * @brief Get the endpoint string for this listener
   */
  std::string endpoint_string() const { return "mem://" + listener_name_; }

private:
  void accept_loop();
  void handle_connection_request(const SharedMemoryHandshake& handshake);
};

/**
 * @brief Client-side connection establishment
 *
 * @param ioc IO context
 * @param listener_name Well-known listener name to connect to
 * @return std::unique_ptr<SharedMemoryChannel> Dedicated channel for this
 * connection
 */
NPRPC_API std::unique_ptr<SharedMemoryChannel>
connect_to_shared_memory_listener(boost::asio::io_context& ioc,
                                  const std::string& listener_name);

} // namespace nprpc::impl

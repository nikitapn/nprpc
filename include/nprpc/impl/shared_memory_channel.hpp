#pragma once

#include <nprpc/export.hpp>
#include <nprpc/impl/lock_free_ring_buffer.hpp>

#include <boost/asio.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace nprpc::impl {

/**
 * @brief Bidirectional IPC channel using lock-free ring buffers in shared
 * memory
 *
 * Uses memory-mapped files with lock-free ring buffers for true zero-copy IPC.
 * Two ring buffers: one for server-to-client, one for client-to-server.
 * Automatically handles cleanup when the last reference is destroyed.
 *
 * Variable-sized messages: Each message has a 4-byte header + payload.
 * This reduces memory footprint significantly compared to fixed-size slots.
 */
class NPRPC_API SharedMemoryChannel
{
public:
  static constexpr size_t MAX_MESSAGE_SIZE =
      32 * 1024 * 1024; // 32MB (same as TCP/WebSocket)
  static constexpr size_t RING_BUFFER_SIZE =
      16 * 1024 * 1024; // 16MB per ring buffer (variable-sized messages)
  // Total per connection: 32MB (16MB × 2 directions) vs 256MB with fixed
  // slots
  // - 8x reduction!

private:
  std::string channel_id_;
  std::string send_ring_name_; // Ring buffer we write to
  std::string recv_ring_name_; // Ring buffer we read from

  std::unique_ptr<LockFreeRingBuffer> send_ring_;
  std::unique_ptr<LockFreeRingBuffer> recv_ring_;

  bool is_server_;
  boost::asio::io_context& ioc_;

  std::unique_ptr<std::thread> read_thread_;
  std::atomic<bool> running_{true};

  // Buffer for receiving messages
  std::vector<char> recv_buffer_;

public:
  /**
   * @brief Construct a new Shared Memory Channel
   *
   * @param ioc Boost.Asio io_context for async operations
   * @param channel_id Unique channel identifier (shared between client and
   * server)
   * @param is_server true if this is the server side, false for client
   * @param create_rings true to create ring buffers (server), false to open
   * existing (client)
   */
  SharedMemoryChannel(boost::asio::io_context& ioc,
                      const std::string& channel_id,
                      bool is_server,
                      bool create_rings);

  ~SharedMemoryChannel();

  // Non-copyable, movable
  SharedMemoryChannel(const SharedMemoryChannel&) = delete;
  SharedMemoryChannel& operator=(const SharedMemoryChannel&) = delete;
  SharedMemoryChannel(SharedMemoryChannel&&) = default;
  SharedMemoryChannel& operator=(SharedMemoryChannel&&) = default;

  /**
   * @brief Send data through the channel (non-blocking)
   *
   * @param data Pointer to data to send
   * @param size Size of data in bytes
   * @return true if sent successfully, false if buffer is full or error
   */
  bool send(const void* data, uint32_t size);

  //--------------------------------------------------------------------------
  // Zero-copy API for direct ring buffer access
  //--------------------------------------------------------------------------

  /**
   * @brief Reserve space in send ring buffer for zero-copy write
   *
   * @param max_size Maximum bytes to write
   * @return Reservation with data pointer, or invalid if buffer full
   */
  LockFreeRingBuffer::WriteReservation reserve_write(size_t max_size);

  /**
   * @brief Commit a zero-copy write with actual bytes written
   */
  void commit_write(const LockFreeRingBuffer::WriteReservation& reservation,
                    size_t actual_size);

  /**
   * @brief Get max message size for reservation
   */
  static constexpr size_t max_message_size() { return MAX_MESSAGE_SIZE; }

  /**
   * @brief Callback invoked when data is received
   *
   * Called on io_context thread. The vector is moved to avoid copying.
   */
  std::function<void(std::vector<char>&&)> on_data_received;

  /**
   * @brief Zero-copy callback for received data
   *
   * Called with a view directly into the ring buffer.
   * The view is valid until on_data_received_done() is called.
   * If set, this takes precedence over on_data_received.
   */
  std::function<void(const LockFreeRingBuffer::ReadView&)>
      on_data_received_view;

  /**
   * @brief Signal that zero-copy read is complete
   *
   * Must be called after processing data from on_data_received_view callback.
   */
  void commit_read(const LockFreeRingBuffer::ReadView& view);

  /**
   * @brief Generate a unique channel ID
   */
  static std::string generate_channel_id()
  {
    return boost::uuids::to_string(boost::uuids::random_generator()());
  }

  /**
   * @brief Check if channel is valid and connected
   */
  bool is_valid() const { return send_ring_ && recv_ring_; }

  /**
   * @brief Get the channel ID
   */
  const std::string& channel_id() const { return channel_id_; }

  /**
   * @brief Get the receive ring buffer (for flat_buffer commit_read tracking)
   */
  LockFreeRingBuffer* get_recv_ring() const { return recv_ring_.get(); }

private:
  void read_loop();
  void cleanup_rings();
};

} // namespace nprpc::impl

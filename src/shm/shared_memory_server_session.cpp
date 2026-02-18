// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include "../logging.hpp"
#include <memory>
#include <nprpc/common.hpp>
#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/impl/session.hpp>
#include <nprpc/impl/shared_memory_channel.hpp>

namespace nprpc::impl {

/**
 * @brief Server-side shared memory session
 *
 * Similar to Session_Socket, but for shared memory transport.
 * Handles incoming RPC requests from a client via SharedMemoryChannel.
 */
class SharedMemoryServerSession
    : public Session,
      public std::enable_shared_from_this<SharedMemoryServerSession>
{
  std::unique_ptr<SharedMemoryChannel> channel_;

public:
  // Server sessions don't initiate calls, so these should never be called
  virtual void timeout_action() final
  {
    // Server sessions don't have timeouts
  }

  virtual void shutdown() override
  {
    // Clear the callback to break the circular reference
    // (callback captures shared_from_this())
    if (channel_) {
      channel_->on_data_received = nullptr;
    }
    Session::shutdown();
  }

  virtual void send_receive(flat_buffer&, uint32_t) override
  {
    // Server sessions don't make outbound calls
    assert(false && "send_receive should not be called on server session");
  }

  virtual void send_receive_async(
      flat_buffer&&,
      std::optional<std::function<void(const boost::system::error_code&,
                                       flat_buffer&)>>&&,
      uint32_t) override
  {
    // Server sessions don't make outbound calls
    assert(false &&
           "send_receive_async should not be called on server session");
  }

  /**
   * @brief Handle incoming request message
   *
   * Called by SharedMemoryChannel when a complete message is received.
   * Processes the RPC request and sends response back.
   */
  void on_message_received(const LockFreeRingBuffer::ReadView& read_view)
  {
    try {
      // Zero-copy read: create a view directly into the ring buffer
      flat_buffer rx_buffer(const_cast<std::uint8_t*>(read_view.data),
                            read_view.size, read_view.size);

      flat_buffer tx_buffer;

      // Dispatch the RPC request (calls servant methods)
      // This synchronously deserializes rx_buffer and calls the servant
      handle_request(rx_buffer, tx_buffer);

      // Now that handle_request is done reading rx_buffer,
      // we can commit the read and free the ring buffer space
      channel_->commit_read(read_view);

      // Send response back through the channel
      if (tx_buffer.has_write_reservation() && tx_buffer.is_view_mode()) {
        // Zero-copy path: reconstruct the reservation and commit
        LockFreeRingBuffer::WriteReservation reservation;
        reservation.data = tx_buffer.data_ptr();
        reservation.max_size = tx_buffer.max_size();
        reservation.write_idx = tx_buffer.reservation_write_idx();
        reservation.valid = true;

        // std::cout << "[nprpc][D] SERVER committing zero-copy
        // response: size="
        // << tx_buffer.size()
        //           << " write_idx=" << reservation.write_idx << "
        //           data=" << (void*)reservation.data << std::endl;

        // Dump first 32 bytes for debugging
        // std::cout << "[nprpc][D] SERVER response first 32 bytes: ";
        // for (size_t i = 0; i < std::min(tx_buffer.size(),
        // size_t(32)); ++i) {
        //     printf("%02x ", (unsigned char)tx_buffer.data_ptr()[i]);
        // }
        // std::cout << std::endl;

        channel_->commit_write(reservation, tx_buffer.size());
      } else {
        // Should not happen for now...
        NPRPC_LOG_ERROR("SharedMemoryServerSession: Unexpected "
                        "non-zero-copy response path");
        // std::abort();
        // Fallback path: buffer was converted to owned mode or didn't
        // have reservation Need to get a new reservation and copy the
        // data
        auto new_reservation = channel_->reserve_write(tx_buffer.size());
        if (new_reservation) {
          std::memcpy(new_reservation.data, tx_buffer.data_ptr(),
                      tx_buffer.size());
          channel_->commit_write(new_reservation, tx_buffer.size());
        } else {
          NPRPC_LOG_ERROR("SharedMemoryServerSession: Failed to "
                          "allocate response buffer");
        }
      }
    } catch (const std::exception& e) {
      NPRPC_LOG_ERROR("SharedMemoryServerSession: Error processing message: {}",
                      e.what());
    }
  }

  SharedMemoryServerSession(boost::asio::io_context& ioc,
                            std::unique_ptr<SharedMemoryChannel> channel)
      : Session(ioc.get_executor())
      , channel_(std::move(channel))
  {
    // Set the endpoint for this session (used for tethered objects)
    // Server sessions get a "tethered" shared memory endpoint
    ctx_.remote_endpoint =
        EndPoint(EndPointType::SharedMemory, // Will need to add
                                             // TetheredSharedMemory if needed
                 channel_->channel_id(),
                 0); // Port not used for shared memory

    // Set the channel pointer for server-side zero-copy responses
    // This allows prepare_zero_copy_buffer to use the existing channel
    // instead of trying to create a new connection
    ctx_.shm_channel = channel_.get();

    // Note: We can't call shared_from_this() in constructor
    // The handler will be set up after construction

    NPRPC_LOG_INFO("SharedMemoryServerSession created for channel: {}",
                   channel_->channel_id());
  }

  /**
   * @brief Initialize the session (must be called after construction)
   *
   * This sets up the data received handler. Must be called after the
   * shared_ptr is constructed since we use shared_from_this().
   */
  void start()
  {
    // Set up the channel to call our handler when data arrives.
    // start_reading() is called AFTER the callback is set so the read
    // thread never observes a null handler.
    channel_->on_data_received_view =
        [this, self = shared_from_this()](
            const LockFreeRingBuffer::ReadView& read_view) {
          on_message_received(read_view);
        };
    channel_->start_reading();
  }

  ~SharedMemoryServerSession()
  {
    NPRPC_LOG_INFO("SharedMemoryServerSession destroyed for channel: {}",
                   channel_->channel_id());
  }
};

/**
 * @brief Create a server session for an accepted shared memory connection
 *
 * This is called by the listener's accept handler.
 */
std::shared_ptr<Session> create_shared_memory_server_session(
    boost::asio::io_context& ioc, std::unique_ptr<SharedMemoryChannel> channel)
{
  auto session =
      std::make_shared<SharedMemoryServerSession>(ioc, std::move(channel));
  session->start(); // Initialize the handler after shared_ptr is created
  return session;
}

} // namespace nprpc::impl

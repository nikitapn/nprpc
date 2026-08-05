// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include "../logging.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>

#include <deque>
#include <memory>
#include <mutex>

#include <nprpc/common.hpp>
#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/impl/session.hpp>
#include <nprpc/impl/shared_memory_channel.hpp>
#include <nprpc/task.hpp>

namespace nprpc::impl {

/**
 * @brief Server-side shared memory session
 *
 * Similar to Session_Socket, but for shared memory transport.
 *
 * Receive path (hybrid):
 *   1. Always copy the payload out of the ring (commit_read after return).
 *   2. Fast path — default POA with AllowBlockTransport and no DispatchExecutor:
 *        handle_request() on the ring thread (extreme low latency).
 *   3. Offload path — custom executor and/or NeverBlockTransport, or when a
 *      serial drain is already in flight (FIFO with the client matcher):
 *        enqueue → ioc drain_loop → handle_request_async → s2c reply.
 */
class SharedMemoryServerSession
    : public Session,
      public std::enable_shared_from_this<SharedMemoryServerSession>
{
  std::unique_ptr<SharedMemoryChannel> channel_;
  boost::asio::io_context& ioc_;

  // Serial work queue (B1).  Guarded by mu_.
  std::mutex mu_;
  std::deque<flat_buffer> pending_;
  bool processing_ = false;
  // Keeps the drain coroutine frame alive until it finishes.
  nprpc::Task<> drain_task_;

  // Peek the target POA for messages that carry poa_idx.  Returns true when
  // dispatch must leave the ring thread (executor / NeverBlockTransport).
  static bool message_requires_offload(const flat_buffer& rx)
  {
    if (rx.size() < sizeof(flat::Header))
      return false;

    const auto* header =
        static_cast<const flat::Header*>(rx.cdata().data());
    uint16_t poa_idx = 0;
    bool has_poa = false;

    switch (header->msg_id) {
    case MessageId::FunctionCall: {
      if (rx.size() < sizeof(flat::Header) + sizeof(flat::CallHeader))
        return false;
      const auto* ch = reinterpret_cast<const flat::CallHeader*>(
          static_cast<const std::byte*>(rx.cdata().data()) +
          sizeof(flat::Header));
      poa_idx = ch->poa_idx;
      has_poa = true;
      break;
    }
    case MessageId::StreamInitialization: {
      // StreamInit layout: Header + StreamInit fields; poa_idx after stream_id.
      constexpr size_t kMin =
          sizeof(flat::Header) + sizeof(uint64_t) + sizeof(uint16_t);
      if (rx.size() < kMin)
        return false;
      const auto* p = static_cast<const std::byte*>(rx.cdata().data()) +
                      sizeof(flat::Header) + sizeof(uint64_t);
      std::memcpy(&poa_idx, p, sizeof(poa_idx));
      has_poa = true;
      break;
    }
    case MessageId::AddReference:
    case MessageId::ReleaseObject: {
      // ObjectIdLocal: poa_idx then object_id (see nprpc_base flat layout).
      if (rx.size() < sizeof(flat::Header) + sizeof(uint16_t))
        return false;
      const auto* p = static_cast<const std::byte*>(rx.cdata().data()) +
                      sizeof(flat::Header);
      std::memcpy(&poa_idx, p, sizeof(poa_idx));
      has_poa = true;
      break;
    }
    default:
      // Stream data / control: no servant hop — keep on the ring.
      return false;
    }

    if (!has_poa || !g_rpc)
      return false;

    // Out-of-range / empty POA → handle inline; handle_request will reply
    // with Error_PoaNotExist.
    if (auto poa = g_rpc->get_poa(poa_idx))
      return (*poa)->requires_off_transport_dispatch();
    return false;
  }

  void process_inline(flat_buffer& rx)
  {
    flat_buffer tx;
    try {
      const bool needs_reply = handle_request(rx, tx);
      if (needs_reply && tx.size() > 0)
        send_reply(tx);
      else
        abort_tx_reservation(tx);
    } catch (const std::exception& e) {
      NPRPC_LOG_ERROR(
          "SharedMemoryServerSession: Error processing message (inline): {}",
          e.what());
      abort_tx_reservation(tx);
    }
  }

  void send_reply(flat_buffer& tx_buffer)
  {
    if (tx_buffer.size() == 0)
      return;

    if (tx_buffer.has_write_reservation() && tx_buffer.is_view_mode()) {
      LockFreeRingBuffer::WriteReservation reservation;
      reservation.data = tx_buffer.data_ptr();
      reservation.max_size = tx_buffer.max_size();
      reservation.slot_idx = tx_buffer.reservation_write_idx();
      reservation.valid = true;
      channel_->commit_write(reservation, tx_buffer.size());
      tx_buffer.release_write_view();
    } else {
      auto new_reservation = channel_->reserve_write(tx_buffer.size());
      if (new_reservation) {
        std::memcpy(new_reservation.data, tx_buffer.data_ptr(),
                    tx_buffer.size());
        channel_->commit_write(new_reservation, tx_buffer.size());
      } else {
        NPRPC_LOG_ERROR(
            "SharedMemoryServerSession: Failed to allocate response buffer");
      }
    }
  }

  void abort_tx_reservation(flat_buffer& tx_buffer)
  {
    if (tx_buffer.has_write_reservation() && tx_buffer.is_view_mode()) {
      LockFreeRingBuffer::WriteReservation reservation;
      reservation.data = tx_buffer.data_ptr();
      reservation.max_size = tx_buffer.max_size();
      reservation.slot_idx = tx_buffer.reservation_write_idx();
      reservation.valid = true;
      channel_->abort_write(reservation);
      tx_buffer.release_write_view();
    }
  }

  // Runs on the io_context: serially process pending owned request buffers.
  // keep_alive must outlive any suspension inside handle_request_async.
  nprpc::Task<> drain_loop(
      std::shared_ptr<SharedMemoryServerSession> keep_alive)
  {
    for (;;) {
      flat_buffer rx;
      {
        std::lock_guard lock(mu_);
        if (pending_.empty()) {
          processing_ = false;
          co_return;
        }
        rx = std::move(pending_.front());
        pending_.pop_front();
      }

      flat_buffer tx;
      try {
        const bool needs_reply = co_await handle_request_async(rx, tx);
        if (needs_reply && tx.size() > 0)
          send_reply(tx);
        else
          abort_tx_reservation(tx);
      } catch (const std::exception& e) {
        NPRPC_LOG_ERROR(
            "SharedMemoryServerSession: Error processing message: {}",
            e.what());
        abort_tx_reservation(tx);
      }
      (void)keep_alive; // used for lifetime across co_await
    }
  }

  void enqueue_and_kick(flat_buffer rx)
  {
    bool should_post = false;
    {
      std::lock_guard lock(mu_);
      pending_.push_back(std::move(rx));
      if (!processing_) {
        processing_ = true;
        should_post = true;
      }
    }
    if (!should_post)
      return;

    // Leave the ring thread: commit_read runs as soon as on_message returns.
    boost::asio::post(ioc_, [self = shared_from_this()]() {
      // Only one drain at a time (processing_).  Pass self into the Task so
      // the session stays alive if handle_request_async later suspends.
      self->drain_task_ = self->drain_loop(self);
    });
  }

public:
  // Server sessions don't initiate calls, so these should never be called
  virtual void timeout_action() final
  {
    // Server sessions don't have timeouts
  }

  virtual void shutdown() override
  {
    // Clear the callbacks to break the circular reference
    // (callback captures shared_from_this())
    if (channel_) {
      channel_->on_data_received = nullptr;
      channel_->on_data_received_view = nullptr;
    }
    {
      std::lock_guard lock(mu_);
      pending_.clear();
    }
    Session::shutdown();
  }

  virtual void send_receive(flat_buffer&, uint32_t) override
  {
    // Server sessions don't make outbound calls
    assert(false && "send_receive should not be called on server session");
  }

  nprpc::Task<> send_receive_coro(flat_buffer& buffer,
                                  uint32_t timeout_ms,
                                  std::stop_token st = {}) override
  {
    assert(false && "send_receive_coro should not be called on server session");
    co_return;
  }

  virtual void send_receive_async(
      flat_buffer&&,
      std::optional<std::function<void(const boost::system::error_code&,
                                       flat_buffer&)>>&&,
      uint32_t) override
  {
    // Server sessions don't make outbound RPC calls
    assert(false &&
           "send_receive_async should not be called on server session");
  }

  // Fire-and-forget stream frames (chunks / complete / error / cancel /
  // window updates).  Must not go through send_receive_async — that path
  // is for client RPCs that wait for a reply.
  void send_stream_message(flat_buffer&& buffer) override
  {
    if (!channel_ || buffer.size() == 0)
      return;
    auto rsv = channel_->reserve_write(buffer.size());
    if (!rsv) {
      NPRPC_LOG_ERROR(
          "SharedMemoryServerSession: ring full, dropped stream frame size={}",
          buffer.size());
      return;
    }
    std::memcpy(rsv.data, buffer.data().data(), buffer.size());
    channel_->commit_write(rsv, buffer.size());
  }

  /**
   * @brief Accept one ring message: copy out, then inline or hand off.
   *
   * Returns quickly when offloading so commit_read can free the slot.
   * The low-latency path runs handle_request on this (ring) thread.
   */
  void on_message_received(const LockFreeRingBuffer::ReadView& read_view)
  {
    // Always copy out of the ring into an owned buffer.
    //
    // 1. StreamDataChunk is std::move'd into StreamManager and delivered
    //    as a raw pointer to Swift deserializers — ring payload offsets
    //    are not 4-byte aligned after variable-size messages, so typed
    //    loads trap (Swift UnsafeRawPointer.load).
    // 2. StreamInit / FunctionCall responses may grow the buffer; writing
    //    into a ring view is unsafe (see Poa.swift owned-copy path).
    // 3. Ownership lets us commit_read before an offloaded dispatch completes.
    flat_buffer rx_buffer;
    {
      auto mb = rx_buffer.prepare(read_view.size);
      std::memcpy(mb.data(), read_view.data, read_view.size);
      rx_buffer.commit(read_view.size);
    }

    // NOTE: read_view is committed by the channel's read_loop after this
    // callback returns (same contract as the client-side callback).

    // Prefer ring-thread dispatch for cheap servants (default POA).  Must
    // not interleave with an in-flight ioc drain — client matches FIFO.
    bool try_inline = !message_requires_offload(rx_buffer);
    if (try_inline) {
      std::lock_guard lock(mu_);
      if (processing_ || !pending_.empty())
        try_inline = false;
    }

    if (try_inline) {
      process_inline(rx_buffer);
      return;
    }

    enqueue_and_kick(std::move(rx_buffer));
  }

  SharedMemoryServerSession(boost::asio::io_context& ioc,
                            std::unique_ptr<SharedMemoryChannel> channel)
      : Session(ioc.get_executor())
      , channel_(std::move(channel))
      , ioc_(ioc)
  {
    // Set the endpoint for this session (used for Ephemeral objects)
    // Server sessions get a "Ephemeral" shared memory endpoint
    ctx_.remote_endpoint =
        EndPoint(EndPointType::SharedMemory, // Will need to add
                                             // EphemeralSharedMemory if needed
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
  session->bind_self(session);
  session->start(); // Initialize the handler after shared_ptr is created
  return session;
}

} // namespace nprpc::impl

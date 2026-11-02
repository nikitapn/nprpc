// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include "../logging.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>

#include <atomic>
#include <memory>

#include <nprpc/common.hpp>
#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/impl/session.hpp>
#include <nprpc/impl/shared_memory_channel.hpp>
#include <nprpc/impl/stream_manager.hpp>

namespace nprpc::impl {

/**
 * @brief Server-side shared memory session
 *
 * Receive path (hybrid):
 *   1. Always copy the payload out of the ring (commit_read after return).
 *   2. Fast path — default POA with AllowBlockTransport and no DispatchExecutor:
 *        handle_request() on the ring thread (extreme low latency).
 *   3. Otherwise Session::hand_off() runs it from the session's FIFO, so
 *      commit_read can free the slot before the servant finishes.
 *
 * Death detection:
 *   Shared memory has no connection to break — a client that is SIGKILLed
 *   leaves the rings mapped and silent, which looks exactly like an idle
 *   client.  A timer polls the channel's peer (graceful detach flag, then
 *   process liveness) and tears the session down the way a WebSocket close
 *   does: streams aborted, session dropped from RpcImpl.
 */
class SharedMemoryServerSession
    : public Session,
      public std::enable_shared_from_this<SharedMemoryServerSession>
{
  std::unique_ptr<SharedMemoryChannel> channel_;
  boost::asio::io_context& ioc_;

  std::atomic<bool> peer_dead_{false};

  // ---- process / reply -----------------------------------------------------

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
      channel_->commit_write(reservation, tx_buffer.size(), tx_buffer.fds());
      tx_buffer.release_write_view();
    } else {
      auto new_reservation = channel_->reserve_write(tx_buffer.size());
      if (new_reservation) {
        std::memcpy(new_reservation.data, tx_buffer.data_ptr(),
                    tx_buffer.size());
        channel_->commit_write(new_reservation, tx_buffer.size(),
                               tx_buffer.fds());
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

  void process_handed_off(flat_buffer& rx, bool) override
  {
    flat_buffer tx;
    try {
      // On a DispatchExecutor queue, invoke_sync is a no-op (is_running_on).
      const bool needs_reply = handle_request(rx, tx);
      if (needs_reply && tx.size() > 0)
        send_reply(tx);
      else
        abort_tx_reservation(tx);
    } catch (const std::exception& e) {
      NPRPC_LOG_ERROR(
          "SharedMemoryServerSession: Error processing message: {}", e.what());
      abort_tx_reservation(tx);
    }
  }

  // ---- peer liveness -------------------------------------------------------

  // Tear down a session whose client is gone.  Idempotent.
  //
  // Runs on the io_context: it joins the channel's read thread (so it must
  // not be the read thread) and drops the session out of RpcImpl, which
  // leaves the posting handler's own `self` as the last reference — the
  // session dies when that handler returns.
  void on_peer_dead()
  {
    if (peer_dead_.exchange(true))
      return;

    NPRPC_LOG_WARN("SharedMemoryServerSession: peer of channel {} is gone "
                   "(pid {}), closing session",
                   channel_->channel_id(), channel_->peer_process().pid);

    // Stops the reader, releases queued work and the channel's reference
    // back to us.
    shutdown();

    // Fail every stream this session carries.  The Session destructor would
    // eventually do this through ~StreamManager, but only once the last
    // external (Swift bridge) holder lets go — a reader blocked on a dead
    // client must be woken now, not then.
    if (ctx_.stream_manager)
      ctx_.stream_manager->cancel_all();

    // Drop out of RpcImpl::opened_sessions_, as a WebSocket close does.
    close();
  }

public:
  virtual void timeout_action() final {}

  virtual void shutdown() override
  {
    if (channel_) {
      // Join the reader before releasing the callbacks: the read thread is
      // executing them, and they hold the strong reference that keeps this
      // session alive while a message is in flight.  stop_reading() returns
      // false only if we *are* the read thread, in which case the callbacks
      // must stay put and ~SharedMemoryChannel finishes the teardown.
      if (channel_->stop_reading()) {
        channel_->on_data_received = nullptr;
        channel_->on_data_received_view = nullptr;
        channel_->on_peer_lost = nullptr;
      }
    }
    clear_handoffs();
    Session::shutdown();
  }

  nprpc::Task<> send_receive_coro(flat_buffer&,
                                  uint32_t,
                                  std::stop_token = {}) override
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
    assert(false &&
           "send_receive_async should not be called on server session");
  }

  bool send_stream_message(flat_buffer&& buffer) override
  {
    if (!channel_ || buffer.size() == 0)
      return false;
    auto rsv = channel_->reserve_write(buffer.size());
    if (!rsv) {
      // The ring is the client's to drain, so a full one means the client
      // stopped reading — parked, overloaded, or dead without the liveness
      // probe having noticed yet.  Say so; the producer decides what a lost
      // chunk means for its stream.
      NPRPC_LOG_ERROR(
          "SharedMemoryServerSession: ring full, dropped stream frame size={}",
          buffer.size());
      return false;
    }
    std::memcpy(rsv.data, buffer.data().data(), buffer.size());
    channel_->commit_write(rsv, buffer.size());
    return true;
  }

  bool supports_fd_passing() const noexcept override
  {
    return channel_ && channel_->supports_fd_passing();
  }

  void on_message_received(const LockFreeRingBuffer::ReadView& read_view)
  {
    // Always copy out of the ring into an owned buffer (alignment, growth,
    // and so commit_read can run before offloaded dispatch finishes).
    flat_buffer rx_buffer;
    {
      auto mb = rx_buffer.prepare(read_view.size);
      std::memcpy(mb.data(), read_view.data, read_view.size);
      rx_buffer.commit(read_view.size);
    }
    rx_buffer.fds() = channel_->take_received_fds();

    if (must_hand_off(rx_buffer))
      hand_off(std::move(rx_buffer));
    else
      process_handed_off(rx_buffer, true);
  }

  SharedMemoryServerSession(boost::asio::io_context& ioc,
                            std::unique_ptr<SharedMemoryChannel> channel)
      : Session(ioc.get_executor())
      , channel_(std::move(channel))
      , ioc_(ioc)
  {
    ctx_.remote_endpoint =
        EndPoint(EndPointType::SharedMemory, channel_->channel_id(), 0);
    ctx_.shm_channel = channel_.get();

    NPRPC_LOG_INFO("SharedMemoryServerSession created for channel: {}",
                   channel_->channel_id());
  }

  void start()
  {
    channel_->on_data_received_view =
        [this, self = shared_from_this()](
            const LockFreeRingBuffer::ReadView& read_view) {
          on_message_received(read_view);
        };
    // Fired on the read thread; the teardown joins that thread, so it has to
    // happen somewhere else.
    channel_->on_peer_lost = [this, self = shared_from_this()]() {
      boost::asio::post(ioc_, [self]() { self->on_peer_dead(); });
    };
    channel_->start_reading();
  }

  ~SharedMemoryServerSession()
  {
    NPRPC_LOG_INFO("SharedMemoryServerSession destroyed for channel: {}",
                   channel_->channel_id());
  }
};

std::shared_ptr<Session> create_shared_memory_server_session(
    boost::asio::io_context& ioc, std::unique_ptr<SharedMemoryChannel> channel)
{
  auto session =
      std::make_shared<SharedMemoryServerSession>(ioc, std::move(channel));
  session->bind_self(session);
  session->start();
  return session;
}

} // namespace nprpc::impl

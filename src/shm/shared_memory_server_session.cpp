// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include "../logging.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>

#include <atomic>
#include <chrono>
#include <deque>
#include <memory>
#include <mutex>

#include <nprpc/common.hpp>
#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/impl/session.hpp>
#include <nprpc/impl/shared_memory_channel.hpp>
#include <nprpc/impl/stream_manager.hpp>

namespace nprpc::impl {

class SharedMemoryServerSession;

// Heap token for DispatchExecutor::post (C function-pointer API).
struct ShmDrainKick {
  std::shared_ptr<SharedMemoryServerSession> session;
};

/**
 * @brief Server-side shared memory session
 *
 * Receive path (hybrid):
 *   1. Always copy the payload out of the ring (commit_read after return).
 *   2. Fast path — default POA with AllowBlockTransport and no DispatchExecutor:
 *        handle_request() on the ring thread (extreme low latency).
 *   3. Offload path — custom executor and/or NeverBlockTransport, or when a
 *      serial drain is already in flight (FIFO with the client matcher):
 *        - If the head message's POA has a DispatchExecutor: fire-and-forget
 *          executor.post(drain) from the ring (e.g. dispatch_async_f / main).
 *          No Asio hop; servant dispatch runs on that queue (is_running_on
 *          makes invoke_sync a no-op).
 *        - Else (NeverBlock without executor): asio::post(ioc) drain.
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

  // Serial work queue.  Guarded by mu_.
  std::mutex mu_;
  std::deque<flat_buffer> pending_;
  bool processing_ = false;

  // Peer liveness polling.  Cheap (a flag load plus, at most, one /proc
  // read), so the interval is set by how fast a dead client should be
  // reaped, not by cost.
  static constexpr auto kLivenessPollInterval = std::chrono::milliseconds(500);
  boost::asio::steady_timer liveness_timer_;
  std::atomic<bool> peer_dead_{false};

  // ---- message peeks -------------------------------------------------------

  static bool peek_poa_idx(const flat_buffer& rx, uint16_t& poa_idx_out)
  {
    if (rx.size() < sizeof(flat::Header))
      return false;

    const auto* header =
        static_cast<const flat::Header*>(rx.cdata().data());

    switch (header->msg_id) {
    case MessageId::FunctionCall: {
      if (rx.size() < sizeof(flat::Header) + sizeof(flat::CallHeader))
        return false;
      const auto* ch = reinterpret_cast<const flat::CallHeader*>(
          static_cast<const std::byte*>(rx.cdata().data()) +
          sizeof(flat::Header));
      poa_idx_out = ch->poa_idx;
      return true;
    }
    case MessageId::StreamInitialization: {
      constexpr size_t kMin =
          sizeof(flat::Header) + sizeof(uint64_t) + sizeof(uint16_t);
      if (rx.size() < kMin)
        return false;
      const auto* p = static_cast<const std::byte*>(rx.cdata().data()) +
                      sizeof(flat::Header) + sizeof(uint64_t);
      std::memcpy(&poa_idx_out, p, sizeof(poa_idx_out));
      return true;
    }
    case MessageId::AddReference:
    case MessageId::ReleaseObject: {
      if (rx.size() < sizeof(flat::Header) + sizeof(uint16_t))
        return false;
      const auto* p = static_cast<const std::byte*>(rx.cdata().data()) +
                      sizeof(flat::Header);
      std::memcpy(&poa_idx_out, p, sizeof(poa_idx_out));
      return true;
    }
    default:
      return false;
    }
  }

  static Poa* peek_poa(const flat_buffer& rx)
  {
    uint16_t poa_idx = 0;
    if (!peek_poa_idx(rx, poa_idx) || !g_rpc)
      return nullptr;
    auto r = g_rpc->get_poa(poa_idx);
    return r ? *r : nullptr;
  }

  // True when dispatch must leave the ring thread.
  static bool message_requires_offload(const flat_buffer& rx)
  {
    if (auto* poa = peek_poa(rx))
      return poa->requires_off_transport_dispatch();
    // No poa_idx (stream data/control) or unknown POA → stay on the ring;
    // handle_request will report Error_PoaNotExist if needed.
    return false;
  }

  // Non-empty DispatchExecutor on the target POA, if any.
  static DispatchExecutor peek_dispatch_executor(const flat_buffer& rx)
  {
    if (auto* poa = peek_poa(rx)) {
      if (poa->dispatch_executor())
        return poa->dispatch_executor();
    }
    return {};
  }

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

  void process_one(flat_buffer& rx)
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

  // Serial drain of pending_.  Runs on ring (inline path never calls this),
  // Asio ioc, or the POA DispatchExecutor queue.
  void drain_pending()
  {
    // Keep the session alive for the whole drain (GCD/Asio may outlive the
    // ring callback's stack).
    auto keep_alive = shared_from_this();
    for (;;) {
      flat_buffer rx;
      {
        std::lock_guard lock(mu_);
        if (pending_.empty()) {
          processing_ = false;
          return;
        }
        rx = std::move(pending_.front());
        pending_.pop_front();
      }
      process_one(rx);
    }
    (void)keep_alive;
  }

  // C trampoline for DispatchExecutor::post (ring → GCD / main, no wait).
  static void drain_kick_f(void* p)
  {
    std::unique_ptr<ShmDrainKick> kick(static_cast<ShmDrainKick*>(p));
    kick->session->drain_pending();
  }

  void enqueue_and_kick(flat_buffer rx)
  {
    DispatchExecutor head_ex;
    bool should_kick = false;
    {
      std::lock_guard lock(mu_);
      // Capture executor of the first message when starting a drain so the
      // whole serial batch runs on that queue (FIFO on a serial GCD queue).
      if (!processing_)
        head_ex = peek_dispatch_executor(rx);
      pending_.push_back(std::move(rx));
      if (!processing_) {
        processing_ = true;
        should_kick = true;
      }
    }
    if (!should_kick)
      return;

    // Leave the ring thread immediately so commit_read can free the slot.
    if (head_ex) {
      // Direct hop: ring → DispatchExecutor (Swift: dispatch_async_f /
      // DispatchQueue.async).  No Asio.
      auto* kick = new ShmDrainKick{shared_from_this()};
      head_ex.post(head_ex.ctx, &drain_kick_f, kick);
    } else {
      // NeverBlockTransport without a custom executor — generic C++ worker.
      boost::asio::post(ioc_, [self = shared_from_this()]() {
        self->drain_pending();
      });
    }
  }

  // ---- peer liveness -------------------------------------------------------

  void arm_liveness_timer()
  {
    liveness_timer_.expires_after(kLivenessPollInterval);
    liveness_timer_.async_wait(
        [self = shared_from_this()](const boost::system::error_code& ec) {
          if (ec) // cancelled by shutdown()
            return;
          if (self->is_closed() || self->peer_dead_.load())
            return;
          if (!self->channel_ || self->channel_->peer_alive()) {
            self->arm_liveness_timer();
            return;
          }
          self->on_peer_dead();
        });
  }

  // Tear down a session whose client is gone.  Idempotent.
  //
  // Runs on the io_context: it joins the channel's read thread (so it must
  // not be the read thread) and drops the session out of RpcImpl, which
  // leaves the timer handler's own `self` as the last reference — the
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
    try {
      liveness_timer_.cancel();
    } catch (...) {
    }

    if (channel_) {
      // Join the reader before releasing the callbacks: the read thread is
      // executing them, and they hold the strong reference that keeps this
      // session alive while a message is in flight.  stop_reading() returns
      // false only if we *are* the read thread, in which case the callbacks
      // must stay put and ~SharedMemoryChannel finishes the teardown.
      if (channel_->stop_reading()) {
        channel_->on_data_received = nullptr;
        channel_->on_data_received_view = nullptr;
      }
    }
    {
      std::lock_guard lock(mu_);
      pending_.clear();
    }
    Session::shutdown();
  }

  virtual void send_receive(flat_buffer&, uint32_t) override
  {
    assert(false && "send_receive should not be called on server session");
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

    // Prefer ring-thread dispatch for cheap servants (default POA).  Must
    // not interleave with an in-flight offload drain — client matches FIFO.
    bool try_inline = !message_requires_offload(rx_buffer);
    if (try_inline) {
      std::lock_guard lock(mu_);
      if (processing_ || !pending_.empty())
        try_inline = false;
    }

    if (try_inline) {
      process_one(rx_buffer);
      return;
    }

    enqueue_and_kick(std::move(rx_buffer));
  }

  SharedMemoryServerSession(boost::asio::io_context& ioc,
                            std::unique_ptr<SharedMemoryChannel> channel)
      : Session(ioc.get_executor())
      , channel_(std::move(channel))
      , ioc_(ioc)
      , liveness_timer_(ioc)
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
    channel_->start_reading();
    arm_liveness_timer();
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

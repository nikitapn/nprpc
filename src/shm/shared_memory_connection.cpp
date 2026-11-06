// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include <chrono>
#include <iostream>

#include <nprpc/common.hpp>
#include <nprpc/impl/shared_memory_connection.hpp>
#include <nprpc/impl/shared_memory_listener.hpp>
#include <nprpc/impl/stream_manager.hpp>

#include "logging.hpp"

namespace nprpc::impl {

namespace {

// A zero timeout means "wait indefinitely", as it does on the other
// transports; steady_clock so a wall-clock jump cannot expire a request.
[[nodiscard]] inline std::chrono::steady_clock::time_point
deadline_from_now(uint32_t timeout_ms) noexcept
{
  if (timeout_ms == 0)
    return {};
  return std::chrono::steady_clock::now() +
         std::chrono::milliseconds(timeout_ms);
}

} // namespace

void SharedMemoryConnection::timeout_action()
{
  // For shared memory, timeout means the other side is unresponsive
  // close();
}

void SharedMemoryConnection::fail_all_pending(
    const boost::system::error_code& ec)
{
  std::deque<std::shared_ptr<IOWork>> pending;
  {
    std::lock_guard lock(mutex_);
    pending.swap(wq_);
  }
  // Outside the lock: on_failed() hands control to the caller's completion
  // handler, which may well come straight back into this connection.
  for (auto& w : pending)
    w->on_failed(ec);
}

void SharedMemoryConnection::on_server_dead()
{
  if (server_dead_.exchange(true))
    return;

  NPRPC_LOG_WARN("SharedMemoryConnection: server of channel {} is gone "
                 "(pid {}), failing outstanding work",
                 channel_->channel_id(), channel_->peer_process().pid);

  // On the read thread, so do the waking here — a caller blocked in
  // send_receive() is waiting for a reply that can never arrive, and
  // nothing else will ever notice on a transport with no socket to break.
  fail_all_pending(boost::asio::error::connection_aborted);

  if (ctx_.stream_manager)
    ctx_.stream_manager->cancel_all();

  // The rest joins the read thread, so it cannot run here.  If nothing ever
  // runs this io_context the session simply stays registered — everyone who
  // was waiting has already been released above.
  if (auto self = weak_from_this().lock()) {
    boost::asio::post(ioc_, [self]() {
      // close() before shutdown(): shutdown() marks the session closed, and
      // close() is what actually drops it out of RpcImpl.
      if (!self->is_closed())
        self->close();
      self->shutdown();
    });
  }
}

void SharedMemoryConnection::shutdown()
{
  if (channel_) {
    // Join the reader before releasing the callbacks it is executing.
    if (channel_->stop_reading()) {
      channel_->on_data_received = nullptr;
      channel_->on_data_received_view = nullptr;
      channel_->on_peer_lost = nullptr;
      channel_->on_periodic_poll = nullptr;
    }
  }
  Session::shutdown();
}

void SharedMemoryConnection::enqueue(std::shared_ptr<IOWork> w)
{
  // mutex_ must be held by the caller.
  const bool expires = w->deadline != std::chrono::steady_clock::time_point{};
  wq_.push_back(std::move(w));
  // The deadline was stamped on the work before it got here, and
  // sweep_expired_requests() enforces it off the hot path — once the tick it
  // runs on is going.  An idle channel's reader sleeps without one, so say
  // so; after the insert, which is what the sweep will look for.
  if (expires && channel_)
    channel_->need_ticks();
}

namespace {
// How long a request waits for a full ring before the call fails. The server's
// reader normally drains it at once; the connection stays up either way.
constexpr std::chrono::milliseconds kRequestSpaceWait{100};
} // namespace

// A stub may have built the request in a ring reservation. One that won't be sent
// has to be released, or the ring stalls at that slot.
void SharedMemoryConnection::discard_reservation(flat_buffer& buffer)
{
  if (!buffer.is_view_mode() || !buffer.has_write_reservation())
    return;
  LockFreeRingBuffer::WriteReservation reservation;
  reservation.data = buffer.data_ptr();
  reservation.max_size = buffer.max_size();
  reservation.slot_idx = buffer.reservation_write_idx();
  reservation.valid = true;
  channel_->abort_write(reservation);
  buffer.release_write_view();
}

void SharedMemoryConnection::deliver_reply(const void* data, size_t size,
                                           std::vector<UniqueFd>&& fds)
{
  const auto* hdr = static_cast<const impl::flat::Header*>(data);

  std::lock_guard lock(mutex_);
  // Almost always the oldest: a session's calls run in order on the server.
  auto it = wq_.begin();
  while (it != wq_.end() && (*it)->call_id != hdr->request_id)
    ++it;
  if (it == wq_.end()) {
    NPRPC_LOG_WARN("SharedMemoryConnection: dropped a reply nobody is waiting "
                   "for, msg_id={} request_id={} channel={}",
                   static_cast<unsigned>(hdr->msg_id), hdr->request_id,
                   channel_->channel_id());
    return;
  }

  auto& buffer = (*it)->buffer();
  buffer.consume(buffer.size());
  auto mb = buffer.prepare(size);
  std::memcpy(mb.data(), data, size);
  buffer.commit(size);
  buffer.fds() = std::move(fds);

  (*it)->on_executed();
  wq_.erase(it);
}

bool SharedMemoryConnection::sweep_expired_requests()
{
  const auto now = std::chrono::steady_clock::now();
  std::vector<std::shared_ptr<IOWork>> expired;
  bool pending = false;

  {
    std::lock_guard lock(mutex_);
    for (auto it = wq_.begin(); it != wq_.end();) {
      auto& w = *it;
      if (w->deadline == std::chrono::steady_clock::time_point{} || now < w->deadline) {
        pending = pending || w->deadline != std::chrono::steady_clock::time_point{};
        ++it;
        continue;
      }
      // Out of the queue before the caller hears: its buffer may be on a stack
      // about to go away, and a late reply must not find it.
      expired.push_back(std::move(w));
      it = wq_.erase(it);
    }
  }

  // Outside the lock — on_failed() runs the caller's completion handler.
  for (auto& w : expired) {
    NPRPC_LOG_WARN("SharedMemoryConnection: request on channel {} timed out",
                   channel_->channel_id());
    w->on_failed(boost::asio::error::timed_out);
  }
  return pending;
}

boost::system::error_code SharedMemoryConnection::do_send_receive(flat_buffer& buffer, uint32_t timeout_ms)
{
  // Protocol:
  //  1. Request is fully serialised (possibly into a zero-copy reservation).
  //  2. Enqueue the work under mutex_, so a reply can't beat it to the queue.
  //  3. Commit the ring slot on the *caller* thread — never waits on any
  //     other request's progress.  FIFO ring consumption stays free of
  //     commit-order deadlocks.
  //  4. Wait for the reply carrying this call's id.

  struct work_impl : IOWork {
    flat_buffer& buf;
    SharedMemoryConnection& this_;
    uint32_t timeout_ms;

    std::atomic_bool done{false};
    boost::system::error_code result;

    void on_failed(const boost::system::error_code& ec) noexcept override
    {
      result = ec;
      done.store(true, std::memory_order_release);
      done.notify_one();
    }

    void on_executed() noexcept override
    {
      result = boost::system::error_code{};
      done.store(true, std::memory_order_release);
      done.notify_one();
    }

    boost::system::error_code wait()
    {
      done.wait(false);
      return result;
    }

    flat_buffer& buffer() noexcept override { return buf; }

    work_impl(flat_buffer& _buf,
              SharedMemoryConnection& _this_,
              uint32_t _timeout_ms)
        : buf(_buf)
        , this_(_this_)
        , timeout_ms(_timeout_ms)
    {
    }
  };

  if (server_dead_.load()) {
    // No point claiming a ring slot nobody will ever read.
    return boost::asio::error::connection_aborted;
  }

  if (buffer.size() == 0 || buffer.size() > max_message_size ||
      buffer.size() > channel_->max_message_size()) {
    NPRPC_LOG_ERROR("SharedMemoryConnection: request of {} bytes exceeds the limit of {}",
                    buffer.size(), channel_->max_message_size());
    discard_reservation(buffer);
    return boost::asio::error::message_size;
  }

  const uint32_t call_id = call_id_for(buffer);
  static_cast<impl::flat::Header*>(buffer.data().data())->request_id = call_id;

  // Peek the slot we will use.  For zero-copy the slot is already claimed;
  // for the copy path we claim inside commit_request — but we need the
  // slot *before* enqueue so the queue is ordered correctly.  So for the
  // copy path we claim+copy here and only commit after enqueue.
  LockFreeRingBuffer::WriteReservation pending_copy_rsv{};
  bool zero_copy = buffer.is_view_mode() && buffer.has_write_reservation();

  if (!zero_copy) {
    pending_copy_rsv =
        channel_->reserve_write_waiting(buffer.size(), kRequestSpaceWait, server_dead_);
    if (!pending_copy_rsv) {
      NPRPC_LOG_ERROR("SharedMemoryConnection: no room in the ring for a request of {} "
                      "bytes after waiting {} ms",
                      buffer.size(), kRequestSpaceWait.count());
      return boost::asio::error::no_buffer_space;
    }
    std::memcpy(pending_copy_rsv.data, buffer.data().data(), buffer.size());
  }

  auto w = std::make_shared<work_impl>(buffer, *this, timeout_ms);
  w->call_id = call_id;
  w->deadline = deadline_from_now(timeout_ms);

  // Zero-copy path: the request bytes already live in the ring reservation.
  // Drop the write-view *before* publishing the slot and *before* a response
  // can arrive.  Otherwise the read thread may memcpy the reply into the
  // still-attached send-ring view, and release_write_view() then wipes that
  // reply — caller sees buf.size()==0 and throws ExceptionBadInput.
  LockFreeRingBuffer::WriteReservation pending_zc_rsv{};
  size_t pending_zc_size = 0;
  if (zero_copy) {
    pending_zc_rsv.data = buffer.data_ptr();
    pending_zc_rsv.max_size = buffer.max_size();
    pending_zc_rsv.slot_idx = buffer.reservation_write_idx();
    pending_zc_rsv.valid = true;
    pending_zc_size = buffer.size();
    buffer.release_write_view(); // buffer is now empty owned; safe for reply
  }

  // Enqueue under the same mutex the response callback uses, so a reply
  // cannot land while we are not yet in the queue.
  {
    std::lock_guard lock(mutex_);
    enqueue(w);
  }

  // Commit after enqueue: the ring consumer may now see this slot.
  if (zero_copy) {
    channel_->commit_write(pending_zc_rsv, pending_zc_size, buffer.fds());
  } else {
    channel_->commit_write(pending_copy_rsv, buffer.size(), buffer.fds());
  }

  auto ec = w->wait();

  if (!ec) {
    return {};
  }

  // A timeout is this request's problem, not the connection's — as on
  // WebSocket, the session keeps serving other calls.  Any other error means
  // the transport is done.
  if (ec != boost::asio::error::timed_out) {
    fail(ec, "send_receive");
    close();
  }
  return ec;
}

void SharedMemoryConnection::send_receive_async(
    flat_buffer&& buffer,
    std::optional<std::function<void(const boost::system::error_code&,
                                     flat_buffer&)>>&& completion_handler,
    uint32_t timeout_ms)
{
  assert(*(uint32_t*)buffer.data().data() == buffer.size());
  const uint32_t call_id = call_id_for(buffer);
  static_cast<impl::flat::Header*>(buffer.data().data())->request_id = call_id;

  struct work_impl : IOWork {
    flat_buffer buf;
    SharedMemoryConnection& this_;
    uint32_t timeout_ms;
    std::optional<
        std::function<void(const boost::system::error_code&, flat_buffer&)>>
        handler;

    void on_failed(const boost::system::error_code& ec) noexcept override
    {
      if (handler)
        handler.value()(ec, buf);
    }

    void on_executed() noexcept override
    {
      if (handler)
        handler.value()(boost::system::error_code{}, buf);
    }

    flat_buffer& buffer() noexcept override { return buf; }

    work_impl(flat_buffer&& _buf,
              SharedMemoryConnection& _this_,
              std::optional<std::function<void(const boost::system::error_code&,
                                               flat_buffer&)>>&& _handler,
              uint32_t _timeout_ms)
        : buf(std::move(_buf))
        , this_(_this_)
        , timeout_ms(_timeout_ms)
        , handler(std::move(_handler))
    {
    }
  };

  const auto request_size = buffer.size();
  // Stubs may have built the request in a ring reservation already. That slot is
  // the request: claiming another and leaving it uncommitted stalls the ring.
  // As on the blocking path, detach it first, so the reply can't land in it.
  const bool zero_copy = buffer.is_view_mode() && buffer.has_write_reservation();
  LockFreeRingBuffer::WriteReservation zero_copy_rsv{};
  if (zero_copy) {
    zero_copy_rsv.data = buffer.data_ptr();
    zero_copy_rsv.max_size = buffer.max_size();
    zero_copy_rsv.slot_idx = buffer.reservation_write_idx();
    zero_copy_rsv.valid = true;
    buffer.release_write_view();
  }

  auto w = std::make_shared<work_impl>(
      std::move(buffer), *this, std::move(completion_handler), timeout_ms);

  if (server_dead_.load()) {
    w->on_failed(boost::asio::error::connection_aborted);
    return;
  }

  auto rsv = zero_copy_rsv;
  if (!zero_copy) {
    if (request_size > channel_->max_message_size()) {
      w->on_failed(boost::system::error_code(boost::asio::error::message_size,
                                             boost::system::system_category()));
      return;
    }
    rsv = channel_->reserve_write_waiting(request_size, kRequestSpaceWait, server_dead_);
    if (!rsv) {
      w->on_failed(boost::system::error_code(boost::asio::error::no_buffer_space,
                                             boost::system::system_category()));
      return;
    }
    std::memcpy(rsv.data, w->buf.data().data(), request_size);
  }
  w->call_id = call_id;
  w->deadline = deadline_from_now(timeout_ms);

  {
    std::lock_guard lock(mutex_);
    enqueue(w);
  }

  channel_->commit_write(rsv, request_size, w->buf.fds());
}

SharedMemoryConnection::SharedMemoryConnection(const EndPoint& endpoint,
                                               boost::asio::io_context& ioc)
    : Session(ioc.get_executor())
    , ioc_(ioc)
{
  ctx_.remote_endpoint = endpoint;
  timeout_timer_.expires_after(std::chrono::system_clock::duration::max());

  // Parse endpoint to extract listener name
  // Expected format: mem://listener_name (no port needed for shared memory)
  std::string listener_name(endpoint.memory_channel_id());
  if (listener_name.empty()) {
    throw nprpc::Exception(
        "Invalid shared memory endpoint: missing listener name");
  }

  // Connect to the listener, which will establish a dedicated channel
  try {
    channel_ = connect_to_shared_memory_listener(ioc_, listener_name);
  } catch (const std::exception& e) {
    std::string error_msg = "Could not connect to shared memory listener: ";
    error_msg += e.what();
    throw nprpc::Exception(error_msg.c_str());
  }

  // Expose the channel on the session context so prepare_zero_copy_buffer
  // can reserve directly without re-looking up the connection.
  ctx_.shm_channel = channel_.get();

  // Demux inbound frames:
  //  - Stream control/data (chunks, complete, error, cancel, window) go to
  //    Session::handle_request → stream_manager (no wq_ entry).
  //  - RPC replies go to the call whose id they carry.
  channel_->on_data_received_view =
      [this](const LockFreeRingBuffer::ReadView& read_view) {
        if (read_view.size < sizeof(impl::flat::Header)) {
          NPRPC_LOG_ERROR(
              "SharedMemoryConnection: RX too small: size={} channel={}",
              read_view.size, channel_->channel_id());
          return;
        }

        const auto* hdr =
            reinterpret_cast<const impl::flat::Header*>(read_view.data);
        const auto msg_id = hdr->msg_id;

        // Stream frames (not StreamInitialization — that is a request/reply
        // RPC and is matched via wq_ on the initiator side).
        const bool is_stream_frame =
            msg_id == MessageId::StreamDataChunk ||
            msg_id == MessageId::StreamCompletion ||
            msg_id == MessageId::StreamError ||
            msg_id == MessageId::StreamCancellation ||
            msg_id == MessageId::StreamWindowUpdate;

        if (is_stream_frame) {
          flat_buffer rx;
          auto mb = rx.prepare(read_view.size);
          std::memcpy(mb.data(), read_view.data, read_view.size);
          rx.commit(read_view.size);
          flat_buffer tx; // unused for fire-and-forget stream frames
          handle_request(rx, tx);
          return;
        }

        deliver_reply(read_view.data, read_view.size, channel_->take_received_fds());
      };

  // Set up data receive handler (copy path — same demux rules)
  channel_->on_data_received = [this](std::vector<char>&& data) {
    if (data.size() < sizeof(impl::flat::Header)) {
      NPRPC_LOG_ERROR("SharedMemoryConnection: Message too small: {}", data.size());
      return;
    }

    const auto* hdr =
        reinterpret_cast<const impl::flat::Header*>(data.data());
    const auto msg_id = hdr->msg_id;
    const bool is_stream_frame =
        msg_id == MessageId::StreamDataChunk ||
        msg_id == MessageId::StreamCompletion ||
        msg_id == MessageId::StreamError ||
        msg_id == MessageId::StreamCancellation ||
        msg_id == MessageId::StreamWindowUpdate;

    if (is_stream_frame) {
      flat_buffer rx;
      auto mb = rx.prepare(data.size());
      std::memcpy(mb.data(), data.data(), data.size());
      rx.commit(data.size());
      flat_buffer tx;
      handle_request(rx, tx);
      return;
    }

    deliver_reply(data.data(), data.size(), channel_->take_received_fds());
  };

  // A shared-memory server that dies leaves the rings mapped and silent, so
  // the read thread probes it and reports here.
  channel_->on_peer_lost = [this]() { on_server_dead(); };

  // Same tick expires requests whose reply never came — see
  // sweep_expired_requests() for why this is not a timer per request.
  channel_->on_periodic_poll = [this]() { return sweep_expired_requests(); };

  // All callbacks are wired — now it is safe to start the receive thread.
  channel_->start_reading();

  start_timeout_timer();
}

SharedMemoryConnection::~SharedMemoryConnection()
{
  // Stop the reader before anything else goes away: its callbacks capture
  // `this` and touch members (mutex_, wq_) that are destroyed before
  // channel_ is, so letting it run into the destructor is a use-after-free.
  if (channel_)
    channel_->stop_reading();
  if (!is_closed())
    close();
}

bool SharedMemoryConnection::send_stream_message(flat_buffer&& buffer)
{
  // Fire-and-forget: write into the send ring without enqueuing on wq_.
  // Stream frames are not request/response paired.  The ring's answer is
  // still worth passing back — it is how a client writing into a bidi
  // stream learns the server stopped reading.
  return send_datagram(std::move(buffer));
}

bool SharedMemoryConnection::send_datagram(flat_buffer&& buffer)
{
  // Used by `[unreliable]` RPC methods and by stream frames. Must not put a
  // waiter on wq_: the peer deliberately sends no reply, and a waiter would
  // either time out (and log) or sit forever and steal later replies that
  // are matched by queue position on this transport.
  if (!channel_ || buffer.size() == 0)
    return false;
  if (server_dead_.load())
    return false;

  auto rsv = channel_->reserve_write(buffer.size());
  if (!rsv) {
    NPRPC_LOG_ERROR(
        "SharedMemoryConnection: ring full, dropped fire-and-forget frame "
        "size={}",
        buffer.size());
    return false;
  }
  std::memcpy(rsv.data, buffer.data().data(), buffer.size());
  channel_->commit_write(rsv, buffer.size(), buffer.fds());
  return true;
}

bool SharedMemoryConnection::prepare_write_buffer(flat_buffer& buffer,
                                                  size_t max_size,
                                                  const EndPoint* endpoint)
{
  // MPSC ring: each try_reserve_write claims a unique slot+payload via CAS,
  // so concurrent callers are safe.  Callers must pass the exact final wire
  // size (measured before serialisation).
  //
  // IMPORTANT: claiming here is fine (and required for zero-copy), but the
  // reservation must be committed (or aborted) on the *caller* thread after
  // serialisation and after the work is enqueued — never from a response-
  // serialised work queue.  See send_receive().
  auto reservation = channel_->reserve_write(max_size);
  if (!reservation)
    return false;

  const EndPoint* ep =
      endpoint ? endpoint : &ctx_.remote_endpoint;
  buffer.set_view(reservation.data, 0, reservation.max_size, ep,
                  reservation.slot_idx, true, channel_.get());
  return true;
}

} // namespace nprpc::impl

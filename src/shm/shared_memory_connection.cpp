// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include <iostream>

#include <nprpc/common.hpp>
#include <nprpc/impl/shared_memory_connection.hpp>
#include <nprpc/impl/shared_memory_listener.hpp>

#include "logging.hpp"

namespace nprpc::impl {

namespace {

// Wrap-aware ordering for the 16-bit ring slot counter.  Valid when both
// slots are in-flight (distance < 2^15, guaranteed by kRingSlots ≪ 2^15).
// Returns true if `a` is strictly before `b` in ring order.
[[nodiscard]] inline bool slot_before(uint64_t a, uint64_t b) noexcept
{
  return static_cast<int16_t>(static_cast<uint16_t>(a) -
                              static_cast<uint16_t>(b)) < 0;
}

} // namespace

void SharedMemoryConnection::timeout_action()
{
  // For shared memory, timeout means the other side is unresponsive
  // std::cerr << "SharedMemoryConnection timeout" << std::endl;
  // close();
}

void SharedMemoryConnection::enqueue_ordered(std::shared_ptr<IOWork> w)
{
  // mutex_ must be held by the caller.
  auto it = wq_.begin();
  if (w->has_slot_order) {
    for (; it != wq_.end(); ++it) {
      if (!(*it)->has_slot_order)
        break; // unordered entries (shouldn't happen on this connection)
      if (slot_before(w->slot_idx, (*it)->slot_idx))
        break;
    }
  } else {
    it = wq_.end(); // push_back
  }

  const bool becomes_front = (it == wq_.begin());
  wq_.insert(it, std::move(w));

  // Arm the timeout for the request that is next to receive a response.
  // Messages are already committed by the time responses can arrive, so
  // operator() only sets the deadline.
  if (becomes_front)
    (*wq_.front())();
}

void SharedMemoryConnection::send_receive(flat_buffer& buffer,
                                          uint32_t timeout_ms)
{
  // Protocol (Option B — commit-before-queue, slot-ordered responses):
  //  1. Request is fully serialised (possibly into a zero-copy reservation).
  //  2. Enqueue the work ordered by slot_idx under mutex_ (so a response
  //     can never race an empty/unordered queue).
  //  3. Commit the ring slot on the *caller* thread — never waits on any
  //     other request's progress.  FIFO ring consumption stays free of
  //     commit-order deadlocks.
  //  4. Wait for the matching response (delivered when this work is at
  //     the front of wq_ and the next ring message arrives).

  struct work_impl : IOWork {
    flat_buffer& buf;
    SharedMemoryConnection& this_;
    uint32_t timeout_ms;

    std::atomic_bool done{false};
    boost::system::error_code result;

    void operator()() noexcept override
    {
      // Already committed; only arm the per-request timeout.
      this_.set_timeout(timeout_ms);
    }

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

  if (buffer.size() == 0 || buffer.size() > max_message_size) {
    fail(boost::system::error_code(boost::asio::error::message_size,
                                   boost::system::system_category()),
         "send_receive");
    close();
    throw nprpc::ExceptionCommFailure();
  }

  // Peek the slot we will use.  For zero-copy the slot is already claimed;
  // for the copy path we claim inside commit_request — but we need the
  // slot *before* enqueue so the queue is ordered correctly.  So for the
  // copy path we claim+copy here and only commit after enqueue.
  LockFreeRingBuffer::WriteReservation pending_copy_rsv{};
  uint64_t slot = 0;
  bool zero_copy = buffer.is_view_mode() && buffer.has_write_reservation();

  if (zero_copy) {
    slot = buffer.reservation_write_idx();
  } else {
    pending_copy_rsv = channel_->reserve_write(buffer.size());
    if (!pending_copy_rsv) {
      fail(boost::system::error_code(boost::asio::error::no_buffer_space,
                                     boost::system::system_category()),
           "send_receive");
      close();
      throw nprpc::ExceptionCommFailure();
    }
    std::memcpy(pending_copy_rsv.data, buffer.data().data(), buffer.size());
    slot = pending_copy_rsv.slot_idx;
  }

  auto w = std::make_shared<work_impl>(buffer, *this, timeout_ms);
  w->slot_idx = slot;
  w->has_slot_order = true;

  // Enqueue under the same mutex the response callback uses, so a reply
  // cannot land while we are not yet in the queue.
  {
    std::lock_guard lock(mutex_);
    enqueue_ordered(w);
  }

  // Commit after enqueue: the ring consumer may now see this slot, but
  // our work is already positioned so the response is matched correctly.
  if (zero_copy) {
    LockFreeRingBuffer::WriteReservation rsv;
    rsv.data = buffer.data_ptr();
    rsv.max_size = buffer.max_size();
    rsv.slot_idx = slot;
    rsv.valid = true;
    channel_->commit_write(rsv, buffer.size());
    buffer.release_write_view();
  } else {
    channel_->commit_write(pending_copy_rsv, buffer.size());
  }

  auto ec = w->wait();

  if (!ec) {
    return;
  }

  fail(ec, "send_receive");
  close();
  throw nprpc::ExceptionCommFailure();
}

void SharedMemoryConnection::send_receive_async(
    flat_buffer&& buffer,
    std::optional<std::function<void(const boost::system::error_code&,
                                     flat_buffer&)>>&& completion_handler,
    uint32_t timeout_ms)
{
  assert(*(uint32_t*)buffer.data().data() == buffer.size());

  struct work_impl : IOWork {
    flat_buffer buf;
    SharedMemoryConnection& this_;
    uint32_t timeout_ms;
    std::optional<
        std::function<void(const boost::system::error_code&, flat_buffer&)>>
        handler;

    void operator()() noexcept override
    {
      this_.set_timeout(timeout_ms);
    }

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

  auto w = std::make_shared<work_impl>(
      std::move(buffer), *this, std::move(completion_handler), timeout_ms);

  // Claim slot on the caller thread (async path is currently always copy).
  auto rsv = channel_->reserve_write(w->buf.size());
  if (!rsv) {
    w->on_failed(boost::system::error_code(boost::asio::error::no_buffer_space,
                                           boost::system::system_category()));
    return;
  }
  std::memcpy(rsv.data, w->buf.data().data(), w->buf.size());
  w->slot_idx = rsv.slot_idx;
  w->has_slot_order = true;

  {
    std::lock_guard lock(mutex_);
    enqueue_ordered(w);
  }

  channel_->commit_write(rsv, w->buf.size());
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
  //  - RPC replies match wq_.front() by ring-slot ordering.
  //
  // Response ↔ request matching relies on wq_ being ordered by ring slot
  // (enqueue_ordered) and commits happening on the caller thread before the
  // server can reply — so the next *RPC* ring message always belongs to
  // wq_.front().  Stream frames may interleave and must not touch wq_.
  channel_->on_data_received_view =
      [this](const LockFreeRingBuffer::ReadView& read_view) {
        if (read_view.size < sizeof(impl::flat::Header)) {
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

        std::lock_guard lock(mutex_);
        if (wq_.empty()) {
          return; // read_loop will still call commit_read
        }

        auto& current_buffer = current_rx_buffer();
        current_buffer.consume(current_buffer.size());
        auto mb = current_buffer.prepare(read_view.size);
        std::memcpy(mb.data(), read_view.data, read_view.size);
        current_buffer.commit(read_view.size);

        (*wq_.front()).on_executed();
        pop_and_execute_next_task();
      };

  // Set up data receive handler (copy path — same demux rules)
  channel_->on_data_received = [this](std::vector<char>&& data) {
    if (data.size() < sizeof(impl::flat::Header)) {
      std::cerr << "SharedMemoryConnection: Message too small: " << data.size()
                << std::endl;
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

    std::lock_guard lock(mutex_);
    if (wq_.empty()) {
      std::cerr << "SharedMemoryConnection: Received unsolicited response"
                << std::endl;
      return;
    }

    auto& current_buffer = current_rx_buffer();
    current_buffer.consume(current_buffer.size());
    auto mb = current_buffer.prepare(data.size());
    std::memcpy(mb.data(), data.data(), data.size());
    current_buffer.commit(data.size());

    (*wq_.front()).on_executed();
    pop_and_execute_next_task();
  };

  // All callbacks are wired — now it is safe to start the receive thread.
  channel_->start_reading();

  start_timeout_timer();
}

SharedMemoryConnection::~SharedMemoryConnection() { close(); }

void SharedMemoryConnection::send_stream_message(flat_buffer&& buffer)
{
  // Fire-and-forget: write into the send ring without enqueuing on wq_.
  // Stream frames are not request/response paired.
  if (!channel_ || buffer.size() == 0)
    return;
  auto rsv = channel_->reserve_write(buffer.size());
  if (!rsv) {
    NPRPC_LOG_ERROR(
        "SharedMemoryConnection: ring full, dropped stream frame size={}",
        buffer.size());
    return;
  }
  std::memcpy(rsv.data, buffer.data().data(), buffer.size());
  channel_->commit_write(rsv, buffer.size());
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

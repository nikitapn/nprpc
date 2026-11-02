// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/system_timer.hpp>
#include <boost/date_time/posix_time/ptime.hpp>
#include <boost/system/error_code.hpp>

#include <chrono>
#include <deque>
#include <memory>
#include <mutex>
#include <stop_token>
#include <unordered_map>
#include <utility>

#include <nprpc/basic.hpp>
#include <nprpc/common.hpp>
#include <nprpc/flat_buffer.hpp>
#include <nprpc/session_context.h>
#include <nprpc/task.hpp>
#include <nprpc/result.hpp>

namespace nprpc {
struct DispatchExecutor;
namespace impl { class DeferredCall; }
}

namespace nprpc::impl {

class Session
{
protected:
  SessionContext ctx_;

  // Indirection cell shared with StreamManager's send callbacks (set up in
  // Session::Session(), before any shared_ptr owns *this — shared_from_this()
  // is not usable there). bind_self() fills in the real weak_ptr once the
  // caller has wrapped the concrete session in a shared_ptr; the callbacks
  // capture this cell (not `this`) so they can safely detect — via
  // weak_ptr::lock(), with no use-after-free/TOCTOU window — that the
  // session has since been destroyed, instead of dispatching a virtual call
  // through a dangling pointer. See nprpc issue: pure-virtual-method-called
  // crash when writing to a stream after its session died uncleanly.
  std::shared_ptr<std::weak_ptr<Session>> self_cell_ =
      std::make_shared<std::weak_ptr<Session>>();

  // Owns the StreamManager ctx_.stream_manager points at. External holders
  // of that raw pointer (the Swift NPRPCStreamWriter/NPRPCStreamReader, via
  // the C bridge) take their own share via StreamManager::external_retain()/
  // external_release() — see nprpc issue about the StreamManager leak this
  // replaces. Session dropping its own share here does NOT free the object
  // out from under a still-alive Swift-side writer/reader; it just stops
  // being one of the owners. self_cell_ above is what tells StreamManager
  // the *session* has died, independent of who still owns the object itself.
  std::shared_ptr<StreamManager> stream_manager_owner_;

  boost::asio::system_timer timeout_timer_;
  boost::asio::system_timer inactive_timer_;
  std::chrono::system_clock::duration timeout_ =
      std::chrono::milliseconds(1000);

  std::atomic<bool> closed_{false};

  void close();
  bool is_closed() { return closed_.load(); }

  virtual void timeout_action() = 0;

  // Shared implementation for handle_request / handle_request_async.
  bool handle_request_body(nprpc::flat_buffer& rx_buffer,
                           nprpc::flat_buffer& tx_buffer);

  // ---- Handing calls off the transport thread ------------------------------
  //
  // A POA with a DispatchExecutor or NeverBlockTransport must not hold up the
  // transport, so its messages run from a per-session FIFO instead, and the
  // reader goes straight back to reading. A session still runs one message at
  // a time: everything that arrives while the queue is busy joins it, which
  // keeps ctx_ to one user and calls in order.
  //
  //   if (must_hand_off(rx)) hand_off(std::move(rx));
  //   else process_handed_off(rx, true);

  // Call on the reader, before dispatching rx.
  bool must_hand_off(const nprpc::flat_buffer& rx);
  // Needs bind_self(); an unbound session runs the message here instead.
  void hand_off(nprpc::flat_buffer&& rx, bool wants_reply = true);
  // Forgets the queued messages. The one already running finishes.
  void clear_handoffs();

  // Runs one message and sends its reply. Handed-off messages get here on the
  // executor, so sending must be safe away from the transport's own thread. A
  // message with wants_reply false never gets a reply, whatever the servant
  // leaves behind. Must not throw past the transport's own error handling;
  // anything that does is logged and the message dropped.
  virtual void process_handed_off(nprpc::flat_buffer& rx, bool wants_reply);

  // Sends the reply to a `[coroutine]` call, from any thread.
  virtual void send_deferred_reply(flat_buffer&& reply)
  {
    send_main_stream_message(std::move(reply));
  }

  // Whether this transport can send a reply after handle_request returns, from
  // any thread, through send_deferred_reply. Otherwise it waits for a
  // `[coroutine]` call to finish, holding its thread.
  virtual bool supports_deferred_replies() const noexcept { return false; }

  // monitor::Transport for telemetry on deferred calls; -1 if this one isn't measured.
  int telemetry_transport_ = -1;

  // A request id, never zero and unique on this session. Every transport draws
  // from here, so an id reserved ahead of sending can't collide with another.
  uint32_t next_call_id() noexcept
  {
    uint32_t id;
    do id = next_call_id_.fetch_add(1, std::memory_order_relaxed); while (id == 0);
    return id;
  }
  // The id to send this message under.
  uint32_t call_id_for(const nprpc::flat_buffer& buffer) noexcept
  {
    const auto id = buffer.call_id();
    return id ? id : next_call_id();
  }

public:
  // Reserves a request id for a call about to be sent, to pass to cancel_call().
  uint32_t reserve_call_id(nprpc::flat_buffer& buffer) noexcept
  {
    const auto id = next_call_id();
    buffer.set_call_id(id);
    return id;
  }
  // A `[coroutine]` call left dispatch without a reply. Reads and clears.
  bool take_call_deferred() noexcept { return std::exchange(call_deferred_, false); }

  // Tells the server this call is no longer wanted. Its reply still comes.
  NPRPC_API void cancel_call(uint32_t call_id);
  // The client is gone: stops the running call's token, and those of calls not
  // yet started. They still run, since some may be one-way.
  void notify_peer_gone();

  // Throwing API used by generated stubs: ExceptionTimeout if no reply came in
  // time, and ExceptionCommFailure for any other transport failure.
  NPRPC_API virtual void send_receive(flat_buffer& buffer, uint32_t timeout_ms);

  // Coroutine variant — suspends the caller until the response arrives, then
  // resumes it on this session's executor. A stop request ends the wait at once
  // with OperationCancelled and tells the server, and the reply is dropped when it
  // comes. Don't make a blocking call from the resumed coroutine: on a socket
  // session the executor is the strand that has to deliver that call's reply.
  // Built on send_receive_async(), so a transport only needs to provide that.
  NPRPC_API virtual nprpc::Task<> send_receive_coro(flat_buffer& buffer,
                                                    uint32_t timeout_ms,
                                                    std::stop_token st = {});

  virtual void send_receive_async(
      flat_buffer&& buffer,
      std::optional<std::function<void(const boost::system::error_code&,
                                       flat_buffer&)>>&& completion_handler,
      uint32_t timeout_ms) = 0;

  /**
   * @brief Send unreliable datagram (fire-and-forget, no response expected)
   *
   * Default implementation falls back to send_receive_async with no handler.
   * QUIC sessions override this to use DATAGRAM extension.
   *
   * @param buffer Message buffer to send (moved)
   * @return true if datagram was sent successfully
   */
  virtual bool send_datagram(flat_buffer&& buffer)
  {
    // Default fallback for transports without a native datagram path.
    // Prefer overrides that write without a reply waiter (SHM, QUIC): a
    // waiter here is wrong for IDL `[unreliable]` methods, which never
    // get a reply. timeout_ms=0 means "never expire", so a missing reply
    // would hang the reply-matching queue forever on SHM.
    send_receive_async(std::move(buffer), std::nullopt, 0);
    return true;
  }

  void set_timeout(uint32_t timeout_ms)
  {
    timeout_ = std::chrono::milliseconds(timeout_ms);
    timeout_timer_.expires_after(timeout_);
  }

  // monitors every asynchronous operation
  // and calls timeout_action() if the operation is not completed
  // within the specified timeout
  void start_timeout_timer() noexcept
  {
    if (is_closed())
      return;

    const auto now = std::chrono::system_clock::now();
    if (timeout_timer_.expiry() <= now) {
      timeout_action();

      try {
        timeout_timer_.expires_after(
            std::chrono::system_clock::duration::max());
      } catch (boost::system::system_error& ec) {
        // nothing we can do here
      }
    }
    // Runs on cancellation too, since set_timeout() relies on that to re-arm,
    // including the cancellation by this session's own destructor.
    timeout_timer_.async_wait([cell = self_cell_](boost::system::error_code const&) {
      if (auto self = cell->lock())
        self->start_timeout_timer();
    });
  }
  const EndPoint& remote_endpoint() const noexcept
  {
    return ctx_.remote_endpoint;
  }
  SessionContext& ctx() noexcept { return ctx_; }

  // Whether this connection can carry file descriptors (IDL `fd`) to the
  // peer.  Only a same-machine transport can; the rest leave this false.
  virtual bool supports_fd_passing() const noexcept { return false; }

  // Called by generated proxies before sending a call that has an `fd`
  // anywhere in its signature; throws if this connection cannot carry it.
  NPRPC_API void require_fd_passing() const;

  // Must be called once by the creator, right after wrapping the concrete
  // session in a shared_ptr (e.g. `session->bind_self(session);`), and
  // before the session is exposed to any I/O. Cannot be done from within
  // Session's own constructor — no shared_ptr owns *this yet at that point.
  void bind_self(std::weak_ptr<Session> self) noexcept
  {
    *self_cell_ = std::move(self);
  }
  // Handles incoming request in rx_buffer and prepares response in tx_buffer
  // in some cases rx_buffer and tx_buffer can be the same buffer
  // Returns true if a reply should be sent, false for fire-and-forget messages
  bool handle_request(nprpc::flat_buffer& rx_buffer,
                      nprpc::flat_buffer& tx_buffer);

  // Awaitable form of handle_request.  Today the body is still synchronous
  // (including optional POA DispatchExecutor post+wait); the Task gives
  // transports a place to co_await without blocking their I/O thread once
  // true async dispatch (resume_on executor) lands.  Default-POA / no-hop
  // paths complete without suspending.
  nprpc::Task<bool> handle_request_async(nprpc::flat_buffer& rx_buffer,
                                         nprpc::flat_buffer& tx_buffer);

  // Send a message for streaming (async, fire-and-forget).
  //
  // Returns false when the message provably did not go out — the shared
  // memory transport writes into a fixed-size ring and can say so, which is
  // the only way a producer learns its consumer stopped consuming.  A
  // transport that queues internally (TCP, WebSocket, QUIC) has nothing to
  // report at this point and answers true; its backpressure shows up as
  // memory growth in its own write queue instead.
  virtual bool send_stream_message(flat_buffer&& buffer)
  {
    send_receive_async(std::move(buffer), std::nullopt, 0);
    return true;
  }

  // Send a control message on main stream (for stream control: complete/error/cancel)
  // Default implementation is same as send_stream_message
  // QUIC sessions override to send on main bidirectional stream, not native data streams
  virtual bool send_main_stream_message(flat_buffer&& buffer)
  {
    return send_stream_message(std::move(buffer));
  }

  virtual void shutdown()
  {
    closed_.store(true);
    notify_peer_gone();
    try {
      timeout_timer_.cancel();
    } catch (...) {
    }
    try {
      inactive_timer_.cancel();
    } catch (...) {
    }
  }

  Session(boost::asio::any_io_executor executor);

  virtual ~Session();

  // Non-throwing form of send_receive, for callers such as the Swift bridge.
  [[nodiscard]] NPRPC_API Result<void>
  send_receive_result(flat_buffer& buffer, uint32_t timeout_ms) noexcept;

  using ResultHandler = std::function<void(Result<void>, flat_buffer&)>;
  // On an initiation error no handler is invoked; the caller handles failure.
  // On successful initiation the handler runs exactly once, possibly inline,
  // and must not throw. The response is borrowed for the duration of the call.
  [[nodiscard]] NPRPC_API Result<void> send_receive_async_result(
      flat_buffer&& buffer, ResultHandler handler, uint32_t timeout_ms) noexcept;

private:
  struct HandedOff {
    nprpc::flat_buffer rx;
    bool wants_reply;
    uint32_t call_id;   // of a FunctionCall, otherwise 0
    bool cancelled;
  };
  struct HandoffKick;
  static void run_handoff_kick(void* kick);

  void kick_handoffs(const DispatchExecutor& target);
  void drain_handoffs(const DispatchExecutor& running_on);
  void on_cancel_call(const nprpc::flat_buffer& rx);

  std::mutex handoff_mu_;
  std::deque<HandedOff> handoffs_;
  // A drain is scheduled or running. It stays set until the drain sees an empty
  // queue, so the message it is running still counts as "busy".
  bool handoff_draining_ = false;
  // The handed-off message running now.
  uint32_t running_call_id_ = 0;
  std::stop_source running_call_stop_{std::nostopstate};
  bool peer_gone_ = false;
  // Tells handle_request_body the running call was cancelled before it started.
  // Only the thread running that message touches it.
  bool skip_running_call_ = false;

  std::atomic<uint32_t> next_call_id_{1};

  // Calls running with no thread of their own. Guarded by handoff_mu_.
  friend class DeferredCall;
  static constexpr size_t max_deferred_calls = 1024;
  std::unordered_map<uint32_t, std::stop_source> deferred_calls_;
  // Set during dispatch, on the thread running it.
  bool call_deferred_ = false;
  std::shared_ptr<DeferredCall> blocking_deferred_call_;

protected:
  // The blocking call each client transport implements. It may still throw
  // where its dependencies do. Server sessions cannot initiate calls and keep
  // this default.
  NPRPC_API virtual boost::system::error_code
  do_send_receive(flat_buffer& buffer, uint32_t timeout_ms);
};

} // namespace nprpc::impl

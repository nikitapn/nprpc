// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include <boost/asio/post.hpp>

#include <coroutine>
#include <cstddef>
#include <cstring>
#include <memory>
#include <mutex>
#include <stop_token>
#include <utility>
#include <optional>

#include <nprpc/stream_base.hpp>
#include <nprpc/stream_reader.hpp>
#include <nprpc/stream_writer.hpp>
#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/impl/stream_manager.hpp>
#include <nprpc/impl/async_result.hpp>

#include "logging.hpp"

#define NPRPC_ENABLE_SESSION_TRACE 0

#if NPRPC_ENABLE_SESSION_TRACE
# define NPRPC_SESSION_LOG_TRACE(format, ...)                \
  NPRPC_LOG_TRACE(                                           \
    "[SESSION] {:p} {} " #format, (void*)this,               \
    remote_endpoint().to_string() __VA_OPT__(, )__VA_ARGS__)
#else
# define NPRPC_SESSION_LOG_TRACE(format, ...) do {} while(0)
#endif

#define NPRPC_SESSION_LOG_ERROR(format, ...)                  \
  NPRPC_LOG_ERROR(                                            \
    "[SESSION] {:p} {} " #format, (void*)this,                \
    remote_endpoint().to_string() __VA_OPT__(, )__VA_ARGS__)

namespace nprpc {
extern void set_context(impl::Session& session);
extern void reset_context();
} // namespace nprpc

namespace nprpc::impl {

boost::system::error_code Session::do_send_receive(flat_buffer&, uint32_t)
{
  assert(false && "this session cannot initiate RPC");
  return boost::asio::error::operation_not_supported;
}

void Session::send_receive(flat_buffer& buffer, uint32_t timeout_ms)
{
  const auto ec = do_send_receive(buffer, timeout_ms);
  if (ec == boost::asio::error::timed_out) throw nprpc::ExceptionTimeout();
  if (ec) throw nprpc::ExceptionCommFailure();
}

Result<void> Session::send_receive_result(flat_buffer& buffer, uint32_t timeout_ms) noexcept
{
  return capture_result([&]() -> Result<void> {
    if (const auto ec = do_send_receive(buffer, timeout_ms))
      return std::unexpected(transport_error(ec));
    return {};
  }, ErrorCode::connection_failure);
}

Result<void> Session::send_receive_async_result(
    flat_buffer&& buffer, ResultHandler handler, uint32_t timeout_ms) noexcept
{
  return start_result_operation(std::move(handler), [&](auto completion) {
    send_receive_async(std::move(buffer), std::move(completion), timeout_ms);
  });
}

nprpc::Task<> Session::send_receive_coro(flat_buffer& buffer,
                                         uint32_t timeout_ms,
                                         std::stop_token st)
{
  // Outlives this coroutine: a cancelled call leaves its frame behind while the
  // transport still holds the completion handler, until the reply or timeout.
  struct Call {
    std::mutex mutex;
    std::coroutine_handle<> waiting;
    bool finished = false;
    bool cancelled = false;
    boost::system::error_code ec;
    flat_buffer response;

    // The handle to resume, if this is what finished the call.
    std::coroutine_handle<> finish(bool cancel) noexcept
    {
      std::lock_guard lock(mutex);
      if (finished) return {};
      finished = true;
      cancelled = cancel;
      return std::exchange(waiting, {});
    }
  };

  if (st.stop_requested()) throw nprpc::OperationCancelled();

  auto call = std::make_shared<Call>();
  const auto executor = timeout_timer_.get_executor();
  const auto call_id = reserve_call_id(buffer);
  // Constructed before suspending, so a stop request that lands now finds nothing
  // to resume, and await_suspend sees it. Nothing has gone out in that case, so
  // there is nothing to cancel.
  std::stop_callback on_stop(st, [call, executor, call_id, cell = self_cell_]() noexcept {
    if (auto waiting = call->finish(true)) {
      if (auto session = cell->lock()) {
        try {
          session->cancel_call(call_id);
        } catch (const std::exception& e) {
          NPRPC_LOG_ERROR("Could not send a call cancellation: {}", e.what());
        }
      }
      boost::asio::post(executor, [waiting] { waiting.resume(); });
    }
  });

  struct Awaiter {
    Session& session;
    std::shared_ptr<Call> call;
    boost::asio::any_io_executor executor;
    flat_buffer& buffer;
    uint32_t timeout_ms;

    bool await_ready() noexcept { return false; }

    bool await_suspend(std::coroutine_handle<> handle)
    {
      {
        std::lock_guard lock(call->mutex);
        if (call->finished) return false;
        call->waiting = handle;
      }
      // Nothing here may touch this awaiter once the call is on its way: the
      // coroutine can be resumed elsewhere, and end, before this returns.
      session.send_receive_async(
          std::move(buffer),
          // Never resume the caller here. A transport can complete a call on its
          // reader thread with its locks held, and the caller is free to make its
          // next call as soon as it's running.
          [call = call, executor = executor](const boost::system::error_code& ec, flat_buffer& response) {
            {
              std::lock_guard lock(call->mutex);
              if (call->finished) return;
              call->ec = ec;
              if (!ec) call->response = std::move(response);
            }
            if (auto waiting = call->finish(false))
              boost::asio::post(executor, [waiting] { waiting.resume(); });
          },
          timeout_ms);
      return true;
    }

    void await_resume() noexcept {}
  };

  co_await Awaiter{*this, call, executor, buffer, timeout_ms};

  if (call->cancelled) throw nprpc::OperationCancelled();
  if (call->ec == boost::asio::error::timed_out) throw nprpc::ExceptionTimeout();
  if (call->ec) throw nprpc::ExceptionCommFailure();
  buffer = std::move(call->response);
}

std::optional<ObjectGuard>
get_object(SessionContext& ctx, uint16_t poa_idx, uint64_t object_id)
{
  do {
    auto poa = g_rpc->get_poa(poa_idx);
    if (!poa) {
      make_simple_answer(ctx, MessageId::Error_PoaNotExist);
      break;
    }
    auto obj = (*poa)->get_object(object_id);
    if (!obj) {
      make_simple_answer(ctx, MessageId::Error_ObjectNotExist);
      break;
    }
    return obj;
  } while (true);

  return std::nullopt;
}

// Run servant dispatch on the POA's optional DispatchExecutor (post+wait).
// SessionContext TLS is established on the thread that actually runs the
// servant so get_context() works under a main-queue hop.
template <typename SessionT, typename Fn>
void invoke_servant_dispatch(ObjectServant& servant,
                             SessionT& session,
                             Fn&& body)
{
  const auto& ex = servant.poa()->dispatch_executor();
  ex.invoke_sync([&] {
    set_context(session);
    try {
      std::forward<Fn>(body)();
    } catch (...) {
      reset_context();
      throw;
    }
    reset_context();
  });
}

namespace {

// Where a message is addressed, without dispatching it. Unknown or too-short
// messages give nullptr; handle_request reports those.
Poa* peek_target_poa(const flat_buffer& rx)
{
  if (rx.size() < sizeof(flat::Header) || !g_rpc)
    return nullptr;

  const auto* data = static_cast<const std::byte*>(rx.cdata().data());
  size_t poa_idx_offset = 0;
  switch (reinterpret_cast<const flat::Header*>(data)->msg_id) {
  case MessageId::FunctionCall:
    if (rx.size() < sizeof(flat::Header) + sizeof(flat::CallHeader))
      return nullptr;
    poa_idx_offset = sizeof(flat::Header) + offsetof(flat::CallHeader, poa_idx);
    break;
  case MessageId::StreamInitialization:
    poa_idx_offset = sizeof(flat::Header) + sizeof(uint64_t); // after stream_id
    break;
  case MessageId::AddReference:
  case MessageId::ReleaseObject:
    poa_idx_offset = sizeof(flat::Header);
    break;
  default:
    return nullptr;
  }
  uint16_t poa_idx = 0;
  if (rx.size() < poa_idx_offset + sizeof(poa_idx))
    return nullptr;
  std::memcpy(&poa_idx, data + poa_idx_offset, sizeof(poa_idx));
  auto poa = g_rpc->get_poa(poa_idx);
  return poa ? *poa : nullptr;
}

// The executor a drain should run on for this message. Empty means the runtime's
// thread pool. Without is_running_on, invoke_sync could not tell it is already
// on the executor and would wait for itself, so such an executor is reached
// from the pool, by post+wait.
DispatchExecutor handoff_target(const flat_buffer& rx)
{
  if (auto* poa = peek_target_poa(rx)) {
    const auto& ex = poa->dispatch_executor();
    if (ex && ex.is_running_on)
      return ex;
  }
  return {};
}

bool same_executor(const DispatchExecutor& a, const DispatchExecutor& b) noexcept
{
  return a.post == b.post && a.ctx == b.ctx;
}

} // namespace

struct Session::HandoffKick {
  std::shared_ptr<Session> session;
  DispatchExecutor running_on;
};

void Session::run_handoff_kick(void* p)
{
  std::unique_ptr<HandoffKick> kick(static_cast<HandoffKick*>(p));
  kick->session->drain_handoffs(kick->running_on);
}

bool Session::must_hand_off(const flat_buffer& rx)
{
  // A cancel has to overtake the queue, so hand_off() takes it on the reader.
  if (rx.size() >= sizeof(flat::Header) &&
      static_cast<const flat::Header*>(rx.cdata().data())->msg_id == MessageId::CancelCall)
    return true;
  if (auto* poa = peek_target_poa(rx); poa && poa->requires_off_transport_dispatch())
    return true;
  std::lock_guard lock(handoff_mu_);
  return handoff_draining_;
}

void Session::on_cancel_call(const flat_buffer& rx)
{
  if (rx.size() < sizeof(flat::Header) + sizeof(flat::CancelCallBody))
    return;
  uint32_t call_id = 0;
  std::memcpy(&call_id,
              static_cast<const std::byte*>(rx.cdata().data()) + sizeof(flat::Header),
              sizeof(call_id));
  if (call_id == 0)
    return;

  std::stop_source to_stop{std::nostopstate};
  {
    std::lock_guard lock(handoff_mu_);
    if (running_call_id_ == call_id) {
      to_stop = running_call_stop_;
    } else {
      for (auto& queued : handoffs_) {
        if (queued.call_id == call_id) {
          queued.cancelled = true;
          break;
        }
      }
    }
  }
  // Unlocked: this runs the servant's stop callbacks.
  to_stop.request_stop();
}

void Session::notify_peer_gone()
{
  std::stop_source to_stop{std::nostopstate};
  {
    std::lock_guard lock(handoff_mu_);
    peer_gone_ = true;
    to_stop = running_call_stop_;
  }
  to_stop.request_stop();
}

void Session::cancel_call(uint32_t call_id)
{
  constexpr auto total = sizeof(flat::Header) + sizeof(flat::CancelCallBody);
  flat_buffer fb;
  fb.prepare(total);
  fb.commit(total);
  auto* header = static_cast<flat::Header*>(fb.data().data());
  header->size = total;
  header->msg_id = MessageId::CancelCall;
  header->msg_type = MessageType::Request;
  header->request_id = 0;
  std::memcpy(static_cast<std::byte*>(fb.data().data()) + sizeof(flat::Header),
              &call_id, sizeof(call_id));
  send_main_stream_message(std::move(fb));
}

void Session::hand_off(flat_buffer&& rx, bool wants_reply)
{
  const auto* header = rx.size() >= sizeof(flat::Header)
      ? static_cast<const flat::Header*>(rx.cdata().data()) : nullptr;
  if (header && header->msg_id == MessageId::CancelCall) {
    on_cancel_call(rx);
    return;
  }
  const uint32_t call_id =
      header && header->msg_id == MessageId::FunctionCall ? header->request_id : 0;

  if (self_cell_->expired()) {
    process_handed_off(rx, wants_reply);
    return;
  }

  // Worked out before rx moves. Only used if this message starts a drain.
  const auto target = handoff_target(rx);
  {
    std::lock_guard lock(handoff_mu_);
    handoffs_.push_back({std::move(rx), wants_reply, call_id, false});
    if (handoff_draining_)
      return;
    handoff_draining_ = true;
  }
  kick_handoffs(target);
}

void Session::clear_handoffs()
{
  std::lock_guard lock(handoff_mu_);
  handoffs_.clear();
}

void Session::kick_handoffs(const DispatchExecutor& target)
{
  auto self = self_cell_->lock();
  if (!self) {
    // The session is going away, and with it whoever was waiting on these.
    std::lock_guard lock(handoff_mu_);
    handoffs_.clear();
    handoff_draining_ = false;
    return;
  }
  if (target) {
    target.post(target.ctx, &Session::run_handoff_kick,
                new HandoffKick{std::move(self), target});
  } else {
    boost::asio::post(get_io_context(), [self = std::move(self)] {
      self->drain_handoffs({});
    });
  }
}

void Session::process_handed_off(flat_buffer& rx, bool)
{
  flat_buffer tx;
  handle_request(rx, tx);
}

void Session::drain_handoffs(const DispatchExecutor& running_on)
{
  for (;;) {
    HandedOff next;
    std::stop_source stop{std::nostopstate};
    {
      std::lock_guard lock(handoff_mu_);
      if (handoffs_.empty()) {
        handoff_draining_ = false;
        return;
      }
      // A message for another executor moves the drain there, so a POA's
      // servants don't run on, or block, some other POA's thread. Pool-bound
      // messages (including stream frames) stay wherever the drain is.
      const auto target = handoff_target(handoffs_.front().rx);
      if (target && !same_executor(target, running_on)) {
        // handoff_draining_ stays set: the new drain takes over.
        target.post(target.ctx, &Session::run_handoff_kick,
                    new HandoffKick{self_cell_->lock(), target});
        return;
      }
      next = std::move(handoffs_.front());
      handoffs_.pop_front();
      running_call_id_ = next.call_id;
      running_call_stop_ = std::stop_source{};
      stop = running_call_stop_;
      if (peer_gone_)
        stop.request_stop(); // no callbacks yet: the servant hasn't started
    }
    skip_running_call_ = next.cancelled;
    ctx_.call.stop_token = stop.get_token();
    try {
      process_handed_off(next.rx, next.wants_reply);
    } catch (const std::exception& e) {
      NPRPC_LOG_ERROR("Handed-off message failed: {}", e.what());
    }
    ctx_.call = {};
    skip_running_call_ = false;
    {
      std::lock_guard lock(handoff_mu_);
      running_call_id_ = 0;
      running_call_stop_ = std::stop_source{std::nostopstate};
    }
  }
}

Session::Session(boost::asio::any_io_executor executor)
    : timeout_timer_{executor}
    , inactive_timer_{executor}
{
  // Owned via shared_ptr (not a bare `new`) so external holders reachable
  // only through ctx_.stream_manager's raw pointer — the Swift
  // NPRPCStreamWriter/NPRPCStreamReader, via external_retain()/
  // external_release() across the C bridge — can take their own share and
  // keep the object alive past *this Session's own lifetime, instead of it
  // either leaking forever or being freed out from under them.
  stream_manager_owner_ = std::make_shared<impl::StreamManager>(
      ctx_, self_cell_, static_cast<impl::RpcImpl*>(g_rpc)->stream_executor());
  ctx_.stream_manager = stream_manager_owner_.get();
  // Capture self_cell_ (a shared_ptr<weak_ptr<Session>>), not `this`. The
  // cell is a separate heap allocation kept alive by the callback's own
  // copy, so it's always safe to read even long after *this has been
  // destroyed — unlike a raw `this` capture, which crashed in production
  // ("pure virtual method called") when a chunk was written after the
  // owning session died without a clean close (bind_self() below never ran
  // for that leftover session, so cell->lock() correctly comes back empty
  // and the write is silently dropped instead of dispatching through a
  // dangling Session*).
  ctx_.stream_manager->set_send_callback(
      [cell = self_cell_](flat_buffer&& fb) {
        if (auto self = cell->lock()) {
          return self->send_main_stream_message(std::move(fb));
        }
        return false;
      });
  // Set up native stream callback for data - uses native QUIC streams if available
  ctx_.stream_manager->set_send_native_stream_callback(
      [cell = self_cell_](flat_buffer&& fb) {
        if (auto self = cell->lock()) {
          return self->send_stream_message(std::move(fb));
        }
        return false;
      });
}

Session::~Session()
{
  // A peer that vanished never sends StreamCancel, and its writers would wait
  // on credits forever while holding the manager.
  stream_manager_owner_->stop_pumps();
}

void Session::require_fd_passing() const
{
  if (!supports_fd_passing())
    throw nprpc::Exception(
        "File descriptors cannot be passed over " +
        ctx_.remote_endpoint.to_string() +
        ": only a shared-memory connection can carry them");
}

bool Session::handle_request(flat_buffer& rx_buffer, flat_buffer& tx_buffer)
{
  // Sync entry point used by TCP/QUIC/epoll/uring today.  Keep as a thin
  // wrapper so all logic lives in one place with the async form.
  return handle_request_body(rx_buffer, tx_buffer);
}

nprpc::Task<bool> Session::handle_request_async(flat_buffer& rx_buffer,
                                                flat_buffer& tx_buffer)
{
  // Future: co_await resume_on(poa.dispatch_executor) around servant
  // dispatch.  For now the body is fully synchronous so this Task never
  // suspends (await_ready() is true for co_awaiters).
  co_return handle_request_body(rx_buffer, tx_buffer);
}

bool Session::handle_request_body(flat_buffer& rx_buffer, flat_buffer& tx_buffer)
{
  bool needs_reply = true;  // Most messages need a reply

  auto validate = [this](ObjectServant& obj) {
    if (obj.validate_session(this->ctx_))
      return true;

    NPRPC_SESSION_LOG_ERROR("is trying to access secured object: {}", obj.get_class());
    make_simple_answer(ctx_, nprpc::impl::MessageId::Error_BadAccess);
    return false;
  };

  NPRPC_SESSION_LOG_TRACE("Received message of size {} bytes", rx_buffer.size());

  // Set context buffers
  ctx_.rx_buffer = &rx_buffer;
  ctx_.tx_buffer = &tx_buffer;

  auto cb = rx_buffer.cdata();

  // Validate message contains at least a header before accessing it
  if (cb.size() < sizeof(impl::flat::Header)) {
    NPRPC_SESSION_LOG_ERROR("Message too small for header: {} bytes", cb.size());
    make_simple_answer(ctx_, nprpc::impl::MessageId::Error_BadInput);
    return needs_reply;
  }

  auto header = static_cast<const impl::flat::Header*>(cb.data());
  switch (header->msg_id) {
  case MessageId::StreamInitialization: {
    impl::flat::StreamInit_Direct msg(rx_buffer, sizeof(impl::Header));
    const auto request_id = header->request_id;

    NPRPC_SESSION_LOG_TRACE("Got Msg: StreamInit [stream_id: {}, "
                   "interface_idx: {}, fn_idx: {}, poa_idx: {}, oid: {}]",
                   msg.stream_id(), (uint32_t)msg.interface_idx(),
                   (uint32_t)msg.func_idx(), msg.poa_idx(), msg.object_id());

    bool not_found = true;
    if (auto obj = get_object(ctx_, msg.poa_idx(), msg.object_id()); obj) {
      if (auto real_obj = (*obj).get(); real_obj) {
        if (!validate(*real_obj))
          return needs_reply;
        try {
          invoke_servant_dispatch(*real_obj, *this, [&] {
            // Dispatch to servant (servant must handle StreamInit msg_id)
            real_obj->dispatch(ctx_, false);
            // Preserve explicit servant replies such as Error_BadInput.
            if (tx_buffer.size() < sizeof(impl::flat::Header))
              make_simple_answer(ctx_, MessageId::Success);
          });
        } catch (const std::exception& e) {
          NPRPC_SESSION_LOG_ERROR("Exception during stream dispatch: {}", e.what());
          if (tx_buffer.size() < sizeof(impl::flat::Header)) {
            make_simple_answer(ctx_, MessageId::Error_BadInput);
          }
        }
        if (tx_buffer.size() >= sizeof(impl::flat::Header)) {
          static_cast<impl::flat::Header*>(tx_buffer.data().data())->request_id = request_id;
        }
        not_found = false;
      }
    }

    if (not_found) {
      if (tx_buffer.size() < sizeof(impl::flat::Header)) {
        NPRPC_SESSION_LOG_ERROR("Object not found for stream. {}", msg.object_id());
        ctx_.stream_manager->send_error(msg.stream_id(), 1,
                                        {}); // TODO: proper error code
      }
    }
    break;
  }
  case MessageId::StreamDataChunk: {
    NPRPC_SESSION_LOG_TRACE("Got Msg: StreamDataChunk");
    ctx_.stream_manager->on_chunk_received(std::move(rx_buffer));
    needs_reply = false;  // Fire-and-forget, no reply needed
    break;
  }
  case MessageId::StreamCompletion: {
    NPRPC_SESSION_LOG_TRACE("Got Msg: StreamCompletion");
    impl::flat::StreamComplete_Direct msg(rx_buffer, sizeof(impl::Header));
    ctx_.stream_manager->on_stream_complete(msg.stream_id(), msg.final_sequence());
    needs_reply = false;  // Fire-and-forget
    break;
  }
  case MessageId::StreamError: {
    NPRPC_SESSION_LOG_TRACE("Got Msg: StreamError");
    impl::flat::StreamError_Direct msg(rx_buffer, sizeof(impl::Header));
    // Copy error data from the message
    flat_buffer error_fb;
    auto error_span = msg.error_data();
    if (error_span.size() > 0) {
      error_fb.prepare(error_span.size());
      error_fb.commit(error_span.size());
      std::memcpy(error_fb.data().data(), error_span.data(), error_span.size());
    }
    ctx_.stream_manager->on_stream_error(msg.stream_id(), msg.error_code(), std::move(error_fb));
    needs_reply = false;  // Fire-and-forget
    break;
  }
  case MessageId::StreamCancellation: {
    NPRPC_SESSION_LOG_TRACE("Got Msg: StreamCancellation");
    impl::flat::StreamCancel_Direct msg(rx_buffer, sizeof(impl::Header));
    ctx_.stream_manager->on_stream_cancel(msg.stream_id());
    needs_reply = false;  // Fire-and-forget
    break;
  }
  case MessageId::StreamWindowUpdate: {
    NPRPC_SESSION_LOG_TRACE("Got Msg: StreamWindowUpdate");
    impl::flat::StreamWindowUpdate_Direct msg(rx_buffer, sizeof(impl::Header));
    ctx_.stream_manager->on_window_update(msg.stream_id(), msg.credits());
    needs_reply = false;  // Fire-and-forget
    break;
  }
  case MessageId::CancelCall:
    // Only on transports that don't call must_hand_off(); they run calls inline,
    // so the call it names has already finished.
    needs_reply = false;
    break;
  case MessageId::FunctionCall: {
    impl::flat::CallHeader_Direct ch(rx_buffer, sizeof(impl::Header));

    NPRPC_SESSION_LOG_TRACE("Got Msg: FunctionCall. [request_id: {}, "
                   "interface_idx: {}, fn_idx: {}, poa_idx: {}, oid: {}]",
                   header->request_id, (uint32_t)ch.interface_idx(),
                   (uint32_t)ch.function_idx(), ch.poa_idx(), ch.object_id());

    if (skip_running_call_) {
      make_simple_answer(ctx_, MessageId::Error_Cancelled, header->request_id);
      break;
    }

    bool not_found = true;
    if (auto obj = get_object(ctx_, ch.poa_idx(), ch.object_id()); obj) {
      if (auto real_obj = (*obj).get(); real_obj) {
        if (!validate(*real_obj))
          return needs_reply;
        // save request ID for later use
        auto request_id = header->request_id;
        try {
          invoke_servant_dispatch(*real_obj, *this, [&] {
            real_obj->dispatch(ctx_, false);
          });
        } catch (const nprpc::OperationCancelled&) {
          make_simple_answer(ctx_, MessageId::Error_Cancelled, request_id);
        } catch (const std::exception& e) {
          NPRPC_SESSION_LOG_ERROR("Exception during dispatch: {}", e.what());
          // TODO: find out why Web client does not handle
          // Error_BadInput properly
          make_simple_answer(ctx_, MessageId::Error_BadInput, request_id);
        }
        // Propagate request_id to response: generated dispatch code only sets
        // msg_type = Answer, never request_id. make_simple_answer already does
        // this for void/error paths, but non-void dispatch responses need it too.
        if (tx_buffer.size() >= sizeof(impl::flat::Header)) {
          static_cast<impl::flat::Header*>(tx_buffer.data().data())->request_id =
              request_id;
        }
        not_found = false;
      }
    }

    if (not_found) {
      NPRPC_SESSION_LOG_ERROR("Object not found {}", ch.object_id());
    }

    break;
  }
  case MessageId::AddReference: {
    detail::flat::ObjectIdLocal_Direct msg(rx_buffer, sizeof(impl::Header));
    detail::ObjectIdLocal oid{msg.poa_idx(), msg.object_id()};

    NPRPC_SESSION_LOG_TRACE("AddReference [poa_idx: {}, oid: {}]",
                            oid.poa_idx, oid.object_id);

    bool success = false;
    if (auto obj = get_object(ctx_, msg.poa_idx(), msg.object_id()); obj) {
      if (auto real_obj = (*obj).get(); real_obj) {
        if (!validate(*real_obj))
          return needs_reply;
        success = true;
        ctx_.ref_list.add_ref(real_obj);
      }
    }

    if (success) {
      NPRPC_SESSION_LOG_TRACE("Reference added");
      make_simple_answer(ctx_, nprpc::impl::MessageId::Success);
    } else {
      NPRPC_SESSION_LOG_TRACE("Object not found");
      make_simple_answer(ctx_, nprpc::impl::MessageId::Error_ObjectNotExist);
    }

    break;
  }
  case MessageId::ReleaseObject: {
    detail::flat::ObjectIdLocal_Direct msg(rx_buffer, sizeof(impl::Header));
    detail::ObjectIdLocal oid{msg.poa_idx(), msg.object_id()};

    NPRPC_SESSION_LOG_TRACE("ReleaseObject [poa_idx: {}, oid: {}]",
                            oid.poa_idx, oid.object_id);

    if (ctx_.ref_list.remove_ref(msg.poa_idx(), msg.object_id())) {
      make_simple_answer(ctx_, nprpc::impl::MessageId::Success);
    } else {
      make_simple_answer(ctx_, nprpc::impl::MessageId::Error_ObjectNotExist);
    }

    break;
  }
  default:
    NPRPC_SESSION_LOG_ERROR("Unknown message ID: {}", static_cast<uint32_t>(header->msg_id));
    make_simple_answer(ctx_, nprpc::impl::MessageId::Error_UnknownMessageId);
    break;
  }

  // Fire-and-forget / [unreliable] servants leave tx empty (no Success ACK).
  // Treat that as no reply so TCP/QUIC do not enqueue a zero-length write.
  if (needs_reply &&
      tx_buffer.size() < sizeof(impl::flat::Header)) {
    needs_reply = false;
  }

  if (needs_reply) {
    NPRPC_SESSION_LOG_TRACE("Sending reply");
    // dump_message(tx_buffer, true);
    if (ctx_.stream_manager) {
      ctx_.stream_manager->on_reply_sent();
    }
  }
  return needs_reply;
}

} // namespace nprpc::impl

// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include <optional>

#include <nprpc/stream_base.hpp>
#include <nprpc/stream_reader.hpp>
#include <nprpc/stream_writer.hpp>
#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/impl/stream_manager.hpp>

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

std::optional<ObjectGuard>
get_object(SessionContext& ctx, uint16_t poa_idx, uint64_t object_id)
{
  do {
    auto poa = g_rpc->get_poa(poa_idx);
    if (!poa) {
      make_simple_answer(ctx, MessageId::Error_PoaNotExist);
      break;
    }
    auto obj = poa->get_object(object_id);
    if (!obj) {
      make_simple_answer(ctx, MessageId::Error_ObjectNotExist);
      break;
    }
    return obj;
  } while (true);

  return std::nullopt;
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
          self->send_main_stream_message(std::move(fb));
        }
      });
  // Set up native stream callback for data - uses native QUIC streams if available
  ctx_.stream_manager->set_send_native_stream_callback(
      [cell = self_cell_](flat_buffer&& fb) {
        if (auto self = cell->lock()) {
          self->send_stream_message(std::move(fb));
        }
      });
}

bool Session::handle_request(flat_buffer& rx_buffer, flat_buffer& tx_buffer)
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
        set_context(*this);
        try {
          // Dispatch to servant (servant must handle StreamInit msg_id)
          real_obj->dispatch(ctx_, false);
          // Preserve explicit servant replies such as Error_BadInput.
          if (tx_buffer.size() < sizeof(impl::flat::Header))
            make_simple_answer(ctx_, MessageId::Success);
        } catch (const std::exception& e) {
          NPRPC_SESSION_LOG_ERROR("Exception during stream dispatch: {}", e.what());
          if (tx_buffer.size() < sizeof(impl::flat::Header)) {
            make_simple_answer(ctx_, MessageId::Error_BadInput);
          }
        }
        if (tx_buffer.size() >= sizeof(impl::flat::Header)) {
          static_cast<impl::flat::Header*>(tx_buffer.data().data())->request_id = request_id;
        }
        reset_context();
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
  case MessageId::FunctionCall: {
    impl::flat::CallHeader_Direct ch(rx_buffer, sizeof(impl::Header));

    NPRPC_SESSION_LOG_TRACE("Got Msg: FunctionCall. [request_id: {}, "
                   "interface_idx: {}, fn_idx: {}, poa_idx: {}, oid: {}]",
                   header->request_id, (uint32_t)ch.interface_idx(),
                   (uint32_t)ch.function_idx(), ch.poa_idx(), ch.object_id());

    bool not_found = true;
    if (auto obj = get_object(ctx_, ch.poa_idx(), ch.object_id()); obj) {
      if (auto real_obj = (*obj).get(); real_obj) {
        if (!validate(*real_obj))
          return needs_reply;
        set_context(*this);
        // save request ID for later use
        auto request_id = header->request_id;
        try {
          real_obj->dispatch(ctx_, false);
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
        reset_context();
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

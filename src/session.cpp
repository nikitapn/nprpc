// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include <optional>

#include <nprpc/stream_base.hpp>
#include <nprpc/stream_reader.hpp>
#include <nprpc/stream_writer.hpp>
#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/impl/stream_manager.hpp>

#include "logging.hpp"

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
  ctx_.stream_manager = new impl::StreamManager(ctx_);
  // Set up send callback for control messages - uses main stream
  ctx_.stream_manager->set_send_callback([this](flat_buffer&& fb) {
    this->send_main_stream_message(std::move(fb));
  });
  // Set up native stream callback for data - uses native QUIC streams if available
  ctx_.stream_manager->set_send_native_stream_callback([this](flat_buffer&& fb) {
    this->send_stream_message(std::move(fb));
  });
  // Set up post callback - use executor to post async work
  ctx_.stream_manager->set_post_callback([executor](std::function<void()> work) {
    boost::asio::post(executor, std::move(work));
  });
}

bool Session::handle_request(flat_buffer& rx_buffer, flat_buffer& tx_buffer)
{
  bool needs_reply = true;  // Most messages need a reply

  auto validate = [this](ObjectServant& obj) {
    if (obj.validate_session(this->ctx_))
      return true;

    NPRPC_LOG_ERROR(
        "Handle Request: {} is trying to access secured object: {}",
        remote_endpoint().to_string(), obj.get_class());
    make_simple_answer(ctx_, nprpc::impl::MessageId::Error_BadAccess);
    return false;
  };

  NPRPC_LOG_INFO("Handle Request: received a message:");
  // dump_message(rx_buffer, true);

  // Set context buffers
  ctx_.rx_buffer = &rx_buffer;
  ctx_.tx_buffer = &tx_buffer;

  auto cb = rx_buffer.cdata();

  // Validate message contains at least a header before accessing it
  if (cb.size() < sizeof(impl::flat::Header)) {
    NPRPC_LOG_ERROR("Message too small for header: {} bytes", cb.size());
    make_simple_answer(ctx_, nprpc::impl::MessageId::Error_BadInput);
    return needs_reply;
  }

  auto header = static_cast<const impl::flat::Header*>(cb.data());
  switch (header->msg_id) {
  case MessageId::StreamInitialization: {
    impl::flat::StreamInit_Direct msg(rx_buffer, sizeof(impl::Header));

    NPRPC_LOG_INFO("Handle Request: StreamInit. stream_id: {}, "
                   "interface_idx: {}, fn_idx: {}, poa_idx: {}, oid: {}",
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
        } catch (const std::exception& e) {
          NPRPC_LOG_ERROR(
              "Handle Request: Exception during stream dispatch: {}",
              e.what());
          // Send StreamError
          ctx_.stream_manager->send_error(msg.stream_id(), 1,
                                          {}); // TODO: proper error code
        }
        reset_context();
        not_found = false;
      }
    }

    if (not_found) {
      NPRPC_LOG_ERROR("Handle Request: Object not found for stream. {}",
                      msg.object_id());
      ctx_.stream_manager->send_error(msg.stream_id(), 1,
                                      {}); // TODO: proper error code
    }
    break;
  }
  case MessageId::StreamDataChunk: {
    NPRPC_LOG_ERROR("Handle Request: StreamDataChunk.");
    ctx_.stream_manager->on_chunk_received(std::move(rx_buffer));
    needs_reply = false;  // Fire-and-forget, no reply needed
    break;
  }
  case MessageId::StreamCompletion: {
    NPRPC_LOG_INFO("Handle Request: StreamCompletion.");
    impl::flat::StreamComplete_Direct msg(rx_buffer, sizeof(impl::Header));
    ctx_.stream_manager->on_stream_complete(msg.stream_id());
    needs_reply = false;  // Fire-and-forget
    break;
  }
  case MessageId::StreamError: {
    NPRPC_LOG_INFO("Handle Request: StreamError.");
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
    NPRPC_LOG_INFO("Handle Request: StreamCancellation.");
    impl::flat::StreamCancel_Direct msg(rx_buffer, sizeof(impl::Header));
    ctx_.stream_manager->on_stream_cancel(msg.stream_id());
    needs_reply = false;  // Fire-and-forget
    break;
  }
  case MessageId::FunctionCall: {
    impl::flat::CallHeader_Direct ch(rx_buffer, sizeof(impl::Header));

    NPRPC_LOG_INFO("Handle Request: FunctionCall. request_id: {}, "
                   "interface_idx: {}, fn_idx: {}, poa_idx: {}, oid: {}",
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
          NPRPC_LOG_ERROR(
              "Handle Request: Exception during dispatch: {}",
              e.what());
          // TODO: find out why Web client does not handle
          // Error_BadInput properly
          make_simple_answer(ctx_, MessageId::Error_BadInput, request_id);
        }
        reset_context();
        not_found = false;
      }
    }

    if (not_found) {
      NPRPC_LOG_ERROR("Handle Request: Object not found. {}",
                      ch.object_id());
    }

    break;
  }
  case MessageId::AddReference: {
    detail::flat::ObjectIdLocal_Direct msg(rx_buffer, sizeof(impl::Header));
    detail::ObjectIdLocal oid{msg.poa_idx(), msg.object_id()};

    NPRPC_LOG_INFO("Handle Request: AddReference. poa_idx: {}, oid: {}",
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
      NPRPC_LOG_INFO("Handle Request: Refference added.");
      make_simple_answer(ctx_, nprpc::impl::MessageId::Success);
    } else {
      NPRPC_LOG_INFO("Handle Request: Object not found.");
      make_simple_answer(ctx_, nprpc::impl::MessageId::Error_ObjectNotExist);
    }

    break;
  }
  case MessageId::ReleaseObject: {
    detail::flat::ObjectIdLocal_Direct msg(rx_buffer, sizeof(impl::Header));
    detail::ObjectIdLocal oid{msg.poa_idx(), msg.object_id()};

    NPRPC_LOG_INFO(
        "Handle Request: ReleaseObject. poa_idx: {}, oid: {}",
        oid.poa_idx, oid.object_id);

    if (ctx_.ref_list.remove_ref(msg.poa_idx(), msg.object_id())) {
      make_simple_answer(ctx_, nprpc::impl::MessageId::Success);
    } else {
      make_simple_answer(ctx_, nprpc::impl::MessageId::Error_ObjectNotExist);
    }

    break;
  }
  default:
    NPRPC_LOG_ERROR("Handle Request: Unknown message ID: {}",
                    static_cast<uint32_t>(header->msg_id));
    make_simple_answer(ctx_, nprpc::impl::MessageId::Error_UnknownMessageId);
    break;
  }

  if (needs_reply) {
    NPRPC_LOG_INFO("Handle Request: sending reply:");
    // dump_message(tx_buffer, true);
  }
  return needs_reply;
}

} // namespace nprpc::impl

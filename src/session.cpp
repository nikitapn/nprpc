// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include "logging.hpp"
#include <nprpc/impl/nprpc_impl.hpp>
#include <optional>

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

void Session::handle_request(flat_buffer& rx_buffer, flat_buffer& tx_buffer)
{
  auto validate = [this](ObjectServant& obj) {
    if (obj.validate_session(this->ctx_))
      return true;

    NPRPC_LOG_ERROR(
        "[nprpc] Handle Request: {} is trying to access secured object: {}",
        remote_endpoint().to_string(), obj.get_class());
    make_simple_answer(ctx_, nprpc::impl::MessageId::Error_BadAccess);
    return false;
  };

  if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryMessageContent) {
    NPRPC_LOG_INFO("[nprpc] Handle Request: received a message:");
    dump_message(rx_buffer, true);
  }

  // Set context buffers
  ctx_.rx_buffer = &rx_buffer;
  ctx_.tx_buffer = &tx_buffer;

  auto cb = rx_buffer.cdata();

  // Validate message contains at least a header before accessing it
  if (cb.size() < sizeof(impl::flat::Header)) {
    NPRPC_LOG_ERROR("Message too small for header: {} bytes", cb.size());
    make_simple_answer(ctx_, nprpc::impl::MessageId::Error_BadInput);
    return;
  }

  auto header = static_cast<const impl::flat::Header*>(cb.data());
  switch (header->msg_id) {
  case MessageId::FunctionCall: {
    impl::flat::CallHeader_Direct ch(rx_buffer, sizeof(impl::Header));

    if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
      NPRPC_LOG_INFO("[nprpc] Handle Request: FunctionCall. request_id: {}, "
                     "interface_idx: {}, fn_idx: {}, poa_idx: {}, oid: {}",
                     header->request_id, (uint32_t)ch.interface_idx(),
                     (uint32_t)ch.function_idx(), ch.poa_idx(), ch.object_id());
    }

    bool not_found = true;
    if (auto obj = get_object(ctx_, ch.poa_idx(), ch.object_id()); obj) {
      if (auto real_obj = (*obj).get(); real_obj) {
        if (!validate(*real_obj))
          return;
        set_context(*this);
        // save request ID for later use
        auto request_id = header->request_id;
        try {
          real_obj->dispatch(ctx_, false);
        } catch (const std::exception& e) {
          NPRPC_LOG_ERROR(
              "[nprpc] Handle Request: Exception during dispatch: {}",
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
      NPRPC_LOG_ERROR("[nprpc] Handle Request: Object not found. {}",
                      ch.object_id());
    }

    break;
  }
  case MessageId::AddReference: {
    detail::flat::ObjectIdLocal_Direct msg(rx_buffer, sizeof(impl::Header));
    detail::ObjectIdLocal oid{msg.poa_idx(), msg.object_id()};

    if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
      NPRPC_LOG_INFO(
          "[nprpc] Handle Request: AddReference. poa_idx: {}, oid: {}",
          oid.poa_idx, oid.object_id);
    }

    bool success = false;
    if (auto obj = get_object(ctx_, msg.poa_idx(), msg.object_id()); obj) {
      if (auto real_obj = (*obj).get(); real_obj) {
        if (!validate(*real_obj))
          return;
        success = true;
        ctx_.ref_list.add_ref(real_obj);
      }
    }

    if (success) {
      if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
        NPRPC_LOG_INFO("[nprpc] Handle Request: Refference added.");
      }
      make_simple_answer(ctx_, nprpc::impl::MessageId::Success);
    } else {
      if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
        NPRPC_LOG_INFO("[nprpc] Handle Request: Object not found.");
      }
      make_simple_answer(ctx_, nprpc::impl::MessageId::Error_ObjectNotExist);
    }

    break;
  }
  case MessageId::ReleaseObject: {
    detail::flat::ObjectIdLocal_Direct msg(rx_buffer, sizeof(impl::Header));
    detail::ObjectIdLocal oid{msg.poa_idx(), msg.object_id()};

    if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
      NPRPC_LOG_INFO(
          "[nprpc] Handle Request: ReleaseObject. poa_idx: {}, oid: {}",
          oid.poa_idx, oid.object_id);
    }

    if (ctx_.ref_list.remove_ref(msg.poa_idx(), msg.object_id())) {
      make_simple_answer(ctx_, nprpc::impl::MessageId::Success);
    } else {
      make_simple_answer(ctx_, nprpc::impl::MessageId::Error_ObjectNotExist);
    }

    break;
  }
  default:
    NPRPC_LOG_ERROR("[nprpc] Handle Request: Unknown message ID: {}",
                    static_cast<uint32_t>(header->msg_id));
    make_simple_answer(ctx_, nprpc::impl::MessageId::Error_UnknownMessageId);
    break;
  }

  if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryMessageContent) {
    NPRPC_LOG_INFO("[nprpc] Handle Request: sending reply:");
    dump_message(tx_buffer, true);
  }
}

} // namespace nprpc::impl

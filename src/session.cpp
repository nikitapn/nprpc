// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

#include <nprpc/impl/nprpc_impl.hpp>
#include <iostream>
#include <optional>

namespace nprpc {
extern void set_context(impl::Session& session);
extern void reset_context();
}

namespace nprpc::impl {

std::optional<ObjectGuard> get_object(SessionContext& ctx, flat_buffer& bin, flat_buffer& bout, uint16_t poa_idx, uint64_t object_id) {
	do {
		auto poa = g_rpc->get_poa(poa_idx);
		if (!poa) {
			make_simple_answer(ctx, bin, bout, MessageId::Error_PoaNotExist);
			break;
		}
		auto obj = poa->get_object(object_id);
		if (!obj) {
			make_simple_answer(ctx, bin, bout, MessageId::Error_ObjectNotExist);
			break;
		}
		return obj;
	} while (true);

	return std::nullopt;
}

void Session::handle_request(flat_buffer& rx_buffer, flat_buffer& tx_buffer) {
	auto validate = [this, &rx_buffer, &tx_buffer](ObjectServant& obj) {
		if (obj.validate_session(this->ctx_))
			return true;

		std::cerr << "[nprpc] Handle Request: " << remote_endpoint() << " is trying to access secured object: " << obj.get_class() << '\n';
		make_simple_answer(ctx_, rx_buffer, tx_buffer, nprpc::impl::MessageId::Error_BadAccess);
		return false;
	};

	if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryMessageContent) {
		std::cout << "[nprpc] Handle Request: received a message:\n";
		dump_message(rx_buffer, true);
	}

	auto cb = rx_buffer.cdata();
	
	// Validate message contains at least a header before accessing it
	if (cb.size() < sizeof(impl::flat::Header)) {
		std::cerr << "Message too small for header: " << cb.size() << " bytes\n";
		make_simple_answer(ctx_, rx_buffer, tx_buffer, nprpc::impl::MessageId::Error_BadInput);
		return;
	}
	
	auto header = static_cast<const impl::flat::Header*>(cb.data());
	switch (header->msg_id) {
	case MessageId::FunctionCall: {
		impl::flat::CallHeader_Direct ch(rx_buffer, sizeof(impl::Header));

		if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
			std::cout << "[nprpc] Handle Request: FunctionCall. " <<
				"request_id: " << header->request_id <<
				", interface_idx: " << (uint32_t)ch.interface_idx() <<
				", fn_idx: " << (uint32_t)ch.function_idx() <<
				", poa_idx: " << ch.poa_idx() <<
				", oid: " << ch.object_id() << std::endl;
		}

		bool not_found = true;
		if (auto obj = get_object(ctx_, rx_buffer, tx_buffer, ch.poa_idx(), ch.object_id()); obj) {
			if (auto real_obj = (*obj).get(); real_obj) {
				if (!validate(*real_obj)) return;
				set_context(*this);
				// save request ID for later use
				auto request_id = header->request_id;
				try { 
					real_obj->dispatch(rx_buffer, tx_buffer, ctx_, false);
				} catch (const std::exception& e) {
					std::cerr << "[nprpc] Handle Request: Exception during dispatch: " << e.what() << '\n';
					// TODO: find out why Web client does not handle Error_BadInput properly
					make_simple_answer(ctx_, rx_buffer, tx_buffer, MessageId::Error_BadInput, request_id);
				}
				reset_context();
				not_found = false;
			}
		}

		if (not_found) {
			std::cerr << "[nprpc] Handle Request: Object not found. " << ch.object_id() << '\n';
		}

		break;
	}
	case MessageId::AddReference: {
		detail::flat::ObjectIdLocal_Direct msg(rx_buffer, sizeof(impl::Header));
		detail::ObjectIdLocal oid{ msg.poa_idx(), msg.object_id() };
		
		if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
			std::cout << "[nprpc] Handle Request: AddReference. " << "poa_idx: " << oid.poa_idx << ", oid: " << oid.object_id << std::endl;
		}
		
		bool success = false;
		if (auto obj = get_object(ctx_, rx_buffer, tx_buffer, msg.poa_idx(), msg.object_id()); obj) {
			if (auto real_obj = (*obj).get(); real_obj) {
				if (!validate(*real_obj)) return;
				success = true;
				ctx_.ref_list.add_ref(real_obj);
			}
		}
		
		if (success) {
			if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
				std::cout << "[nprpc] Handle Request: Refference added." << std::endl;
			}
			make_simple_answer(ctx_, rx_buffer, tx_buffer, nprpc::impl::MessageId::Success);
		} else {
			if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
				std::cout << "[nprpc] Handle Request: Object not found." << std::endl;
			}
			make_simple_answer(ctx_, rx_buffer, tx_buffer, nprpc::impl::MessageId::Error_ObjectNotExist);
		}

		break;
	}
	case MessageId::ReleaseObject: {
		detail::flat::ObjectIdLocal_Direct msg(rx_buffer, sizeof(impl::Header));
		detail::ObjectIdLocal oid{ msg.poa_idx(), msg.object_id() };

		if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
			std::cout << "[nprpc] Handle Request: ReleaseObject. " << "poa_idx: " << oid.poa_idx << ", oid: " << oid.object_id << std::endl;
		}

		if (ctx_.ref_list.remove_ref(msg.poa_idx(), msg.object_id())) {
			make_simple_answer(ctx_, rx_buffer, tx_buffer, nprpc::impl::MessageId::Success);
		} else {
			make_simple_answer(ctx_, rx_buffer, tx_buffer, nprpc::impl::MessageId::Error_ObjectNotExist);
		}

		break;
	}
	default:
		make_simple_answer(ctx_, rx_buffer, tx_buffer, nprpc::impl::MessageId::Error_UnknownMessageId);
		break;
	}

	if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryMessageContent) {
		std::cout << "[nprpc] Handle Request: sending reply:\n";
		dump_message(tx_buffer, true);
	}
}

}

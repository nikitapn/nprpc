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

std::optional<ObjectGuard> get_object(flat_buffer& buf, uint16_t poa_idx, uint64_t object_id) {
	do {
		auto poa = g_orb->get_poa(poa_idx);
		if (!poa) {
			make_simple_answer(buf, MessageId::Error_PoaNotExist);
			break;
		}
		auto obj = poa->get_object(object_id);
		if (!obj) {
			make_simple_answer(buf, MessageId::Error_ObjectNotExist);
			break;
		}
		return obj;
	} while (true);

	return std::nullopt;
}

void Session::handle_request() {
	auto validate = [this](ObjectServant& obj) {
		if (obj.validate_session(this->ctx_)) return true;

		std::cerr << remote_endpoint() << " is trying to access secured object: " << obj.get_class() << '\n';
		make_simple_answer(rx_buffer_(), nprpc::impl::MessageId::Error_BadAccess);
		return false;
	};

	if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryMessageContent) {
		std::cout << "received a message:\n";
		dump_message(rx_buffer_(), true);
	}

	auto cb = rx_buffer_().cdata();
	
	// Validate message contains at least a header before accessing it
	if (cb.size() < sizeof(impl::flat::Header)) {
		std::cerr << "Message too small for header: " << cb.size() << " bytes\n";
		make_simple_answer(rx_buffer_(), nprpc::impl::MessageId::Error_BadInput);
		return;
	}
	
	auto header = static_cast<const impl::flat::Header*>(cb.data());
	switch (header->msg_id) {
	case MessageId::FunctionCall: {
		impl::flat::CallHeader_Direct ch(rx_buffer_(), sizeof(impl::Header));
		
		if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
			std::cout << "FunctionCall. " << "interface_idx: " << (uint32_t)ch.interface_idx() << ", fn_idx: " << (uint32_t)ch.function_idx() 
				<< ", poa_idx: " << ch.poa_idx() << ", oid: " << ch.object_id() << std::endl;
		}
		bool not_found = true;
		if (auto obj = get_object(rx_buffer_(), ch.poa_idx(), ch.object_id()); obj) {
			if (auto real_obj = (*obj).get(); real_obj) {
				if (!validate(*real_obj)) return;
				set_context(*this);
				// save request ID for later use
				auto request_id = header->request_id;
				try { 
					real_obj->dispatch(rx_buffer_, ctx_, false);
				} catch (const std::exception& e) {
					std::cerr << "Exception during dispatch: " << e.what() << '\n';
					// TODO: find out why Web client does not handle Error_BadInput properly
					make_simple_answer(rx_buffer_(), MessageId::Error_BadInput, request_id);
				}
				reset_context();
				not_found = false;
			}
		}

		if (not_found) {
			std::cerr << "Object not found. " << ch.object_id() << '\n';
		}

		break;
	}
	case MessageId::AddReference: {
		detail::flat::ObjectIdLocal_Direct msg(rx_buffer_(), sizeof(impl::Header));
		detail::ObjectIdLocal oid{ msg.poa_idx(), msg.object_id() };
		
		if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
			std::cout << "AddReference. " << "poa_idx: " << oid.poa_idx << ", oid: " << oid.object_id << std::endl;
		}
		
		bool success = false;
		if (auto obj = get_object(rx_buffer_(), msg.poa_idx(), msg.object_id()); obj) {
			if (auto real_obj = (*obj).get(); real_obj) {
				if (!validate(*real_obj)) return;
				success = true;
				ctx_.ref_list.add_ref(real_obj);
			}
		}
		
		if (success) {
			if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
				std::cout << "Refference added." << std::endl;
			}
			make_simple_answer(rx_buffer_(), nprpc::impl::MessageId::Success);
		} else {
			if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
				std::cout << "Object not found." << std::endl;
			}
			make_simple_answer(rx_buffer_(), nprpc::impl::MessageId::Error_ObjectNotExist);
		}

		break;
	}
	case MessageId::ReleaseObject: {
		detail::flat::ObjectIdLocal_Direct msg(rx_buffer_(), sizeof(impl::Header));
		detail::ObjectIdLocal oid{ msg.poa_idx(), msg.object_id() };

		if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
			std::cout << "ReleaseObject. " << "poa_idx: " << oid.poa_idx << ", oid: " << oid.object_id << std::endl;
		}

		if (ctx_.ref_list.remove_ref(msg.poa_idx(), msg.object_id())) {
			make_simple_answer(rx_buffer_(), nprpc::impl::MessageId::Success);
		} else {
			make_simple_answer(rx_buffer_(), nprpc::impl::MessageId::Error_ObjectNotExist);
		}

		break;
	}
	default:
		make_simple_answer(rx_buffer_(), nprpc::impl::MessageId::Error_UnknownMessageId);
		break;
	}

	if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryMessageContent) {
		std::cout << "sending reply:\n";
		dump_message(rx_buffer_(), true);
	}
}

}

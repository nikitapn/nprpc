// Copyright (c) 2021 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

#include <nprpc/nprpc_impl.hpp>
#include <iostream>
#include <optional>

namespace nprpc::impl {

std::optional<ObjectGuard> get_object(boost::beast::flat_buffer& buf, uint16_t poa_idx, uint64_t object_id) {
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
	if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryMessageContent) {
		std::cout << "received a message:\n";
		dump_message(rx_buffer_(), true);
	}

	auto cb = rx_buffer_().cdata();
	switch (static_cast<const impl::Header*>(cb.data())->msg_id) {
	case MessageId::FunctionCall: {
		impl::flat::CallHeader_Direct ch(rx_buffer_(), sizeof(impl::Header));
		
		if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
			std::cout << "FunctionCall. " << "interface_idx: " << (uint32_t)ch.interface_idx() << ", fn_idx: " << (uint32_t)ch.function_idx() 
				<< ", poa_idx: " << ch.poa_idx() << ", oid: " << ch.object_id() << std::endl;
		}
		bool not_found = true;
		if (auto obj = get_object(rx_buffer_(), ch.poa_idx(), ch.object_id()); obj) {
			if (auto real_obj = (*obj).get(); real_obj) {
				real_obj->dispatch(rx_buffer_, remote_endpoint_, false, ref_list_);
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
				success = true;
				ref_list_.add_ref(real_obj);
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

		if (ref_list_.remove_ref(msg.poa_idx(), msg.object_id())) {
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

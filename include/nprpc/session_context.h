// Copyright (c) 2021-2025 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by
// LICENSING file in the topmost directory

#pragma once

#include <nprpc_base.hpp>
#include <nprpc/endpoint.hpp>
#include <nprpc/export.hpp>

namespace nprpc {

class ObjectServant;

namespace impl {
class ReferenceListImpl;
class SharedMemoryChannel;
}

class NPRPC_API ReferenceList {
	impl::ReferenceListImpl* impl_;
public:
	void add_ref(ObjectServant* obj);
	// false - reference not exist
	bool remove_ref(poa_idx_t poa_idx, oid_t oid);

	ReferenceList() noexcept;
	~ReferenceList();
};

struct SessionContext {
	EndPoint remote_endpoint;
	ReferenceList ref_list;
	// For server-side shared memory sessions, points to the channel for zero-copy responses
	// nullptr for all other session types (TCP, WebSocket, client-side, etc.)
	impl::SharedMemoryChannel* shm_channel = nullptr;
	flat_buffer *rx_buffer = nullptr, *tx_buffer = nullptr;
};

NPRPC_API SessionContext& get_context();

} // namespace nprpc

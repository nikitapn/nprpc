// Copyright (c) 2021 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

#pragma once

#include "export.hpp"
#include <nprpc/nprpc_base.hpp>

namespace nprpc {

class ObjectServant;

struct EndPoint {
	union {
		struct {
			uint32_t ip4;
			uint32_t port;
		};
		uint64_t hash;
	};
	EndPoint() = default;
	EndPoint(uint32_t _ip4, uint32_t _port) : ip4{ _ip4 }, port{ _port } {}
	bool is_local() const noexcept { return hash == 0; }
	bool operator==(const EndPoint& other) const noexcept { return hash == other.hash; }
};

namespace impl {
class ReferenceListImpl;
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

} // namespace nprpc
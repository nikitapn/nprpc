#pragma once

#include <nprpc/nprpc_base.hpp>
#include <nprpc/endpoint.hpp>
#include <nprpc/export.hpp>

namespace nprpc {

class ObjectServant;

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

struct SessionContext {
	EndPoint remote_endpoint;
	ReferenceList ref_list;

	bool operator==(const SessionContext& other) const noexcept { 
		return remote_endpoint == other.remote_endpoint; 
	}
};

NPRPC_API SessionContext& get_context();

} // namespace nprpc

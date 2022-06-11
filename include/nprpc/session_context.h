#pragma once

#include <nprpc/endpoint.hpp>
#include <nprpc/export.hpp>

namespace nprpc {

struct SessionContext {
	EndPoint remote_endpoint;

	bool operator==(const SessionContext& other) const noexcept { return remote_endpoint == other.remote_endpoint; }
};



NPRPC_API const SessionContext& get_context();

}

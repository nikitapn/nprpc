#include <nprpc/session_context.h>
#include <nprpc/impl/session.hpp>

namespace nprpc {

static thread_local SessionContext* ctx;

NPRPC_API SessionContext& get_context() {
	return (*ctx);
}

void set_context(impl::Session& session) {
	ctx = &session.ctx();
}

}
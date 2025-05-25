#include <nprpc/impl/nprpc_impl.hpp>
#include <cassert>

namespace nprpc::impl {

Poa* RpcImpl::create_poa_impl(uint32_t objects_max, PoaPolicy::Lifespan lifespan) {
    auto idx = poa_size_.fetch_add(1);
    auto poa = new PoaImpl(objects_max, idx);
    poas_[idx] = std::unique_ptr<PoaImpl>(poa);
    
    // Set the policy
    poa->pl_lifespan_ = lifespan;
    
    return poa;
}

} // namespace nprpc::impl

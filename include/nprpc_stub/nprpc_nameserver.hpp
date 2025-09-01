#ifndef __NPRPC_NPRPC_NAMESERVER_HPP__
#define __NPRPC_NPRPC_NAMESERVER_HPP__

#include <nprpc/flat.hpp>
#include <nprpc/nprpc.hpp>

namespace nprpc { 
class INameserver_Servant
  : public virtual nprpc::ObjectServant
{
public:
  static std::string_view _get_class() noexcept { return "nprpc_nameserver/nprpc.Nameserver"; }
  std::string_view get_class() const noexcept override { return INameserver_Servant::_get_class(); }
  void dispatch(nprpc::Buffers& bufs, [[maybe_unused]] nprpc::SessionContext& ctx, [[maybe_unused]] bool from_parent) override;
  virtual void Bind (nprpc::Object* obj, ::nprpc::flat::Span<char> name) = 0;
  virtual bool Resolve (::nprpc::flat::Span<char> name, nprpc::detail::flat::ObjectId_Direct obj) = 0;
};

class Nameserver
  : public virtual nprpc::Object
{
  const uint8_t interface_idx_;
public:
  using servant_t = INameserver_Servant;

  Nameserver(uint8_t interface_idx) : interface_idx_(interface_idx) {}
  void Bind (const ObjectId& obj, const std::string& name);
  bool Resolve (const std::string& name, Object*& obj);
};

} // namespace nprpc

namespace nprpc_nameserver::helper {
} // namespace nprpc_nameserver::helper

#endif
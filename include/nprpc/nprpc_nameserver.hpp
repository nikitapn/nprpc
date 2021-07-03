#ifndef NPRPC_NAMESERVER_
#define NPRPC_NAMESERVER_

#include <nprpc/flat.hpp>
#include <nprpc/nprpc.hpp>

namespace nprpc { 
class INameserver_Servant
  : public virtual nprpc::ObjectServant
{
public:
  static std::string_view _get_class() noexcept { return "nprpc_nameserver/nprpc.Nameserver"; }
  std::string_view get_class() const noexcept override { return INameserver_Servant::_get_class(); }
  void dispatch(nprpc::Buffers& bufs, nprpc::EndPoint remote_endpoint, bool from_parent, nprpc::ReferenceList& ref_list) override;
  virtual void Bind (nprpc::Object* obj, ::flat::Span<char> name) = 0;
  virtual bool Resolve (::flat::Span<char> name, nprpc::detail::flat::ObjectId_Direct obj) = 0;
};

class Nameserver
  : public virtual nprpc::Object
{
  const uint8_t interface_idx_;
public:
  using servant_t = INameserver_Servant;

  Nameserver(uint8_t interface_idx) : interface_idx_(interface_idx) {}
  void Bind (/*in*/const ObjectId& obj, /*in*/const std::string& name);
  bool Resolve (/*in*/const std::string& name, /*out*/Object*& obj);
};

} // namespace nprpc


#endif
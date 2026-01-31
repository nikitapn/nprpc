#ifndef __NPRPC_NPRPC_NAMESERVER_HPP__
#define __NPRPC_NPRPC_NAMESERVER_HPP__

#include <nprpc/flat.hpp>
#include <nprpc/nprpc.hpp>
#include <nprpc/stream_writer.hpp>
#include <nprpc/stream_reader.hpp>

// Module export macro
#ifdef NPRPC_EXPORTS
#  define NPRPC_NAMESERVER_API NPRPC_EXPORT_ATTR
#else
#  define NPRPC_NAMESERVER_API NPRPC_IMPORT_ATTR
#endif

namespace nprpc::common {

class NPRPC_NAMESERVER_API INameserver_Servant
  : public virtual ::nprpc::ObjectServant
{
public:
  static std::string_view _get_class() noexcept { return "nprpc_nameserver/nprpc.common.Nameserver"; }
  std::string_view get_class() const noexcept override { return INameserver_Servant::_get_class(); }
  void dispatch(::nprpc::SessionContext& ctx, [[maybe_unused]] bool from_parent) override;
  virtual void Bind (::nprpc::Object* obj, ::nprpc::flat::Span<char> name) = 0;
  virtual bool Resolve (::nprpc::flat::Span<char> name, ::nprpc::detail::flat::ObjectId_Direct obj) = 0;
};

class NPRPC_NAMESERVER_API Nameserver
  : public virtual ::nprpc::Object
{
  const uint8_t interface_idx_;
public:
  using servant_t = INameserver_Servant;

  Nameserver(uint8_t interface_idx) : interface_idx_(interface_idx) {}
  void Bind (const ObjectId& obj, const std::string& name);
  bool Resolve (const std::string& name, Object*& obj);
};


#ifdef NPRPC_SWIFT_BRIDGE
// Swift servant bridge for Nameserver
class Nameserver_SwiftBridge : public INameserver_Servant {
  void* swift_servant_;
public:
  Nameserver_SwiftBridge(void* swift_servant) : swift_servant_(swift_servant) {}

  void Bind(int32_t obj, int32_t const& name) override;
  void Resolve(int32_t const& name, int32_t& obj) override;
};

extern "C" {
  void Bind_swift_trampoline(void* swift_servant, int32_t obj, void* name);
  void Resolve_swift_trampoline(void* swift_servant, void* name, int32_t* obj);
}
#endif // NPRPC_SWIFT_BRIDGE

namespace helper {
} // namespace nprpc_nameserver::helper
} // module nprpc::common

#endif
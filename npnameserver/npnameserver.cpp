// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include <memory>
#include <unordered_map>
#include <print>

#include <nprpc_nameserver.hpp>

#include <boost/asio/signal_set.hpp>
#include <boost/beast/core/error.hpp>

#ifndef NPRPC_NAMESERVER_LOG
#define NPRPC_NAMESERVER_LOG 0
#endif

class NameserverImpl : public nprpc::common::INameserver_Servant
{
  std::unordered_map<std::string, std::unique_ptr<nprpc::Object>> objects_;

public:
  void Bind(nprpc::Object* obj, nprpc::flat::Span<char> name) override
  {
    if constexpr (NPRPC_NAMESERVER_LOG) {
      std::println("Binding object: {}", obj->object_id());
      std::println("  Object will be bound as: {}", (std::string_view)name);
    }
    auto const str = std::string((std::string_view)name);
    objects_[str] = std::move(std::unique_ptr<nprpc::Object>(obj));
  }

  bool Resolve(nprpc::flat::Span<char> name,
               nprpc::detail::flat::ObjectId_Direct obj) override
  {
    auto const str = std::string((std::string_view)name);
    auto found = objects_.find(str);

    if (found == objects_.end()) {
      obj.object_id() = nprpc::invalid_object_id;
      return false;
    }

    const auto& oid = found->second->get_data();
    nprpc::detail::helpers::assign_from_cpp_ObjectId(obj, oid);

    if constexpr (NPRPC_NAMESERVER_LOG) {
      std::print("Resolved object: {}", obj.object_id());
      std::print("  Object is resolved as: {}", str);
    }

    return true;
  }
};

int main()
{
  NameserverImpl server;
  boost::asio::io_context ioc;

  try {
    auto rpc = nprpc::RpcBuilder()
                   .set_debug_level(nprpc::DebugLevel::DebugLevel_Critical)
                   .with_tcp(15000)
                   .with_http(15001)
                   .build(ioc);

    auto poa = nprpc::PoaBuilder(rpc)
                   .with_max_objects(1)
                   .with_object_id_policy(
                       nprpc::PoaPolicy::ObjectIdPolicy::UserSupplied)
                   .with_lifespan(nprpc::PoaPolicy::Lifespan::Persistent)
                   .build();

    auto oid = poa->activate_object_with_id(
        0, &server,
        nprpc::ObjectActivationFlags::ALLOW_TCP |
            nprpc::ObjectActivationFlags::ALLOW_WEBSOCKET);

    boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);
    signals.async_wait(
        [&](boost::beast::error_code const&, int) { ioc.stop(); });

    ioc.run();
    rpc->destroy();

  } catch (const std::exception& e) {
    std::println(stderr, "Nameserver failed: {}", e.what());
    return 1;
  }

  return 0;
}

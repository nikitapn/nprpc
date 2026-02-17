// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include <iostream>
#include <memory>
#include <unordered_map>

#include <nprpc_nameserver.hpp>

#include <boost/asio/signal_set.hpp>
#include <boost/beast/core/error.hpp>
#include <boost/format.hpp>

#ifndef NPRPC_NAMESERVER_LOG
#define NPRPC_NAMESERVER_LOG 1
#endif

class NameserverImpl : public nprpc::common::INameserver_Servant
{
  std::unordered_map<std::string, std::unique_ptr<nprpc::Object>> objects_;

public:
  void Bind(nprpc::Object* obj, nprpc::flat::Span<char> name) override
  {
    if constexpr (NPRPC_NAMESERVER_LOG) {
      std::cout << boost::format("Binding object: %1%\n") % obj->object_id();
      std::cout << boost::format("  Object will be bound as: %1%\n") % (std::string_view)name;
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
      std::cout << boost::format("Resolved object: %1%\n") % obj.object_id();
      std::cout << boost::format("  Object is resolved as: %1%\n") % str;
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
                   .set_log_level(nprpc::LogLevel::trace)
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
    std::cerr << boost::format("Nameserver failed: %1%\n") % e.what();
    return 1;
  }

  return 0;
}

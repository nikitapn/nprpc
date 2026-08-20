// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include "nprpc_base.hpp"
#include <iostream>
#include <memory>
#include <unordered_map>

#include <nprpc_nameserver.hpp>

#include <boost/asio/signal_set.hpp>
#include <boost/beast/core/error.hpp>
#include <boost/format.hpp>

#ifndef NPRPC_NAMESERVER_LOG
# define NPRPC_NAMESERVER_LOG 0
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
    nprpc::detail::helpers::ObjectId::to_flat(obj, oid);

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
    nprpc::RpcBuilder builder;
    builder.set_log_level(nprpc::LogLevel::error);

#if defined(NPRPC_ENABLE_TCP) || defined(NPRPC_ENABLE_HTTP) || \
    defined(NPRPC_ENABLE_WEBSOCKET) || defined(NPRPC_ENABLE_QUIC)
    builder.with_hostname("localhost");
#endif

#ifdef NPRPC_ENABLE_TCP
    builder.with_tcp(15000);
#endif
#ifdef NPRPC_ENABLE_HTTP
    auto http = builder.with_http(15001)
                    .allow_origins({"https://localhost:24443"});
#ifdef NPRPC_ENABLE_SSL
    http.ssl("/home/nikita/projects/nprpc/certs/out/localhost.crt",
             "/home/nikita/projects/nprpc/certs/out/localhost.key");
#endif
#endif

    auto rpc = builder.build();

    auto poa = nprpc::PoaBuilder(rpc)
                   .with_max_objects(1)
                   .with_object_id_policy(
                       nprpc::PoaPolicy::ObjectIdPolicy::UserSupplied)
                   .with_lifespan(nprpc::PoaPolicy::Lifespan::Persistent)
                   .build();

    using F = nprpc::ObjectActivationFlags;
    auto flags = F::shm;
#ifdef NPRPC_ENABLE_TCP
    flags = flags | F::tcp;
#endif
#ifdef NPRPC_ENABLE_HTTP
    flags = flags | F::http;
# ifdef NPRPC_ENABLE_SSL
    flags = flags | F::https;
# endif
#endif
#if defined(NPRPC_ENABLE_WEBSOCKET) && defined(NPRPC_ENABLE_HTTP)
    flags = flags | F::ws;
# ifdef NPRPC_ENABLE_SSL
    flags = flags | F::wss;
# endif
#endif
#ifdef NPRPC_ENABLE_QUIC
    flags = flags | F::quic;
#endif
    [[maybe_unused]] auto oid = poa->activate_object_with_id(0, &server, flags);

    boost::asio::signal_set signals(rpc->ioc(), SIGINT, SIGTERM);
    signals.async_wait(
        [&](boost::beast::error_code const&, int) { rpc->ioc().stop(); });

    rpc->run();
    rpc->destroy();

  } catch (const std::exception& e) {
    std::cerr << boost::format("Nameserver failed: %1%\n") % e.what();
    return 1;
  }

  return 0;
}

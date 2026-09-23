// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include "nprpc_base.hpp"
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

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

namespace {

// Clients reach the nameserver at fixed ports (see Rpc::get_nameserver), so
// only the hostname, TLS and CORS are configurable.
struct Options {
  std::string hostname = "localhost";
  std::string cert_file;
  std::string key_file;
  std::vector<std::string> allowed_origins;
};

constexpr std::string_view usage =
    "Usage: npnameserver [options]\n"
    "\n"
    "Serves the NPRPC nameserver on TCP port 15000 and HTTP/WebSocket port\n"
    "15001 (for transports compiled into NPRPC).\n"
    "\n"
    "Options:\n"
    "  --hostname NAME        Host written into object references (default: localhost)\n"
    "  --cert FILE            TLS certificate; with --key, serves HTTPS/WSS on 15001\n"
    "  --key FILE             TLS private key\n"
    "  --allow-origin ORIGIN  Allow cross-origin browser calls from ORIGIN (repeatable)\n"
    "  -h, --help             Show this help\n";

// Returns false after printing a message if the arguments are invalid or
// --help was given; `exit_code` says which.
bool parse_args(int argc, char** argv, Options& opts, int& exit_code)
{
  for (int i = 1; i < argc; ++i) {
    std::string_view arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      std::cout << usage;
      exit_code = 0;
      return false;
    }
    std::string* target = nullptr;
    if (arg == "--hostname")
      target = &opts.hostname;
    else if (arg == "--cert")
      target = &opts.cert_file;
    else if (arg == "--key")
      target = &opts.key_file;
    else if (arg == "--allow-origin")
      target = &opts.allowed_origins.emplace_back();
    else {
      std::cerr << "npnameserver: unknown option '" << arg << "'\n\n" << usage;
      exit_code = 2;
      return false;
    }
    if (++i == argc) {
      std::cerr << "npnameserver: " << arg << " needs a value\n";
      exit_code = 2;
      return false;
    }
    *target = argv[i];
  }
  if (opts.cert_file.empty() != opts.key_file.empty()) {
    std::cerr << "npnameserver: --cert and --key must be given together\n";
    exit_code = 2;
    return false;
  }
  return true;
}

} // namespace

int main(int argc, char** argv)
{
  Options opts;
  if (int exit_code; !parse_args(argc, argv, opts, exit_code))
    return exit_code;

  [[maybe_unused]] const bool use_tls = !opts.cert_file.empty();

  NameserverImpl server;

  try {
    nprpc::RpcBuilder builder;
    builder.set_log_level(nprpc::LogLevel::error);

#if defined(NPRPC_ENABLE_TCP) || defined(NPRPC_ENABLE_HTTP) || \
    defined(NPRPC_ENABLE_WEBSOCKET) || defined(NPRPC_ENABLE_QUIC)
    builder.with_hostname(opts.hostname);
#endif

#ifdef NPRPC_ENABLE_TCP
    builder.with_tcp(15000);
#endif
#ifdef NPRPC_ENABLE_HTTP
    auto http = builder.with_http(15001);
    if (!opts.allowed_origins.empty())
      http.allow_origins(std::move(opts.allowed_origins));
    if (use_tls) {
#ifdef NPRPC_ENABLE_SSL
      http.ssl(opts.cert_file, opts.key_file);
#else
      throw std::runtime_error("TLS was not compiled in (NPRPC_ENABLE_SSL=OFF)");
#endif
    }
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
    // The HTTP listener detects TLS per connection, so with a certificate it
    // serves both plain and secure clients on the same port.
#ifdef NPRPC_ENABLE_HTTP
    flags = flags | F::http;
    if (use_tls)
      flags = flags | F::https;
#endif
#if defined(NPRPC_ENABLE_WEBSOCKET) && defined(NPRPC_ENABLE_HTTP)
    flags = flags | F::ws;
    if (use_tls)
      flags = flags | F::wss;
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

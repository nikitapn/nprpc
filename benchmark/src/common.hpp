#pragma once

#include <nprpc/nprpc.hpp>
#include <nprpc_nameserver.hpp>
#include <nprpc_benchmark.hpp>
#include <nprpc/impl/misc/thread_pool.hpp>

extern nprpc::Rpc* g_rpc;

template<typename T>
auto get_object(const std::string& object_name) {
  auto nameserver = g_rpc->get_nameserver("127.0.0.1");

  nprpc::Object* raw;
  if (!nameserver->Resolve(object_name, raw))
    throw std::runtime_error("Failed to resolve object from nameserver: " + object_name);

  return nprpc::ObjectPtr(nprpc::narrow<T>(raw));
}
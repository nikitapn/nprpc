// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

// nprpc_node - Native Node.js addon entry point

#include "shm_channel_wrapper.hpp"
#include <napi.h>

namespace nprpc::impl {
// Stub for global RPC pointer since it's declared in hpp as extern
class RpcImpl* g_rpc = nullptr;
}

Napi::Object Init(Napi::Env env, Napi::Object exports)
{
  nprpc_node::ShmChannelWrapper::Init(env, exports);

  // Export version info
  exports.Set("version", Napi::String::New(env, "0.1.0"));

  return exports;
}

NODE_API_MODULE(nprpc_node, Init)

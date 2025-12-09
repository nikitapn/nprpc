// nprpc_node - Native Node.js addon entry point
// Copyright (c) 2025 nikitapnn1@gmail.com
// SPDX-License-Identifier: MIT

#include "shm_channel_wrapper.hpp"
#include <napi.h>

Napi::Object Init(Napi::Env env, Napi::Object exports)
{
  nprpc_node::ShmChannelWrapper::Init(env, exports);

  // Export version info
  exports.Set("version", Napi::String::New(env, "0.1.0"));

  return exports;
}

NODE_API_MODULE(nprpc_node, Init)

// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <nprpc/endpoint.hpp>
#include <nprpc/export.hpp>
#include <nprpc_base.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace nprpc {

class ObjectServant;

class StreamManager;

namespace impl {
class ReferenceListImpl;
class SharedMemoryChannel;
class StreamManager;
} // namespace impl

class NPRPC_API ReferenceList
{
  impl::ReferenceListImpl* impl_;

public:
  void add_ref(ObjectServant* obj);
  // false - reference not exist
  bool remove_ref(poa_idx_t poa_idx, oid_t oid);

  ReferenceList() noexcept;
  ~ReferenceList();
};

struct SessionContext {
  EndPoint remote_endpoint;
  ReferenceList ref_list;
  // For server-side shared memory sessions, points to the channel for
  // zero-copy responses nullptr for all other session types (TCP, WebSocket,
  // client-side, etc.)
  impl::SharedMemoryChannel* shm_channel = nullptr;
  flat_buffer *rx_buffer = nullptr, *tx_buffer = nullptr;

  impl::StreamManager* stream_manager = nullptr;

  // HTTP-only: value of the incoming Cookie: header (valid during dispatch).
  // Empty string_view for all non-HTTP transports (TCP/WS/SHM/UDP).
  std::string_view cookies;

  // HTTP-only: Set-Cookie header values to attach to the HTTP response.
  // Servants append to this via nprpc::http::set_cookie().
  // Ignored for all non-HTTP transports.
  std::vector<std::string> set_cookies;
};

NPRPC_API SessionContext& get_context();

} // namespace nprpc

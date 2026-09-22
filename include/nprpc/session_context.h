// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <nprpc/endpoint.hpp>
#include <nprpc/export.hpp>
#include <nprpc_base_ext.hpp>
#include <string>
#include <string_view>
#include <vector>

#include <boost/asio/io_context.hpp>

namespace nprpc {

class ObjectServant;

class StreamManager;

namespace impl {
class ReferenceListImpl;
class SharedMemoryChannel;
class StreamManager;
} // namespace impl

/// The servants a session holds references to. Released together when the
/// session ends, so a client that disconnects without releasing its
/// references does not keep transient objects alive.
class NPRPC_API ReferenceList
{
  impl::ReferenceListImpl* impl_;

public:
  /// Records a reference to `obj` and adds one to its count. Ignored for a
  /// servant already in the list.
  void add_ref(ObjectServant* obj);
  /// Drops the reference to the servant `oid` in POA `poa_idx`.
  /// Returns false if the session held none.
  bool remove_ref(poa_idx_t poa_idx, oid_t oid);

  /// An empty list.
  ReferenceList() noexcept;
  /// Releases every reference still held.
  ~ReferenceList();
};

/// One client connection as seen by a servant: who is calling, and the
/// state that lives as long as the connection. Get the current one with
/// `get_context()` inside a servant method.
struct SessionContext {
  /// The client's address and transport.
  EndPoint remote_endpoint;
  /// Servants this client holds references to.
  ReferenceList ref_list;
  /// For server-side shared memory sessions, points to the channel for
  /// zero-copy responses; nullptr for all other session types (TCP,
  /// WebSocket, client-side, etc.)
  impl::SharedMemoryChannel* shm_channel = nullptr;
  /// The request being dispatched and the reply being built; runtime use.
  flat_buffer *rx_buffer = nullptr, *tx_buffer = nullptr;

  /// Streams open on this session; runtime use.
  impl::StreamManager* stream_manager = nullptr;

  /// HTTP-only: value of the incoming Cookie: header (valid during dispatch).
  /// Empty for all non-HTTP transports (TCP/WS/SHM).
  std::string_view cookies;

  /// HTTP-only: Set-Cookie header values to attach to the HTTP response.
  /// Servants append to this via `nprpc::http::set_cookie()`.
  /// Ignored for all non-HTTP transports.
  std::vector<std::string> set_cookies;
};

/// The session of the call being dispatched on this thread. Only valid
/// inside a servant method; throws `Exception` elsewhere.
NPRPC_API SessionContext& get_context();

/// Returns the io_context for the current RPC instance.
/// Safe to call from any thread, including servant dispatch handlers.
/// Use with `nprpc::spawn_task()` to schedule async work after dispatch returns.
NPRPC_API boost::asio::io_context& get_io_context();

} // namespace nprpc

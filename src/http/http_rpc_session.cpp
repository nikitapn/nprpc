// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include "../logging.hpp"
#include <cstring>
#include <nprpc/impl/http_rpc_session.hpp>

namespace nprpc::impl {

bool HttpRpcSession::process_rpc_request(const std::string& request_data,
                                         std::string& response_data)
{
  try {
    // FIXME: Avoid copy
    // Clear previous buffer
    rx_buffer_.consume(rx_buffer_.size());

    // Copy request data into rx_buffer
    auto mb = rx_buffer_.prepare(request_data.size());
    std::memcpy(mb.data(), request_data.data(), request_data.size());
    rx_buffer_.commit(request_data.size());

    // Dispatch the RPC call
    handle_request(rx_buffer_, tx_buffer_);

    // Extract response
    auto response_span = tx_buffer_.cdata();
    response_data.assign(static_cast<const char*>(response_span.data()),
                         response_span.size());

    return true;

  } catch (const std::exception& e) {
    NPRPC_LOG_ERROR("HttpRpcSession: Error processing request: {}", e.what());
    return false;
  }
}

} // namespace nprpc::impl

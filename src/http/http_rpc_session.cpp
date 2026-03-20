// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include "../logging.hpp"
#include <cstring>
#include <vector>
#include <nprpc/impl/http_rpc_session.hpp>

namespace nprpc::impl {

bool HttpRpcSession::process_rpc_request(const std::string& request_data,
                                         std::string& response_data,
                                         std::string_view cookies,
                                         std::vector<std::string>* out_set_cookies)
{
  flat_buffer request_buffer(request_data.empty() ? flat_buffer::default_initial_size()
                                                  : request_data.size());
  if (!request_data.empty()) {
    auto writable = request_buffer.prepare(request_data.size());
    std::memcpy(writable.data(), request_data.data(), request_data.size());
    request_buffer.commit(request_data.size());
  }

  flat_buffer response_buffer;
  if (!process_rpc_request(std::move(request_buffer), response_buffer, cookies,
                           out_set_cookies)) {
    return false;
  }

  auto response_span = response_buffer.cdata();
  response_data.assign(static_cast<const char*>(response_span.data()),
                       response_span.size());
  return true;
}

bool HttpRpcSession::process_rpc_request(flat_buffer&& request_data,
                                         flat_buffer& response_data,
                                         std::string_view cookies,
                                         std::vector<std::string>* out_set_cookies)
{
  try {
    // Populate incoming cookies so servants can call nprpc::http::get_cookie()
    ctx_.cookies = cookies;
    ctx_.set_cookies.clear();

    rx_buffer_ = std::move(request_data);
    tx_buffer_.clear();

    // Dispatch the RPC call
    handle_request(rx_buffer_, tx_buffer_);

    response_data = std::move(tx_buffer_);

    // Forward any Set-Cookie headers queued by the servant
    if (out_set_cookies && !ctx_.set_cookies.empty())
      *out_set_cookies = std::move(ctx_.set_cookies);

    return true;

  } catch (const std::exception& e) {
    NPRPC_LOG_ERROR("HttpRpcSession: Error processing request: {}", e.what());
    return false;
  }
}

} // namespace nprpc::impl

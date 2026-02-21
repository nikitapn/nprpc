// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <boost/asio.hpp>
#include <memory>
#include <string_view>
#include <vector>
#include <nprpc/common.hpp>
#include <nprpc/impl/session.hpp>

namespace nprpc::impl {

/**
 * @brief HTTP RPC session for handling one-shot RPC calls over HTTP
 *
 * Unlike WebSocket sessions, HTTP sessions are stateless:
 * - Each HTTP request creates a temporary session
 * - RPC call is processed
 * - Response is returned
 * - Session is destroyed
 *
 * This is ideal for:
 * - Simple data fetching (GetData, GetList, etc.)
 * - Guest/anonymous users
 * - Initial page loads
 * - Fire-and-forget calls (analytics, logging)
 *
 * For subscriptions and live updates, WebSocket is still preferred.
 */
class HttpRpcSession : public Session,
                       public std::enable_shared_from_this<HttpRpcSession>
{
  flat_buffer rx_buffer_{flat_buffer::default_initial_size()};
  flat_buffer tx_buffer_{flat_buffer::default_initial_size()};

public:
  HttpRpcSession(boost::asio::io_context& ioc)
      : Session(ioc.get_executor())
  {
    // HTTP sessions are "tethered" (ephemeral) - use TcpTethered type
    ctx_.remote_endpoint = EndPoint(EndPointType::TcpTethered, "", 0);
  }

  /**
   * @brief Process an RPC request from HTTP POST body
   *
   * @param request_data Binary NPRPC request data from HTTP body
   * @param response_data Output parameter for response data
   * @return true if processed successfully, false on error
   */
  bool process_rpc_request(const std::string& request_data,
                           std::string& response_data,
                           std::string_view cookies = {},
                           std::vector<std::string>* out_set_cookies = nullptr);

  // HTTP sessions don't support async operations - these should never be
  // called
  virtual void timeout_action() final
  {
    // No-op for HTTP sessions
  }

  virtual void send_receive(flat_buffer&, uint32_t) override
  {
    // HTTP sessions don't make outbound calls
    assert(false && "send_receive not supported on HTTP sessions");
  }

  virtual void send_receive_async(
      flat_buffer&&,
      std::optional<std::function<void(const boost::system::error_code&,
                                       flat_buffer&)>>&&,
      uint32_t) override
  {
    // HTTP sessions don't make outbound calls
    assert(false && "send_receive_async not supported on HTTP sessions");
  }

  ~HttpRpcSession() = default;
};

/**
 * @brief Factory function to create and process an HTTP RPC request
 *
 * @param ioc IO context
 * @param request_body HTTP POST body containing NPRPC request
 * @param response_body Output for NPRPC response
 * @return true if successful, false on error
 */
inline bool process_http_rpc(boost::asio::io_context& ioc,
                             const std::string& request_body,
                             std::string& response_body,
                             std::string_view cookies = {},
                             std::vector<std::string>* out_set_cookies = nullptr)
{
  auto session = std::make_shared<HttpRpcSession>(ioc);
  return session->process_rpc_request(request_body, response_body, cookies, out_set_cookies);
}

} // namespace nprpc::impl

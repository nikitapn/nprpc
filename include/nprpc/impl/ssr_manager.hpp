// Copyright (c) 2025 nikitapnn1@gmail.com
// SPDX-License-Identifier: MIT

#pragma once

#ifdef NPRPC_SSR_ENABLED

#include <map>
#include <nprpc/impl/node_worker_manager.hpp>
#include <string>
#include <string_view>

namespace nprpc::impl {

/**
 * @brief Global SSR manager - initialized when SSR is enabled
 */
NPRPC_API extern std::unique_ptr<NodeWorkerManager> g_ssr_manager;

/**
 * @brief Initialize the SSR subsystem
 *
 * Must be called after g_cfg is set up, before HTTP servers start.
 * Spawns the Node.js worker process.
 */
NPRPC_API void init_ssr(boost::asio::io_context& ioc);

/**
 * @brief Stop the SSR subsystem
 *
 * Called during shutdown to gracefully stop the Node.js worker.
 */
NPRPC_API void stop_ssr();

/**
 * @brief Check if a request should be handled by SSR
 *
 * SSR is used for:
 * - GET/HEAD requests for HTML pages and SvelteKit data endpoints
 * - POST requests for SvelteKit form actions (paths with ?/ query)
 * - Not /rpc endpoints
 *
 * @param method HTTP method (GET, POST, etc.)
 * @param path Request path (including query string)
 * @param accept Accept header value
 * @return true if request should be forwarded to SSR
 */
inline bool should_ssr(std::string_view method,
                       std::string_view path,
                       std::string_view accept)
{
  // Don't SSR the RPC endpoint
  if (path == "/rpc" || path.starts_with("/rpc/")) {
    return false;
  }

  // SvelteKit form actions are POST requests with ?/ in the path
  // e.g., /sverdle?/enter, /contact?/submit
  if (method == "POST") {
    return path.find("?/") != std::string_view::npos;
  }

  // Only GET and HEAD requests beyond this point
  if (method != "GET" && method != "HEAD") {
    return false;
  }

  // SvelteKit __data.json endpoints are always SSR (client-side navigation)
  if (path.find("__data.json") != std::string_view::npos) {
    return true;
  }

  // Check if client accepts HTML (for regular page requests)
  if (accept.find("text/html") == std::string_view::npos) {
    return false;
  }

  // Check if path has a file extension (static file)
  // SSR handles routes without extensions
  auto query_pos = path.find('?');
  auto path_only =
      (query_pos != std::string_view::npos) ? path.substr(0, query_pos) : path;
  auto last_slash = path_only.rfind('/');
  auto last_segment = (last_slash != std::string_view::npos)
                          ? path_only.substr(last_slash + 1)
                          : path_only;

  // If there's a dot in the last segment, it's likely a static file
  if (last_segment.find('.') != std::string_view::npos) {
    return false;
  }

  return true;
}

/**
 * @brief Check if SSR is ready to handle requests
 */
inline bool is_ssr_ready()
{
  return g_ssr_manager && g_ssr_manager->is_ready();
}

/**
 * @brief Forward a request to SSR and get the response
 *
 * @param method HTTP method
 * @param url Full URL (scheme://host:port/path?query)
 * @param headers Request headers
 * @param body Request body (for POST/PUT)
 * @param client_address Client IP address
 * @param timeout_ms Timeout in milliseconds
 * @return Response from SSR, or nullopt on error/timeout
 */
inline std::optional<NodeWorkerManager::SsrResponse>
forward_to_ssr(std::string_view method,
               std::string_view url,
               const std::map<std::string, std::string>& headers,
               std::string_view body,
               std::string_view client_address,
               uint32_t timeout_ms = 30000)
{
  if (!is_ssr_ready()) {
    return std::nullopt;
  }

  NodeWorkerManager::SsrRequest req;
  req.method = std::string(method);
  req.url = std::string(url);
  req.headers = headers;
  req.body = std::string(body);
  req.client_address = std::string(client_address);

  return g_ssr_manager->forward_request(req, timeout_ms);
}

/**
 * @brief Async version of forward_to_ssr
 */
inline void forward_to_ssr_async(
    std::string_view method,
    std::string_view url,
    const std::map<std::string, std::string>& headers,
    std::string_view body,
    std::string_view client_address,
    std::function<void(std::optional<NodeWorkerManager::SsrResponse>)> callback,
    uint32_t timeout_ms = 30000)
{
  if (!is_ssr_ready()) {
    if (callback)
      callback(std::nullopt);
    return;
  }

  NodeWorkerManager::SsrRequest req;
  req.method = std::string(method);
  req.url = std::string(url);
  req.headers = headers;
  req.body = std::string(body);
  req.client_address = std::string(client_address);

  g_ssr_manager->forward_request_async(req, callback, timeout_ms);
}

} // namespace nprpc::impl

#endif // NPRPC_SSR_ENABLED

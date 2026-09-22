// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include <nprpc/impl/page_dispatch.hpp>
#include <nprpc/impl/nprpc_impl.hpp>

#include "../logging.hpp"

namespace nprpc::impl {

bool page_handler_applies(std::string_view method,
                          std::string_view path) noexcept
{
  if (!g_cfg.page_handler) return false;
  if (method != "GET" && method != "HEAD" && method != "POST") return false;
  // Framework-owned paths stay framework-owned: a catch-all handler must not be
  // able to shadow the RPC endpoint or the dev-reload hook by accident.
  if (path == "/rpc" || path.starts_with("/rpc/")) return false;
  if (path.starts_with("/_nprpc/")) return false;
  return true;
}

std::optional<PageResponse>
invoke_page_handler(std::string_view method,
                    std::string_view target,
                    std::map<std::string, std::string> headers,
                    std::string body,
                    std::string_view client_address)
{
  const auto [path, query] = split_page_target(target);
  if (!page_handler_applies(method, path)) return std::nullopt;

  PageRequest req;
  req.method = std::string(method);
  req.target = std::string(target);
  req.path = std::string(path);
  req.query = std::string(query);
  req.headers = std::move(headers);
  req.body = std::move(body);
  req.client_address = std::string(client_address);

  try {
    return g_cfg.page_handler(req);
  } catch (const std::exception& e) {
    NPRPC_LOG_ERROR("Page handler threw for '{}': {}", req.target, e.what());
    return std::nullopt;
  } catch (...) {
    NPRPC_LOG_ERROR("Page handler threw a non-standard exception for '{}'",
                    req.target);
    return std::nullopt;
  }
}

} // namespace nprpc::impl

// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <nprpc/export.hpp>
#include <nprpc/page_handler.hpp>

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace nprpc::impl {

/// Split "/blog?page=2" into "/blog" and "page=2".
inline std::pair<std::string_view, std::string_view>
split_page_target(std::string_view target) noexcept
{
  const auto q = target.find('?');
  if (q == std::string_view::npos) return {target, {}};
  return {target.substr(0, q), target.substr(q + 1)};
}

/// Lowercase a header field name so handlers can look headers up by a known key.
inline std::string to_lower_copy(std::string_view s)
{
  std::string out(s);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return out;
}

/// True when a page handler is installed and could plausibly own this request.
///
/// Framework paths (/rpc, /_nprpc/...) are excluded here rather than left to
/// the handler, so a catch-all route cannot shadow them by accident.
NPRPC_API bool page_handler_applies(std::string_view method,
                                    std::string_view path) noexcept;

/// Run the configured page handler.  Returns nullopt when there is no handler,
/// when it declines the request, or when it throws — in every case the caller
/// falls through to its normal routing.
NPRPC_API std::optional<PageResponse>
invoke_page_handler(std::string_view method,
                    std::string_view target,
                    std::map<std::string, std::string> headers,
                    std::string body,
                    std::string_view client_address);

} // namespace nprpc::impl

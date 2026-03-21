// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <filesystem>
#include <optional>
#include <string_view>

namespace nprpc::impl {

/// Return a reasonable MIME type based on the file extension.
/// The returned string_view points to static storage and is always valid.
std::string_view mime_type(std::string_view path);

/// Resolve a request target under the configured HTTP doc root.
/// Returns std::nullopt when the target is malformed or escapes doc_root,
/// including via symlink traversal.
std::optional<std::filesystem::path>
resolve_http_doc_root_path(std::string_view doc_root,
						   std::string_view request_target) noexcept;

/// Returns true when the request target addresses the HTTP RPC endpoint.
bool is_rpc_http_target(std::string_view path) noexcept;

/// Returns the request origin if it is allowed by the configured HTTP CORS
/// allowlist. Empty result means CORS should not be granted.
std::optional<std::string_view>
get_allowed_http_origin(std::string_view origin) noexcept;

/// Returns true when a browser origin is acceptable for stateful upgrades.
/// Empty origins are treated as non-browser clients and allowed.
/// Same-origin requests are always allowed. Cross-origin requests must appear
/// in the configured HTTP allowlist.
bool is_allowed_browser_origin(std::string_view origin,
							   std::string_view scheme,
							   std::string_view authority) noexcept;

} // namespace nprpc::impl

// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string_view>

namespace nprpc::impl {

inline constexpr std::size_t k_max_http_request_body_size = 10'000;

/// Cache-Control sent with static files, over HTTP/1.1 and HTTP/3 alike.
/// Browsers revalidate with the ETag once it runs out.
inline constexpr std::string_view k_static_file_cache_control =
    "public, max-age=3600";

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

/// Parses a decimal Content-Length header value without throwing.
/// Returns std::nullopt for empty, non-numeric, or out-of-range values.
std::optional<std::size_t>
parse_http_content_length(std::string_view value) noexcept;

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

/// Parse an HTTP-date in the preferred IMF-fixdate format
/// ("Mon, 18 Apr 2026 12:34:56 GMT"). Returns std::nullopt for anything
/// else. No heap allocation.
std::optional<std::filesystem::file_time_type>
parse_http_date(std::string_view s) noexcept;

/// Whether a file last modified at @p mtime is unchanged since the
/// If-Modified-Since date @p http_date. Compares whole seconds: that is all
/// Last-Modified carries, so a client echoing it back must match.
bool not_modified_since(std::filesystem::file_time_type mtime,
                        std::string_view http_date) noexcept;

/// Whether an If-None-Match header value matches @p etag: "*", or any tag
/// in its comma-separated list, compared weakly (W/ prefixes ignored), as
/// RFC 9110 §13.1.2 requires for GET and HEAD.
bool if_none_match_matches(std::string_view if_none_match,
                           std::string_view etag) noexcept;

} // namespace nprpc::impl

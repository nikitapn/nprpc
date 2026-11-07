// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nprpc::impl {

inline constexpr std::size_t k_max_http_request_body_size = 10'000;

/// Cache-Control sent with static files that no cache_control() rule
/// matches, over HTTP/1.1 and HTTP/3 alike. Browsers revalidate with the ETag
/// once it runs out.
inline constexpr std::string_view k_static_file_cache_control =
    "public, max-age=3600";

/// (pattern, Cache-Control value) pairs, tried in order; see
/// select_static_cache_control().
using CacheControlRules = std::vector<std::pair<std::string, std::string>>;

/// Whether a URL path matches a cache_control() pattern.
/// A pattern without '/' is matched against the last path segment
/// ("*.html", "favicon.ico"); one with '/' against the whole path
/// ("/assets/**", "/static/*.css"). '*' matches any run of characters within
/// a segment, '**' any run across segments, '?' one character within a
/// segment. The match is case-sensitive, like the paths it serves.
bool cache_control_pattern_matches(std::string_view pattern,
                                   std::string_view path) noexcept;

/// The Cache-Control value for a static file at request path @p path (query
/// and fragment ignored): the value of the first rule whose pattern matches,
/// or k_static_file_cache_control. An empty result means send no
/// Cache-Control. Points into @p rules or static storage.
std::string_view select_static_cache_control(const CacheControlRules& rules,
                                             std::string_view path) noexcept;

/// A Range request header resolved against a representation of @p size
/// bytes (RFC 9110 §14).
struct ByteRangeSelection {
  enum class Kind : uint8_t {
    Full,          // no usable Range: send the whole representation, 200
    Partial,       // send [offset, offset + length), 206
    Unsatisfiable, // 416 with Content-Range: bytes */size
  };
  Kind kind = Kind::Full;
  uint64_t offset = 0;
  uint64_t length = 0;
};

/// Resolve a Range header value. Only a single "bytes" range is served
/// ("bytes=0-499", "bytes=500-", "bytes=-500"); anything else — another
/// unit, several ranges, a malformed value — gives Full, which RFC 9110
/// §14.2 allows a server to answer with the whole representation.
ByteRangeSelection select_byte_range(std::string_view range,
                                     uint64_t size) noexcept;

/// Whether an If-Range header value lets the Range apply (RFC 9110 §13.1.5):
/// an entity tag must equal @p etag by strong comparison (a weak tag never
/// matches); an HTTP-date must equal the Last-Modified of @p mtime, to the
/// second.
bool if_range_matches(std::string_view if_range,
                      std::string_view etag,
                      std::filesystem::file_time_type mtime) noexcept;

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

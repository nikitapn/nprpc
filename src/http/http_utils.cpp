// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include <nprpc/impl/http_utils.hpp>

#include <nprpc/impl/nprpc_impl.hpp>

#include <boost/beast/core/string.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <shared_mutex>
#include <ankerl/unordered_dense.h>

namespace nprpc::impl {

namespace {

bool path_is_within_root(const std::filesystem::path& root,
                        const std::filesystem::path& candidate) noexcept
{
  const auto relative = candidate.lexically_relative(root);
  if (relative.empty()) {
    return candidate == root;
  }

  for (const auto& component : relative) {
    if (component == "..") {
      return false;
    }
  }

  return !relative.is_absolute();
}

} // namespace

std::string_view mime_type(std::string_view path)
{
  using boost::beast::iequals;

  auto const pos = path.rfind(".");
  if (pos == std::string_view::npos)
    return "application/octet-stream";

  auto ext = path.substr(pos);

  if (iequals(ext, ".htm"))
    return "text/html";
  if (iequals(ext, ".html"))
    return "text/html";
  if (iequals(ext, ".woff2"))
    return "font/woff2";
  if (iequals(ext, ".php"))
    return "text/html";
  if (iequals(ext, ".css"))
    return "text/css";
  if (iequals(ext, ".txt"))
    return "text/plain";
  if (iequals(ext, ".pdf"))
    return "application/pdf";
  if (iequals(ext, ".js"))
    return "application/javascript";
  if (iequals(ext, ".json"))
    return "application/json";
  if (iequals(ext, ".xml"))
    return "application/xml";
  if (iequals(ext, ".swf"))
    return "application/x-shockwave-flash";
  if (iequals(ext, ".wasm"))
    return "application/wasm";
  if (iequals(ext, ".flv"))
    return "video/x-flv";
  if (iequals(ext, ".png"))
    return "image/png";
  if (iequals(ext, ".jpe"))
    return "image/jpeg";
  if (iequals(ext, ".jpeg"))
    return "image/jpeg";
  if (iequals(ext, ".jpg"))
    return "image/jpeg";
  if (iequals(ext, ".gif"))
    return "image/gif";
  if (iequals(ext, ".webp"))
    return "image/webp";
  if (iequals(ext, ".avif"))
    return "image/avif";
  if (iequals(ext, ".bmp"))
    return "image/bmp";
  if (iequals(ext, ".ico"))
    return "image/vnd.microsoft.icon";
  if (iequals(ext, ".tiff"))
    return "image/tiff";
  if (iequals(ext, ".tif"))
    return "image/tiff";
  if (iequals(ext, ".svg"))
    return "image/svg+xml";
  if (iequals(ext, ".svgz"))
    return "image/svg+xml";
  return "application/octet-stream";
}

std::optional<std::filesystem::path>
resolve_http_doc_root_path(std::string_view doc_root,
                           std::string_view request_target) noexcept
{
  namespace fs = std::filesystem;

  if (doc_root.empty() || request_target.empty() || request_target[0] != '/') {
    return std::nullopt;
  }

  const auto target_end = request_target.find_first_of("?#");
  const auto target_path = request_target.substr(0, target_end);
  if (target_path.empty() || target_path[0] != '/') {
    return std::nullopt;
  }

  // Cache canonical_root — doc_root is set once at startup and never changes.
  static fs::path s_canonical_root;
  static std::string s_cached_doc_root;
  static std::once_flag s_root_once;
  std::call_once(s_root_once, [&]() {
    std::error_code ec;
    s_canonical_root = fs::weakly_canonical(fs::path(doc_root), ec);
    if (!ec) s_cached_doc_root = doc_root;
  });

  if (s_canonical_root.empty() || s_cached_doc_root != doc_root) {
    return std::nullopt;
  }

  // Fast path: look up previously resolved path.
  // Uses shared_mutex so concurrent reads don't block each other.
  static std::shared_mutex s_cache_mutex;
  static ankerl::unordered_dense::map<std::string, fs::path> s_path_cache;

  {
    std::shared_lock lock(s_cache_mutex);
    auto it = s_path_cache.find(std::string(target_path));
    if (it != s_path_cache.end()) {
      return it->second;
    }
  }

  // Slow path: resolve and insert.
  const fs::path raw_target_path(target_path);
  for (const auto& component : raw_target_path) {
    if (component == "..") {
      return std::nullopt;
    }
  }

  fs::path relative_path;
  for (const auto& component : raw_target_path.lexically_normal()) {
    if (component.empty() || component == "/" || component == ".") {
      continue;
    }

    if (component == "..") {
      return std::nullopt;
    }

    relative_path /= component;
  }

  std::error_code ec;
  const fs::path resolved_path =
      fs::weakly_canonical(s_canonical_root / relative_path, ec);
  if (ec || resolved_path.empty() ||
      !path_is_within_root(s_canonical_root, resolved_path)) {
    return std::nullopt;
  }

  {
    std::unique_lock lock(s_cache_mutex);
    s_path_cache.emplace(target_path, resolved_path);
  }

  return resolved_path;
}

bool is_rpc_http_target(std::string_view path) noexcept
{
  return path == "/rpc" || path.starts_with("/rpc/");
}

std::optional<std::size_t>
parse_http_content_length(std::string_view value) noexcept
{
  if (value.empty()) {
    return std::nullopt;
  }

  std::size_t parsed = 0;
  const auto* begin = value.data();
  const auto* end = begin + value.size();
  const auto [ptr, ec] = std::from_chars(begin, end, parsed);
  if (ec != std::errc{} || ptr != end) {
    return std::nullopt;
  }

  return parsed;
}

std::optional<std::string_view>
get_allowed_http_origin(std::string_view origin) noexcept
{
  if (origin.empty() || g_cfg.http_allowed_origins.empty()) {
    return std::nullopt;
  }

  const auto it = std::find(g_cfg.http_allowed_origins.begin(),
                            g_cfg.http_allowed_origins.end(), origin);
  if (it == g_cfg.http_allowed_origins.end()) {
    return std::nullopt;
  }

  return origin;
}

bool is_allowed_browser_origin(std::string_view origin,
                               std::string_view scheme,
                               std::string_view authority) noexcept
{
  if (origin.empty()) {
    return true;
  }

  if (!scheme.empty() && !authority.empty()) {
    std::string same_origin;
    same_origin.reserve(scheme.size() + authority.size() + 3);
    same_origin.append(scheme);
    same_origin.append("://");
    same_origin.append(authority);
    if (origin == same_origin) {
      return true;
    }
  }

  return get_allowed_http_origin(origin).has_value();
}

// The ETag's opaque part, without a W/ weak prefix: weak comparison
// (RFC 9110 §8.8.3.2) ignores whether either side is weak.
static std::string_view etag_opaque(std::string_view tag) noexcept
{
  if (tag.size() >= 2 && tag[0] == 'W' && tag[1] == '/')
    tag.remove_prefix(2);
  return tag;
}

bool if_none_match_matches(std::string_view if_none_match,
                           std::string_view etag) noexcept
{
  const auto ours = etag_opaque(etag);
  while (!if_none_match.empty()) {
    const auto comma = if_none_match.find(',');
    auto tag = if_none_match.substr(0, comma);
    while (!tag.empty() && (tag.front() == ' ' || tag.front() == '\t'))
      tag.remove_prefix(1);
    while (!tag.empty() && (tag.back() == ' ' || tag.back() == '\t'))
      tag.remove_suffix(1);
    if (tag == "*" || (!tag.empty() && etag_opaque(tag) == ours))
      return true;
    if (comma == std::string_view::npos)
      break;
    if_none_match.remove_prefix(comma + 1);
  }
  return false;
}

std::optional<std::filesystem::file_time_type>
parse_http_date(std::string_view s) noexcept
{
  // "Mon, 18 Apr 2026 12:34:56 GMT" — fixed 29 bytes.
  //  0         1         2
  //  0123456789012345678901234567890
  if (s.size() != 29) return std::nullopt;

  // Validate all fixed separators and GMT suffix up front.
  if (s[3] != ',' || s[4] != ' ' || s[7] != ' ' || s[11] != ' ' ||
      s[16] != ' ' || s[19] != ':' || s[22] != ':' || s[25] != ' ' ||
      s[26] != 'G' || s[27] != 'M' || s[28] != 'T')
    return std::nullopt;

  // Parse fixed-width decimal. Uses unsigned subtraction — wraps on non-digit.
  auto parse_int = [&](int pos, int len) noexcept -> int {
    int val = 0;
    for (int i = 0; i < len; ++i) {
      const unsigned c =
          static_cast<unsigned>(static_cast<unsigned char>(s[pos + i])) - '0';
      if (c > 9) return -1;
      val = val * 10 + static_cast<int>(c);
    }
    return val;
  };

  // Month: pack s[8..10] into a uint32_t with a single 4-byte load.
  // s[11]==' ' is already validated above, so reading 4 bytes is safe.
  // Masking the high byte yields the same 3-char key as the constexpr table.
  static constexpr auto m3 = [](char a, char b, char c) constexpr -> uint32_t {
    return uint32_t(uint8_t(a)) | (uint32_t(uint8_t(b)) << 8)
           | (uint32_t(uint8_t(c)) << 16);
  };
  static constexpr uint32_t month_keys[12] = {
      m3('J','a','n'), m3('F','e','b'), m3('M','a','r'), m3('A','p','r'),
      m3('M','a','y'), m3('J','u','n'), m3('J','u','l'), m3('A','u','g'),
      m3('S','e','p'), m3('O','c','t'), m3('N','o','v'), m3('D','e','c'),
  };
  uint32_t mon;
  std::memcpy(&mon, s.data() + 8, 4); // single MOV r32; s[11]==' ' is valid
  mon &= 0x00FF'FFFFu;

  int month = -1;
  for (int i = 0; i < 12; ++i) {
    if (mon == month_keys[i]) { month = i; break; }
  }
  if (month == -1) return std::nullopt;

  const int day  = parse_int(5, 2);
  const int year = parse_int(12, 4);
  const int hour = parse_int(17, 2);
  const int min  = parse_int(20, 2);
  const int sec  = parse_int(23, 2);

  if (day < 1 || day > 31) return std::nullopt;
  if (year < 1970)         return std::nullopt;
  if (hour > 23)           return std::nullopt;
  if (min  > 59)           return std::nullopt;
  if (sec  > 60)           return std::nullopt; // 60 = leap second

  // Compute seconds since Unix epoch directly — no timegm, no struct tm.
  //
  // Leap days since 1970 up to but not including `year`:
  //   floor((y-1)/4) - floor((y-1)/100) + floor((y-1)/400) - 477
  //   (477 = same expression evaluated at y=1970)
  static constexpr int days_before_month[12] = {
      0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
  };
  const bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
  const int leap_days = (year - 1) / 4 - (year - 1) / 100 + (year - 1) / 400
                        - 477;

  const int64_t days = static_cast<int64_t>(year - 1970) * 365
                       + leap_days
                       + days_before_month[month]
                       + (month > 1 && leap ? 1 : 0)
                       + (day - 1);

  const int64_t epoch_sec = days * 86400
                            + static_cast<int64_t>(hour) * 3600
                            + min * 60
                            + sec;

  auto sys = std::chrono::sys_seconds{std::chrono::seconds{epoch_sec}};
  return std::chrono::clock_cast<std::filesystem::file_time_type::clock>(sys);
}

bool not_modified_since(std::filesystem::file_time_type mtime,
                        std::string_view http_date) noexcept
{
  const auto since = parse_http_date(http_date);
  return since &&
         std::chrono::floor<std::chrono::seconds>(mtime) <= *since;
}

} // namespace nprpc::impl

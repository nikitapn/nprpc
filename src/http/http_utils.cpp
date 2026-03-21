// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include <nprpc/impl/http_utils.hpp>

#include <nprpc/impl/nprpc_impl.hpp>

#include <boost/beast/core/string.hpp>

#include <algorithm>
#include <charconv>
#include <filesystem>

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

  std::error_code ec;
  const fs::path canonical_root = fs::weakly_canonical(fs::path(doc_root), ec);
  if (ec || canonical_root.empty()) {
    return std::nullopt;
  }

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

  const fs::path resolved_path =
      fs::weakly_canonical(canonical_root / relative_path, ec);
  if (ec || resolved_path.empty() ||
      !path_is_within_root(canonical_root, resolved_path)) {
    return std::nullopt;
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

} // namespace nprpc::impl

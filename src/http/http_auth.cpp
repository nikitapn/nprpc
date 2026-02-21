// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include <nprpc/http_auth.hpp>
#include <nprpc/session_context.h>

#include <string>

namespace nprpc::http {

// ── helpers ──────────────────────────────────────────────────────────────────

static SessionContext* try_get_ctx() noexcept
{
  try {
    return &nprpc::get_context();
  } catch (...) {
    return nullptr;
  }
}

// Parse "name=value; name2=value2 ; name3=value3" into the named value.
static std::optional<std::string>
parse_cookie(std::string_view cookies, std::string_view name)
{
  while (!cookies.empty()) {
    // Skip leading spaces
    while (!cookies.empty() && cookies.front() == ' ')
      cookies.remove_prefix(1);

    auto semi = cookies.find(';');
    auto pair = cookies.substr(0, semi);
    cookies   = semi != std::string_view::npos
                  ? cookies.substr(semi + 1)
                  : std::string_view{};

    auto eq = pair.find('=');
    if (eq == std::string_view::npos)
      continue;

    auto cookie_name = pair.substr(0, eq);
    // trim trailing spaces from name
    while (!cookie_name.empty() && cookie_name.back() == ' ')
      cookie_name.remove_suffix(1);

    if (cookie_name != name)
      continue;

    auto cookie_value = pair.substr(eq + 1);
    // trim surrounding spaces from value
    while (!cookie_value.empty() && cookie_value.front() == ' ')
      cookie_value.remove_prefix(1);
    while (!cookie_value.empty() && cookie_value.back() == ' ')
      cookie_value.remove_suffix(1);

    return std::string(cookie_value);
  }
  return std::nullopt;
}

// Build the Set-Cookie header value string.
static std::string build_set_cookie(std::string_view    name,
                                    std::string_view    value,
                                    const CookieOptions& opts)
{
  std::string hdr = std::string(name) + "=" + std::string(value);

  if (!opts.path.empty())
    hdr += "; Path=" + std::string(opts.path);
  if (!opts.domain.empty())
    hdr += "; Domain=" + std::string(opts.domain);
  if (opts.max_age.has_value())
    hdr += "; Max-Age=" + std::to_string(*opts.max_age);
  if (!opts.same_site.empty())
    hdr += "; SameSite=" + std::string(opts.same_site);
  if (opts.secure)
    hdr += "; Secure";
  if (opts.http_only)
    hdr += "; HttpOnly";

  return hdr;
}

// ── public API ───────────────────────────────────────────────────────────────

std::optional<std::string> get_cookie(std::string_view name)
{
  auto* ctx = try_get_ctx();
  if (!ctx || ctx->cookies.empty())
    return std::nullopt;
  return parse_cookie(ctx->cookies, name);
}

void set_cookie(std::string_view     name,
                std::string_view     value,
                const CookieOptions& opts)
{
  auto* ctx = try_get_ctx();
  if (!ctx)
    return; // not in an HTTP session — silently ignore
  ctx->set_cookies.push_back(build_set_cookie(name, value, opts));
}

void clear_cookie(std::string_view name,
                  std::string_view path,
                  std::string_view domain)
{
  set_cookie(name, "", {
    .http_only = true,
    .secure    = true,
    .max_age   = 0,
    .path      = path,
    .domain    = domain,
  });
}

} // namespace nprpc::http

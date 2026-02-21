// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

/**
 * @file http_auth.hpp
 * @brief Cookie-based authentication helpers for HTTP RPC sessions.
 *
 * These helpers are only meaningful inside an object servant method that
 * is being dispatched over an HTTP(S) transport.  They are safe but
 * silently no-op / return nullopt when called from a WebSocket, TCP,
 * shared-memory or UDP session.
 *
 * Usage (server side):
 *
 *   // Read:
 *   auto sid = nprpc::http::get_cookie("session_id");
 *   if (!sid) { throw nprpc::Exception("Not authenticated"); }
 *
 *   // Write (response will carry Set-Cookie: ...):
 *   nprpc::http::set_cookie("session_id", token, {
 *       .http_only = true,
 *       .secure    = true,
 *       .same_site = "Strict",
 *       .max_age   = 86400,
 *   });
 *
 * On the JavaScript side nothing extra is required: generated stubs already
 * send `credentials: 'include'` so the browser attaches cookies automatically.
 */

#include <nprpc/export.hpp>
#include <optional>
#include <string>
#include <string_view>

namespace nprpc::http {

/// Options for a Set-Cookie header.
struct CookieOptions {
  /// Prevent JS access (recommended for session tokens).
  bool http_only = true;
  /// Only send over HTTPS.
  bool secure = true;
  /// SameSite attribute — "Strict", "Lax", or "None".
  /// Use "None" together with secure=true for cross-site cookies.
  std::string_view same_site = "Strict";
  /// Cookie lifetime in seconds. Omit (nullopt) for a session cookie.
  std::optional<int> max_age;
  /// Path scope. Defaults to "/" (entire site).
  std::string_view path = "/";
  /// Domain scope. Empty = current host only.
  std::string_view domain;
};

/// Get a named cookie value from the current HTTP request.
/// Returns std::nullopt when:
///   - not inside an HTTP-dispatched servant call, or
///   - the named cookie is absent.
NPRPC_API std::optional<std::string> get_cookie(std::string_view name);

/// Queue a Set-Cookie header for the current HTTP response.
/// No-op when called from a non-HTTP session.
NPRPC_API void set_cookie(std::string_view name,
                          std::string_view value,
                          const CookieOptions& opts = {});

/// Remove a cookie on the client side by setting Max-Age=0.
NPRPC_API void clear_cookie(std::string_view name,
                             std::string_view path   = "/",
                             std::string_view domain = {});

} // namespace nprpc::http

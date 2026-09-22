// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>

namespace nprpc {

/// An HTTP request offered to the application for server-side rendering.
///
/// Headers are keyed by lowercase field name.  `path` and `query` are the
/// already-split halves of `target`, so a handler can switch on the path
/// without re-parsing.
struct PageRequest {
  /// "GET", "HEAD" or "POST".
  std::string method;
  /// Path and query as sent: "/blog?page=2".
  std::string target;
  /// "/blog"
  std::string path;
  /// "page=2" (no leading '?')
  std::string query;
  /// Request headers, keyed by lowercase name.
  std::map<std::string, std::string> headers;
  /// Empty for GET/HEAD.
  std::string body;
  /// Peer IP; empty if unavailable.
  std::string client_address;
};

/// The application's answer for a PageRequest.
struct PageResponse {
  /// HTTP status code.
  unsigned status = 200;
  /// Response headers. Content-Type defaults to HTML; Content-Length is
  /// set by the server.
  std::map<std::string, std::string> headers;
  /// The response body.
  std::string body;
};

/// Renders a page in-process, on the HTTP server's own thread.
///
/// Returning `std::nullopt` means "not mine" — the server carries on with
/// static file serving, so a handler can claim `/blog` and leave `/assets/...`
/// to the zero-copy file cache.
///
/// The call is synchronous and blocks an I/O thread, which suits a template
/// render (microseconds) but not a database round-trip.  Anything slow belongs
/// on another thread, with the page handler serving what it already has.
using PageHandler = std::function<std::optional<PageResponse>(const PageRequest&)>;

} // namespace nprpc

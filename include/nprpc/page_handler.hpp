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
  std::string method;         // "GET", "HEAD", "POST"
  std::string target;         // "/blog?page=2"
  std::string path;           // "/blog"
  std::string query;          // "page=2" (no leading '?')
  std::map<std::string, std::string> headers;
  std::string body;           // empty for GET/HEAD
  std::string client_address; // peer IP, empty if unavailable
};

/// The application's answer for a PageRequest.
struct PageResponse {
  unsigned status = 200;
  std::map<std::string, std::string> headers;
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

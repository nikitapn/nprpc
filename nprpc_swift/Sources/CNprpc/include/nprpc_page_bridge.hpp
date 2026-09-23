// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

// C ABI for nprpc::PageHandler.
//
// A page handler is a plain function pointer plus an opaque context, the same
// shape create_poa already uses for its executor hooks.  Going through C rather
// than std::function keeps Swift out of C++ closure lifetime rules: Swift hands
// over an @convention(c) trampoline and an Unmanaged box, and never has to own
// or free anything the server allocated.
//
// The response is written through setters on an opaque handle instead of being
// returned by value, so neither side has to agree on a struct layout or on who
// frees the body.

#pragma once

#include <cstddef>

extern "C" {

/// One HTTP request offered to the application.  Every pointer is valid only
/// for the duration of the call; copy anything you keep.
///
/// Header names are lowercased.  `header_names[i]` pairs with
/// `header_values[i]`.
struct nprpc_page_request {
  const char* method;         // "GET", "HEAD", "POST"
  const char* target;         // "/blog?page=2"
  const char* path;           // "/blog"
  const char* query;          // "page=2" (no leading '?'), "" when absent
  const char* body;           // not NUL-terminated; use body_len
  size_t body_len;
  const char* client_address; // "" when unavailable
  const char* const* header_names;
  const char* const* header_values;
  size_t header_count;
};

/// Set the response status.  Defaults to 200 if never called.
void nprpc_page_response_set_status(void* response, unsigned status);

/// Append a response header.  Setting the same name twice overwrites it.
void nprpc_page_response_set_header(void* response,
                                    const char* name,
                                    const char* value);

/// Set the response body.  Copies `len` bytes; safe to free `data` afterwards.
void nprpc_page_response_set_body(void* response, const char* data, size_t len);

/// Render one page.
///
/// Return true once the response has been filled in, or false to decline the
/// request — the server then falls through to static file serving, so
/// declining unknown paths is how a handler lets the zero-copy file cache
/// serve assets.
///
/// Called synchronously on an HTTP I/O thread, and on several of them at once
/// when the server runs a thread pool, so the implementation must be
/// thread-safe.
typedef bool (*nprpc_page_handler_fn)(void* ctx,
                                      const nprpc_page_request* request,
                                      void* response);

} // extern "C"

// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include <nprpc/impl/http_file_cache.hpp>
#include <nprpc/impl/http_request_throttler.hpp>
#include <nprpc/impl/telemetry.hpp>
#include <nprpc/impl/http_rpc_session.hpp>
#include <nprpc/impl/http_utils.hpp>
#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/impl/page_dispatch.hpp>
#ifdef NPRPC_WEBSOCKET_ENABLED
#include <nprpc/impl/websocket_session.hpp>
#endif
#ifdef NPRPC_SSL_ENABLED
#include <nprpc/impl/ssl.hpp>
#include <boost/asio/ssl.hpp>
#endif

#include <boost/beast/http.hpp>

#include <queue>
#include <format>

#include "../logging.hpp"

namespace nprpc::impl {


namespace {

constexpr std::size_t kHttpResponseChunkSize = 64 * 1024;

} // namespace

namespace http = beast::http;

//==============================================================================
// cached_file_body - A Beast body type for zero-copy cached file responses
//==============================================================================

/// Body type that serves data from HttpFileCache with zero-copy
struct cached_file_body {
  /// The type of the value returned by the body
  struct value_type {
    CachedFileGuard guard;
    // The bytes to send: the file, or a compressed copy of it. Either way
    // the pinned CachedFile owns them for as long as the guard lives.
    const std::uint8_t* bytes = nullptr;
    std::size_t length = 0;

    value_type() = default;
    explicit value_type(CachedFileGuard g,
                        const CachedFile::EncodedVariant* variant = nullptr)
        : guard(std::move(g))
    {
      if (variant) {
        bytes = variant->data.data();
        length = variant->data.size();
      } else if (guard) {
        bytes = guard->data();
        length = guard->size();
      }
    }

    /// Narrow the body to [offset, offset + len), for a 206. The caller has
    /// checked the range against size().
    void slice(std::uint64_t offset, std::uint64_t len) noexcept
    {
      bytes += offset;
      length = static_cast<std::size_t>(len);
    }

    bool is_open() const noexcept { return static_cast<bool>(guard); }
    std::uint64_t size() const noexcept { return length; }
  };

  /// Returns the size of the body
  static std::uint64_t size(value_type const& v) noexcept { return v.size(); }

  /// The algorithm used during serialization
  class writer
  {
    value_type const& body_;
    std::size_t offset_ = 0;

  public:
    using const_buffers_type = boost::asio::const_buffer;

    template <bool isRequest, class Fields>
    explicit writer(http::header<isRequest, Fields> const&, value_type const& b)
        : body_(b)
    {
    }

    void init(boost::system::error_code& ec) { ec = {}; }

    boost::optional<std::pair<const_buffers_type, bool>>
    get(boost::system::error_code& ec)
    {
      ec = {};

      if (!body_.bytes || offset_ >= body_.length) {
        return boost::none;
      }

      // Bound each serializer step so large responses do not turn into a
      // single oversized write on the underlying stream.
      auto remaining = body_.length - offset_;
      auto chunk_size = std::min(remaining, kHttpResponseChunkSize);
      auto result = std::make_pair(
          const_buffers_type(body_.bytes + offset_, chunk_size),
          chunk_size < remaining
      );
      offset_ += chunk_size;
      return result;
    }
  };
};

//==============================================================================

// Helper to add Alt-Svc header for HTTP/3 advertisement
// Format: Alt-Svc: h3=":port"; ma=86400
template <class Response> void add_alt_svc_header(Response& res)
{
  if (g_cfg.http3_enabled && g_cfg.listen_http_port != 0) {
    res.set("Alt-Svc",
            std::format("h3=\":{}\"; ma=86400", g_cfg.listen_http_port));
  }
}

// Whether a response of this media type varies by Accept-Encoding, i.e. we
// would compress it for a client that asked.
static bool response_varies_by_encoding(std::string_view content_type) noexcept
{
  return g_cfg.http_compression_enabled &&
         is_compressible_content_type(content_type);
}

// The coding to send a body of @p body_size bytes in, from the request's
// Accept-Encoding. Identity unless compression is on and the body is big
// enough to be worth it.
template <class Fields>
static ContentEncoding choose_response_encoding(Fields const& req_fields,
                                                std::size_t body_size)
{
  if (!g_cfg.http_compression_enabled ||
      body_size < g_cfg.http_compression_min_size) {
    return ContentEncoding::Identity;
  }
  auto const it = req_fields.find(http::field::accept_encoding);
  if (it == req_fields.end())
    return ContentEncoding::Identity;
  return negotiate_content_encoding(
      std::string_view(it->value().data(), it->value().size()));
}

// The generator hides the status, so read it here on the way out.
template <class Body, class Fields>
http::message_generator counted(http::response<Body, Fields>&& res)
{
  if (telemetry_enabled())
    record_http1_status(res.result_int());
  return http::message_generator(std::move(res));
}

// Handle RPC request over HTTP POST
template <class Body, class Allocator>
http::message_generator
handle_rpc_request(http::request<Body, http::basic_fields<Allocator>>& req,
                   const boost::asio::ip::address& remote_ip)
{
  auto const origin = std::string(req[http::field::origin]);
  auto const allowed_origin = get_allowed_http_origin(origin);

  auto const add_cors = [&](auto& res) {
    if (allowed_origin) {
      res.set(http::field::access_control_allow_origin, *allowed_origin);
      res.set("Access-Control-Allow-Credentials", "true");
      res.set(http::field::vary, "Origin");
    }
    res.set(http::field::access_control_allow_methods, "POST, OPTIONS");
    res.set(http::field::access_control_allow_headers, "Content-Type");
  };

  // Helper to create success response
  auto const rpc_response = [&](std::string&& body_data,
                                const std::vector<std::string>& set_cookies) {
    http::response<http::string_body> res{http::status::ok, req.version()};
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::content_type, "application/octet-stream");
    add_cors(res);
    for (const auto& sc : set_cookies)
    res.insert(http::field::set_cookie, sc);
    add_alt_svc_header(res);
    res.keep_alive(req.keep_alive());
    res.body() = std::move(body_data);
    res.prepare_payload();
    return res;
  };

  // Helper for error response
  auto const rpc_error = [&](beast::string_view what) {
    http::response<http::string_body> res{http::status::internal_server_error,
                                          req.version()};
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::content_type, "text/plain");
    add_cors(res);
    add_alt_svc_header(res);
    res.keep_alive(req.keep_alive());
    res.body() = std::string("RPC Error: ") + std::string(what);
    res.prepare_payload();
    return res;
  };

  auto const too_many_requests = [&](beast::string_view what) {
    http::response<http::string_body> res{http::status::too_many_requests,
                                          req.version()};
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::content_type, "text/plain");
    add_cors(res);
    add_alt_svc_header(res);
    res.keep_alive(req.keep_alive());
    res.body() = std::string(what);
    res.prepare_payload();
    return res;
  };

  if (!http_request_throttler().allow_http_rpc_request(
          remote_ip,
          g_cfg.http_rpc_max_requests_per_ip_per_second,
          g_cfg.http_rpc_max_requests_burst)) {
    record_http1_throttled();
    return counted(too_many_requests("HTTP RPC rate limit exceeded"));
  }

  try {
    // Extract request body as string
    std::string request_body = req.body();

    if (request_body.empty()) {
      return counted(rpc_error("Empty request body"));
    }

    // Extract incoming Cookie: header (empty string_view if absent)
    auto cookie_it = req.find(http::field::cookie);
    std::string_view cookies =
        cookie_it != req.end() ? std::string_view(cookie_it->value()) : std::string_view{};

    // Process RPC request
    std::string response_body;
    std::vector<std::string> set_cookies;
    if (!process_http_rpc(g_rpc->ioc(), request_body, response_body,
                          cookies, &set_cookies)) {
      return counted(rpc_error("Failed to process RPC request"));
    }

    return counted(rpc_response(std::move(response_body), set_cookies));

  } catch (const std::exception& e) {
    NPRPC_LOG_ERROR("HTTP RPC exception: {}", e.what());
    return counted(rpc_error(e.what()));
  }
}

// This function produces an HTTP response for the given
// request. The type of the response object depends on the
// contents of the request, so the interface requires the
// caller to pass a generic lambda for receiving the response.
template <class Body, class Allocator>
http::message_generator
handle_request(beast::string_view doc_root,
               http::request<Body, http::basic_fields<Allocator>>&& req,
               const boost::asio::ip::address& remote_ip)
{
  // Returns a bad request response
  auto const bad_request = [&req](beast::string_view why) {
    http::response<http::string_body> res{http::status::bad_request,
                                          req.version()};
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::content_type, "text/html");
    add_alt_svc_header(res);
    res.keep_alive(req.keep_alive());
    res.body() = std::string(why);
    res.prepare_payload();
    return res;
  };

  // Returns a not found response
  auto const not_found = [&req](beast::string_view target) {
    http::response<http::string_body> res{http::status::not_found,
                                          req.version()};
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::content_type, "text/html");
    add_alt_svc_header(res);
    res.keep_alive(req.keep_alive());
    res.body() = "The resource '" + std::string(target) + "' was not found.";
    res.prepare_payload();
    return res;
  };

  // Returns a server error response
  auto const server_error = [&req](beast::string_view what) {
    http::response<http::string_body> res{http::status::internal_server_error,
                                          req.version()};
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::content_type, "text/html");
    add_alt_svc_header(res);
    res.keep_alive(req.keep_alive());
    res.body() = "An error occurred: '" + std::string(what) + "'";
    res.prepare_payload();
    return res;
  };

  // Handle OPTIONS preflight for CORS
  if (req.method() == http::verb::options) {
    if (!is_rpc_http_target(req.target())) {
      return counted(bad_request("Unknown HTTP-method"));
    }

    auto const origin = std::string(req[http::field::origin]);
    auto const allowed_origin = get_allowed_http_origin(origin);
    if (!allowed_origin) {
      http::response<http::string_body> res{http::status::forbidden,
                                            req.version()};
      res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
      res.set(http::field::content_type, "text/plain");
      add_alt_svc_header(res);
      res.keep_alive(req.keep_alive());
      res.body() = "CORS origin denied";
      res.prepare_payload();
      return counted(std::move(res));
    }

    http::response<http::empty_body> res{http::status::no_content,
                                         req.version()};
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::access_control_allow_origin, *allowed_origin);
    res.set("Access-Control-Allow-Credentials", "true");
    res.set(http::field::vary, "Origin");
    res.set(http::field::access_control_allow_methods, "POST, OPTIONS");
    res.set(http::field::access_control_allow_headers, "Content-Type");
    res.set(http::field::access_control_max_age, "86400"); // 24 hours
    add_alt_svc_header(res);
    res.keep_alive(req.keep_alive());
    return counted(std::move(res));
  }

  // Make sure we can handle the method
  if (req.method() != http::verb::get && req.method() != http::verb::post &&
      req.method() != http::verb::head)
    return counted(bad_request("Unknown HTTP-method"));

  // Check if this is an RPC request (POST to /rpc or /rpc/*)
  if (req.method() == http::verb::post && is_rpc_http_target(req.target())) {
    return handle_rpc_request(req, remote_ip);
  }

  // In-process page rendering.  Declining (nullopt) falls through to the
  // static file cache, so assets keep their zero-copy path.
  {
    std::string_view target_sv(req.target().data(), req.target().size());
    const auto [page_path, page_query] = split_page_target(target_sv);
    std::string_view method_sv = req.method() == http::verb::get    ? "GET"
                                 : req.method() == http::verb::head ? "HEAD"
                                                                    : "POST";

    if (page_handler_applies(method_sv, page_path)) {
      std::map<std::string, std::string> headers;
      for (const auto& field : req) {
        headers[to_lower_copy(field.name_string())] = std::string(field.value());
      }

      auto page = invoke_page_handler(method_sv, target_sv, std::move(headers),
                                      std::string(req.body()),
                                      remote_ip.to_string());
      if (page) {
        http::response<http::string_body> res{
            static_cast<http::status>(page->status), req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        for (const auto& [key, value] : page->headers) {
          if (key != "content-length" && key != "transfer-encoding") {
            res.set(key, value);
          }
        }
        if (res.find(http::field::content_type) == res.end()) {
          res.set(http::field::content_type, "text/html; charset=utf-8");
        }
        add_alt_svc_header(res);
        res.keep_alive(req.keep_alive());

        // Compress the rendered body unless the handler already encoded it
        // itself, or the response has no body to speak of.
        auto const content_type = std::string_view(
            res[http::field::content_type].data(),
            res[http::field::content_type].size());
        if (res.find(http::field::content_encoding) == res.end() &&
            page->status != 204 && page->status != 304 &&
            response_varies_by_encoding(content_type)) {
          res.set(http::field::vary, "accept-encoding");
          auto const encoding = choose_response_encoding(req, page->body.size());
          if (encoding != ContentEncoding::Identity) {
            auto compressed = compress_body(
                reinterpret_cast<const std::uint8_t*>(page->body.data()),
                page->body.size(), encoding);
            if (compressed && compressed->size() < page->body.size()) {
              res.set(http::field::content_encoding,
                      content_encoding_token(encoding));
              page->body.assign(
                  reinterpret_cast<const char*>(compressed->data()),
                  compressed->size());
            }
          }
        }
        // A HEAD response carries the headers of the GET but no body; Beast
        // still needs the length, so set it before clearing.
        if (req.method() == http::verb::head) {
          res.content_length(page->body.size());
        } else {
          res.body() = std::move(page->body);
          res.prepare_payload();
        }
        return counted(std::move(res));
      }
    }
  }

  if (g_cfg.http_root_dir.empty())
    return counted(bad_request("Illegal request: only Upgrade is allowed"));

  if (req.target().empty() || req.target()[0] != '/')
    return counted(bad_request("Illegal request-target"));

  auto request_target = std::string_view(req.target().data(), req.target().size());
  if (req.target().length() == 1 && req.target().back() == '/') {
    request_target = "/index.html";
  }

  auto path = resolve_http_doc_root_path(doc_root, request_target);
  if (!path) {
    return counted(not_found(req.target()));
  }

  // Get file from cache (zero-copy)
  auto cached_file = get_file_cache().get(*path);
  if (!cached_file) {
    return counted(not_found(req.target()));
  }

  auto const content_type = cached_file->content_type();
  auto const cache_control =
      select_static_cache_control(g_cfg.http_cache_control_rules, request_target);

  // A Range applies to GET only (RFC 9110 §14.2), and with an If-Range only
  // while the file is the one the client already holds part of. Ranges are
  // served from the file itself, never a compressed copy, so a client that
  // resumes a download gets byte offsets into what it can store.
  ByteRangeSelection range{};
  if (req.method() == http::verb::get) {
    if (auto const r = req.find(http::field::range); r != req.end()) {
      auto const ir = req.find(http::field::if_range);
      if (ir == req.end() ||
          if_range_matches(std::string_view(ir->value().data(), ir->value().size()),
                           cached_file->etag(), cached_file->mtime())) {
        range = select_byte_range(
            std::string_view(r->value().data(), r->value().size()),
            cached_file->size());
      }
    }
  }
  auto const ranged = range.kind != ByteRangeSelection::Kind::Full;

  // Pick the representation first: a compressed copy carries its own ETag,
  // and a conditional request is answered for the one we would send. The
  // copy is built once per cached file, then served zero-copy.
  const CachedFile::EncodedVariant* variant = nullptr;
  if (!ranged && cached_file->compressible()) {
    auto const encoding = choose_response_encoding(req, cached_file->size());
    if (encoding != ContentEncoding::Identity)
      variant = cached_file->encoded(encoding);
  }
  auto const etag =
      variant ? std::string_view{variant->etag} : cached_file->etag();
  auto const varies = response_varies_by_encoding(content_type);

  // Validators and caching policy, the same on a 200, a HEAD and a 304.
  auto const set_cache_headers = [&](auto& res) {
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::etag, etag);
    res.set(http::field::last_modified, cached_file->last_modified_str());
    if (!cache_control.empty())
      res.set(http::field::cache_control, cache_control);
    res.set(http::field::accept_ranges, "bytes");
    if (varies)
      res.set(http::field::vary, "accept-encoding");
    add_alt_svc_header(res);
    res.keep_alive(req.keep_alive());
  };

  // Conditional GET: If-None-Match takes precedence over If-Modified-Since
  // (RFC 9110 §13.2.2).
  bool not_modified = false;
  if (auto const inm = req.find(http::field::if_none_match); inm != req.end()) {
    not_modified = if_none_match_matches(
        std::string_view(inm->value().data(), inm->value().size()), etag);
  } else if (auto const ims = req.find(http::field::if_modified_since);
             ims != req.end()) {
    not_modified = not_modified_since(
        cached_file->mtime(),
        std::string_view(ims->value().data(), ims->value().size()));
  }
  if (not_modified) {
    http::response<http::empty_body> res{http::status::not_modified,
                                         req.version()};
    set_cache_headers(res);
    return counted(std::move(res));
  }

  // Range: one satisfiable range gets a 206 with that slice, one past the
  // end a 416; anything else falls through to the whole file.
  if (range.kind == ByteRangeSelection::Kind::Unsatisfiable) {
    http::response<http::empty_body> res{http::status::range_not_satisfiable,
                                         req.version()};
    set_cache_headers(res);
    res.set(http::field::content_range,
            std::format("bytes */{}", cached_file->size()));
    res.content_length(0);
    return counted(std::move(res));
  }
  if (range.kind == ByteRangeSelection::Kind::Partial) {
    http::response<cached_file_body> res{http::status::partial_content,
                                         req.version()};
    set_cache_headers(res);
    res.set(http::field::content_type, content_type);
    res.set(http::field::content_range,
            std::format("bytes {}-{}/{}", range.offset,
                        range.offset + range.length - 1, cached_file->size()));
    res.body() = cached_file_body::value_type(std::move(cached_file));
    res.body().slice(range.offset, range.length);
    res.content_length(range.length);
    return counted(std::move(res));
  }

  auto const length = variant ? variant->data.size() : cached_file->size();

  // HEAD gets exactly the headers a GET would.
  if (req.method() == http::verb::head) {
    http::response<http::empty_body> res{http::status::ok, req.version()};
    set_cache_headers(res);
    res.set(http::field::content_type, content_type);
    if (variant)
      res.set(http::field::content_encoding,
              content_encoding_token(variant->encoding));
    res.content_length(length);
    return counted(std::move(res));
  }

  // Respond to GET request using cached file body (zero-copy)
  http::response<cached_file_body> res{http::status::ok, req.version()};
  set_cache_headers(res);
  res.set(http::field::content_type, content_type);
  if (variant)
    res.set(http::field::content_encoding,
            content_encoding_token(variant->encoding));
  res.body() = cached_file_body::value_type(std::move(cached_file), variant);
  res.content_length(length);
  return counted(std::move(res));
}

// Handles an HTTP server connection.
// This uses the Curiously Recurring Template Pattern so that
// the same code works with both SSL streams and regular sockets.
template <class Derived> class http_session
{
  std::shared_ptr<std::string const> doc_root_;

  // Access the derived class, this is part of
  // the Curiously Recurring Template Pattern idiom.
  Derived& derived() { return static_cast<Derived&>(*this); }

  static constexpr auto timeout_sec = std::chrono::seconds(6);
  static constexpr std::size_t queue_limit = 8; // max responses
  std::queue<http::message_generator> response_queue_;

  // The parser is stored in an optional container so we can
  // construct it from scratch it at the beginning of each new message.
  boost::optional<http::request_parser<http::string_body>> parser_;

  // The peer's address, looked up on the first request: it can't change for
  // the life of the connection, so one getpeername() serves every request.
  boost::optional<net::ip::address> remote_ip_;

  // The first request gets timeout_sec like the handshake; the reads after
  // it wait out the keep-alive idle time instead.
  bool first_request_ = true;

protected:
  flat_buffer buffer_;

public:
  // Construct the session
  http_session(flat_buffer buffer,
               std::shared_ptr<std::string const> const& doc_root)
      : doc_root_(doc_root)
      , buffer_(std::move(buffer))
  {
  }

  void do_read()
  {
    // Construct a new parser for each message
    parser_.emplace();

    // Apply a reasonable limit to the allowed size
    // of the body in bytes to prevent abuse.
    parser_->body_limit(g_cfg.http_max_request_body_size);

    // Set the timeout. Only the read is affected while a response is still
    // being written: expires_after leaves a pending operation's timer alone.
    beast::get_lowest_layer(derived().stream()).expires_after(
        first_request_
            ? timeout_sec
            : std::chrono::seconds(g_cfg.http_keep_alive_timeout_sec));

    // Read a request using the parser-oriented interface
    http::async_read(derived().stream(), buffer_, *parser_,
                     beast::bind_front_handler(&http_session::on_read,
                                               derived().shared_from_this()));
  }

  void on_read(beast::error_code ec, std::size_t bytes_transferred)
  {
    boost::ignore_unused(bytes_transferred);

    // This means they closed the connection
    if (ec == http::error::end_of_stream)
      return derived().do_eof();

    if (ec)
      return fail(ec, "read");

    if (!remote_ip_) {
      // Not the throwing overload: a peer that resets the connection right
      // after its request makes getpeername() fail with ENOTCONN, and an
      // exception here would escape io_context::run() and end the process.
      beast::error_code ep_ec;
      const auto remote_ep =
          beast::get_lowest_layer(derived().stream()).socket().remote_endpoint(
              ep_ec);
      if (ep_ec)
        return fail(ep_ec, "remote_endpoint");
      remote_ip_ = remote_ep.address();
    }
    const auto remote_ip = *remote_ip_;

    // See if it is a WebSocket Upgrade
    if (beast::websocket::is_upgrade(parser_->get())) {
#ifndef NPRPC_WEBSOCKET_ENABLED
      {
        auto& req = parser_->get();
        http::response<http::string_body> res{http::status::not_implemented,
                                              req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/plain");
        add_alt_svc_header(res);
        res.keep_alive(false);
        res.body() = "WebSocket support is not compiled in";
        res.prepare_payload();
        queue_write(std::move(res));
        return;
      }
#else
      auto& req = parser_->get();
      auto const origin = std::string(req[http::field::origin]);
      auto const host = std::string(req[http::field::host]);
      auto const scheme = derived().is_secure() ? std::string_view{"https"}
                                                : std::string_view{"http"};
      if (!is_allowed_browser_origin(origin, scheme, host)) {
        http::response<http::string_body> res{http::status::forbidden,
                                              req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/plain");
        add_alt_svc_header(res);
        res.keep_alive(false);
        res.body() = "WebSocket origin denied";
        res.prepare_payload();
        queue_write(std::move(res));
        return;
      }

      if (!http_request_throttler().allow_websocket_upgrade(
              remote_ip,
              g_cfg.http_websocket_upgrades_per_ip_per_second,
              g_cfg.http_websocket_upgrades_burst)) {
        record_throttled(monitor::Transport::WebSocket);
        http::response<http::string_body> res{http::status::too_many_requests,
                                              req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/plain");
        add_alt_svc_header(res);
        res.keep_alive(false);
        res.body() = "WebSocket upgrade rate limit exceeded";
        res.prepare_payload();
        queue_write(std::move(res));
        return;
      }

      if (!http_request_throttler().try_acquire_websocket_session(
              remote_ip,
              g_cfg.http_websocket_max_active_sessions_per_ip)) {
        record_throttled(monitor::Transport::WebSocket);
        http::response<http::string_body> res{http::status::too_many_requests,
                                              req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/plain");
        add_alt_svc_header(res);
        res.keep_alive(false);
        res.body() = "WebSocket session limit exceeded";
        res.prepare_payload();
        queue_write(std::move(res));
        return;
      }

      // Disable the timeout.
      // The websocket::stream uses its own timeout settings.
      beast::get_lowest_layer(derived().stream()).expires_never();

      // Create a websocket session, transferring ownership
      // of both the socket and the HTTP request.
      return make_accepting_websocket_session(derived().release_stream(),
                                              parser_->release(),
                                              remote_ip);
#endif // NPRPC_WEBSOCKET_ENABLED
    }

    first_request_ = false;

    // Every response mirrors the request's keep-alive, so clearing it here
    // makes the response carry "Connection: close" and on_write close.
    if (g_cfg.http_keep_alive_timeout_sec == 0)
      parser_->get().keep_alive(false);
    const bool keep_alive = parser_->get().keep_alive();

    // Send the response
    queue_write(handle_request(*doc_root_, parser_->release(), remote_ip));

    // If we aren't at the queue limit, try to pipeline another request. Not
    // on a connection that closes after this response: that read could only
    // time out, and a timed-out read closes the socket under the write.
    if (keep_alive && response_queue_.size() < queue_limit)
      do_read();
  }

  void queue_write(http::message_generator response)
  {
    // Allocate and store the work
    response_queue_.push(std::move(response));

    // If there was no previous work, start the write loop
    if (response_queue_.size() == 1)
      do_write();
  }

  // Called to start/continue the write-loop. Should not be called when
  // write_loop is already active.
  void do_write()
  {
    // Always reset the timeout on the underlying stream before we start a
    // new write probably is related to this issue:
    // https://github.com/boostorg/beast/issues/1599
    beast::get_lowest_layer(derived().stream()).expires_after(timeout_sec);

    if (!response_queue_.empty()) {
      bool keep_alive = response_queue_.front().keep_alive();

      beast::async_write(derived().stream(), std::move(response_queue_.front()),
                         beast::bind_front_handler(&http_session::on_write,
                                                   derived().shared_from_this(),
                                                   keep_alive));
    }
  }

  void
  on_write(bool keep_alive, beast::error_code ec, std::size_t bytes_transferred)
  {
    boost::ignore_unused(bytes_transferred);

    if (ec)
      return fail(ec, "write");

    if (!keep_alive) {
      // This means we should close the connection, usually because
      // the response indicated the "Connection: close" semantic.
      return derived().do_eof();
    }

    // Resume the read if it has been paused
    if (response_queue_.size() == queue_limit)
      do_read();

    response_queue_.pop();

    do_write();
  }
};

//------------------------------------------------------------------------------

// Handles a plain HTTP connection
class plain_http_session
    : public http_session<plain_http_session>,
      public std::enable_shared_from_this<plain_http_session>
{
  beast_tcp_stream_strand stream_;

public:
  // Create the session
  plain_http_session(beast_tcp_stream_strand&& stream,
                     flat_buffer&& buffer,
                     std::shared_ptr<std::string const> const& doc_root)
      : http_session<plain_http_session>(std::move(buffer), doc_root)
      , stream_(std::move(stream))
  {
  }

  // Start the session
  void run() { this->do_read(); }

  // Called by the base class
  beast_tcp_stream_strand& stream() { return stream_; }

  bool is_secure() const noexcept { return false; }

  // Called by the base class
  beast_tcp_stream_strand release_stream() { return std::move(stream_); }

  // Called by the base class
  void do_eof()
  {
    // Send a TCP shutdown
    beast::error_code ec;
    stream_.socket().shutdown(tcp::socket::shutdown_send, ec);

    // At this point the connection is closed gracefully
  }
};

//------------------------------------------------------------------------------

#ifdef NPRPC_SSL_ENABLED
// Handles an SSL HTTP connection
class ssl_http_session : public http_session<ssl_http_session>,
                         public std::enable_shared_from_this<ssl_http_session>
{
  beast::ssl_stream<beast_tcp_stream_strand> stream_;

public:
  // Create the http_session
  ssl_http_session(beast_tcp_stream_strand&& stream,
                   net::ssl::context& ctx,
                   flat_buffer&& buffer,
                   std::shared_ptr<std::string const> const& doc_root)
      : http_session<ssl_http_session>(std::move(buffer), doc_root)
      , stream_(std::move(stream), ctx)
  {
  }

  // Start the session
  void run()
  {
    // Set the timeout.
    beast::get_lowest_layer(stream_).expires_after(std::chrono::seconds(6));

    // Perform the SSL handshake
    // Note, this is the buffered version of the handshake.
    stream_.async_handshake(
        net::ssl::stream_base::server, buffer_.data(),
        beast::bind_front_handler(&ssl_http_session::on_handshake,
                                  shared_from_this()));
  }

  // Called by the base class
  beast::ssl_stream<beast_tcp_stream_strand>& stream() { return stream_; }

  bool is_secure() const noexcept { return true; }

  // Called by the base class
  beast::ssl_stream<beast_tcp_stream_strand> release_stream()
  {
    return std::move(stream_);
  }

  // Called by the base class
  void do_eof()
  {
    // Set the timeout.
    beast::get_lowest_layer(stream_).expires_after(std::chrono::seconds(6));

    // Perform the SSL shutdown
    stream_.async_shutdown(beast::bind_front_handler(
        &ssl_http_session::on_shutdown, shared_from_this()));
  }

private:
  void on_handshake(beast::error_code ec, std::size_t bytes_used)
  {
    if (ec) {
      record_http1_handshake_failure();
      return fail(ec, "handshake");
    }

    // Consume the portion of the buffer used by the handshake
    buffer_.consume(bytes_used);

    do_read();
  }

  void on_shutdown(beast::error_code ec)
  {
    if (ec)
      return fail(ec, "shutdown");

    // At this point the connection is closed gracefully
  }
};
#endif // NPRPC_SSL_ENABLED

//------------------------------------------------------------------------------

// Detects SSL handshakes
class detect_session : public std::enable_shared_from_this<detect_session>
{
  beast_tcp_stream_strand stream_;
#ifdef NPRPC_SSL_ENABLED
  // Pinned for the lifetime of the session: the context this connection was
  // accepted with stays valid even if a certificate reload publishes a new
  // one while the handshake is still in flight.
  std::shared_ptr<net::ssl::context> ctx_;
#endif
  std::shared_ptr<std::string const> doc_root_;
  flat_buffer buffer_;

public:
  explicit detect_session(beast_tcp_stream_strand&& socket,
#ifdef NPRPC_SSL_ENABLED
                          std::shared_ptr<net::ssl::context> ctx,
#endif
                          std::shared_ptr<std::string const> const& doc_root)
      : stream_(std::move(socket))
#ifdef NPRPC_SSL_ENABLED
      , ctx_(std::move(ctx))
#endif
      , doc_root_(doc_root)
  {
  }

  // Launch the detector
  void run()
  {
    // We need to be executing within a strand to perform async operations
    // on the I/O objects in this session. Although not strictly necessary
    // for single-threaded contexts, this example code is written to be
    // thread-safe by default.
    net::dispatch(stream_.get_executor(),
                  beast::bind_front_handler(&detect_session::on_run,
                                            this->shared_from_this()));
  }

  void on_run()
  {
#ifdef NPRPC_SSL_ENABLED
    // Set the timeout.
    stream_.expires_after(std::chrono::seconds(6));

    beast::async_detect_ssl(
        stream_, buffer_,
        beast::bind_front_handler(&detect_session::on_detect,
                                  this->shared_from_this()));
#else
    std::make_shared<plain_http_session>(std::move(stream_), std::move(buffer_),
                                         doc_root_)
        ->run();
#endif
  }

#ifdef NPRPC_SSL_ENABLED
  void on_detect(beast::error_code ec, bool result)
  {
    if (ec)
      return fail(ec, "detect");

    if (result) {
      if (!ctx_) {
        // TLS was offered but no server context is configured.
        NPRPC_LOG_ERROR("[HTTP] TLS ClientHello with no server SSL context");
        return;
      }
      // Launch SSL session
      std::make_shared<ssl_http_session>(std::move(stream_), *ctx_,
                                         std::move(buffer_), doc_root_)
          ->run();
      return;
    }

    // Launch plain session
    std::make_shared<plain_http_session>(std::move(stream_), std::move(buffer_),
                                         doc_root_)
        ->run();
  }
#endif
};

// Accepts incoming connections and launches the sessions
class listener : public std::enable_shared_from_this<listener>
{
  net::io_context& ioc_;
  tcp::acceptor acceptor_;
  std::shared_ptr<std::string const> doc_root_;
  bool running_ = true;

public:
  listener(net::io_context& ioc,
           tcp::endpoint endpoint,
           std::shared_ptr<std::string const> const& doc_root)
      : ioc_(ioc)
      , acceptor_(net::make_strand(ioc))
      , doc_root_(doc_root)
  {
    beast::error_code ec;

    // Open the acceptor
    acceptor_.open(endpoint.protocol(), ec);
    if (ec) {
      fail(ec, "open");
      return;
    }

    // Allow address reuse
    acceptor_.set_option(net::socket_base::reuse_address(true), ec);
    if (ec) {
      fail(ec, "set_option");
      return;
    }

    // Bind to the server address
    acceptor_.bind(endpoint, ec);
    if (ec) {
      fail(ec, "bind");
      return;
    }

    // Start listening for connections
    acceptor_.listen(net::socket_base::max_listen_connections, ec);
    if (ec) {
      fail(ec, "listen");
      return;
    }
  }

  // Start accepting incoming connections
  void run() { do_accept(); }

  void stop()
  {
    running_ = false;
    beast::error_code ec;
    acceptor_.cancel(ec);
    acceptor_.close(ec);
  }

private:
  void do_accept()
  {
    if (!running_)
      return;
    // The new connection gets its own strand
    acceptor_.async_accept(
        net::make_strand(ioc_),
        beast::bind_front_handler(&listener::on_accept, shared_from_this()));
  }

  void on_accept(beast::error_code ec, tcp_stream_strand socket)
  {
    if (ec) {
      if (ec != boost::asio::error::operation_aborted) {
        fail(ec, "accept");
      }
      return;
    }
    if (!running_)
      return;

    // Without TCP_NODELAY, Nagle holds back the last partial segment of each
    // response until the client ACKs the previous one, and the client delays
    // that ACK by up to 40 ms: every multi-segment response stalls for it.
    beast::error_code nodelay_ec;
    socket.set_option(tcp::no_delay(true), nodelay_ec);

    // Create the detector http_session and run it.
    // The SSL context is read here rather than cached in the listener, so a
    // certificate reload is picked up by the next connection.
    std::make_shared<detect_session>(beast_tcp_stream_strand(std::move(socket)),
#ifdef NPRPC_SSL_ENABLED
                                     server_ssl_context(),
#endif
                                     doc_root_)
        ->run();

    // Accept another connection
    do_accept();
  }
};

static std::shared_ptr<listener> g_http_listener;

void init_http_server(boost::asio::io_context& ioc)
{
  if (!nprpc::impl::g_cfg.listen_http_port)
    return;

  // Create and launch a listening port
  g_http_listener = std::make_shared<listener>(
      ioc,
      tcp::endpoint{net::ip::make_address(g_cfg.listen_address),
                    g_cfg.listen_http_port},
      std::make_shared<std::string const>(g_cfg.http_root_dir));
  g_http_listener->run();

  NPRPC_LOG_INFO("[HTTP/WebSocket] HTTP Server: Listening on {}:{} with root '{}'", g_cfg.listen_address,
                 g_cfg.listen_http_port, g_cfg.http_root_dir);
}

void stop_http_server()
{
  if (g_http_listener) {
    g_http_listener->stop();
    g_http_listener.reset();
  }
}

} // namespace nprpc::impl
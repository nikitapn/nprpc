// Copyright (c) 2021-2025 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by
// LICENSING file in the topmost directory

#include <nprpc/impl/websocket_session.hpp>
#include <nprpc/impl/http_rpc_session.hpp>
#include <nprpc/impl/nprpc_impl.hpp>

#include <boost/beast/http.hpp>

#include <queue>

namespace nprpc::impl {
// Return a reasonable mime type based on the extension of a file.
beast::string_view mime_type(beast::string_view path) {
  using beast::iequals;
  auto const ext = [&path] {
    auto const pos = path.rfind(".");
    if (pos == beast::string_view::npos)
      return beast::string_view{};
    return path.substr(pos);
  }();
  if (iequals(ext, ".htm")) return "text/html";
  if (iequals(ext, ".html")) return "text/html";
  if (iequals(ext, ".woff2")) return "font/woff2";
  if (iequals(ext, ".php")) return "text/html";
  if (iequals(ext, ".css")) return "text/css";
  if (iequals(ext, ".txt")) return "text/plain";
  if (iequals(ext, ".pdf")) return "application/pdf";
  if (iequals(ext, ".js")) return "application/javascript";
  if (iequals(ext, ".json")) return "application/json";
  if (iequals(ext, ".xml")) return "application/xml";
  if (iequals(ext, ".swf")) return "application/x-shockwave-flash";
  if (iequals(ext, ".wasm")) return "application/wasm";
  if (iequals(ext, ".flv")) return "video/x-flv";
  if (iequals(ext, ".png")) return "image/png";
  if (iequals(ext, ".jpe")) return "image/jpeg";
  if (iequals(ext, ".jpeg")) return "image/jpeg";
  if (iequals(ext, ".jpg")) return "image/jpeg";
  if (iequals(ext, ".gif")) return "image/gif";
  if (iequals(ext, ".bmp")) return "image/bmp";
  if (iequals(ext, ".ico")) return "image/vnd.microsoft.icon";
  if (iequals(ext, ".tiff")) return "image/tiff";
  if (iequals(ext, ".tif")) return "image/tiff";
  if (iequals(ext, ".svg")) return "image/svg+xml";
  if (iequals(ext, ".svgz")) return "image/svg+xml";
  return "application/text";
}

// Append an HTTP rel-path to a local filesystem path.
// The returned path is normalized for the platform.
std::string
path_cat(
    beast::string_view base,
    beast::string_view path) {
  if (base.empty())
    return std::string(path);
  std::string result(base);
#ifdef BOOST_MSVC
  char constexpr path_separator = '\\';
  if (result.back() == path_separator)
    result.resize(result.size() - 1);
  result.append(path.data(), path.size());
  for (auto &c : result)
    if (c == '/')
      c = path_separator;
#else
  char constexpr path_separator = '/';
  if (result.back() == path_separator)
    result.resize(result.size() - 1);
  result.append(path.data(), path.size());
#endif
  return result;
}

// Handle RPC request over HTTP POST
template <class Body, class Allocator>
http::message_generator handle_rpc_request(
    http::request<Body, http::basic_fields<Allocator>>& req) {
  
  // Helper to create success response
  auto const rpc_response = 
      [&req](std::string&& body_data, bool add_cors = true) {
        http::response<http::string_body> res{http::status::ok, req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "application/octet-stream");
        
        // Add CORS headers for cross-origin requests from browsers
        if (add_cors) {
          res.set(http::field::access_control_allow_origin, "*");
          res.set(http::field::access_control_allow_methods, "POST, OPTIONS");
          res.set(http::field::access_control_allow_headers, "Content-Type");
        }
        
        res.keep_alive(req.keep_alive());
        res.body() = std::move(body_data);
        res.prepare_payload();
        return res;
      };

  // Helper for error response
  auto const rpc_error =
      [&req](beast::string_view what) {
        http::response<http::string_body> res{http::status::internal_server_error, req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/plain");
        res.set(http::field::access_control_allow_origin, "*");
        res.keep_alive(req.keep_alive());
        res.body() = std::string("RPC Error: ") + std::string(what);
        res.prepare_payload();
        return res;
      };

  try {
    // Extract request body as string
    std::string request_body = req.body();
    
    if (request_body.empty()) {
      return rpc_error("Empty request body");
    }

    // Process RPC request
    std::string response_body;
    if (!process_http_rpc(g_orb->ioc(), request_body, response_body)) {
      return rpc_error("Failed to process RPC request");
    }

    if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
      std::cout << "HTTP RPC: Processed request, response size: " 
                << response_body.size() << " bytes" << std::endl;
    }

    return rpc_response(std::move(response_body));

  } catch (const std::exception& e) {
    std::cerr << "HTTP RPC exception: " << e.what() << std::endl;
    return rpc_error(e.what());
  }
}

// This function produces an HTTP response for the given
// request. The type of the response object depends on the
// contents of the request, so the interface requires the
// caller to pass a generic lambda for receiving the response.
template <class Body, class Allocator>
http::message_generator handle_request(
    beast::string_view doc_root,
    http::request<Body, http::basic_fields<Allocator>> &&req) {
  // Returns a bad request response
  auto const bad_request =
      [&req](beast::string_view why) {
        http::response<http::string_body> res{http::status::bad_request, req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/html");
        res.keep_alive(req.keep_alive());
        res.body() = std::string(why);
        res.prepare_payload();
        return res;
      };

  // Returns a not found response
  auto const not_found =
      [&req](beast::string_view target) {
        http::response<http::string_body> res{http::status::not_found, req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/html");
        res.keep_alive(req.keep_alive());
        res.body() = "The resource '" + std::string(target) + "' was not found.";
        res.prepare_payload();
        return res;
      };

  // Returns a server error response
  auto const server_error =
      [&req](beast::string_view what) {
        http::response<http::string_body> res{http::status::internal_server_error, req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/html");
        res.keep_alive(req.keep_alive());
        res.body() = "An error occurred: '" + std::string(what) + "'";
        res.prepare_payload();
        return res;
      };

  // Handle OPTIONS preflight for CORS
  if (req.method() == http::verb::options) {
    http::response<http::empty_body> res{http::status::no_content, req.version()};
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::access_control_allow_origin, "*");
    res.set(http::field::access_control_allow_methods, "GET, POST, OPTIONS");
    res.set(http::field::access_control_allow_headers, "Content-Type");
    res.set(http::field::access_control_max_age, "86400"); // 24 hours
    res.keep_alive(req.keep_alive());
    return res;
  }

  // Make sure we can handle the method
  if (req.method() != http::verb::get && req.method() != http::verb::post && req.method() != http::verb::head)
    return bad_request("Unknown HTTP-method");

  // Check if this is an RPC request (POST to /rpc or /rpc/*)
  if (req.method() == http::verb::post && 
      (req.target() == "/rpc" || req.target().starts_with("/rpc/"))) {
    return handle_rpc_request(req);
  }

  if (g_cfg.http_root_dir.empty())
    return bad_request("Illegal request: only Upgrade is allowed");

  // Request path must be absolute and not contain "..".
  if (req.target().empty() ||
      req.target()[0] != '/' ||
      req.target().find("..") != beast::string_view::npos)
    return bad_request("Illegal request-target");

  std::string path;

  if (req.target().length() == 1 && req.target().back() == '/') {
    path = path_cat(doc_root, "/index.html");
  } else if (
      !g_cfg.spa_links.empty() &&
      req.target().find('.') == std::string::npos) {
    if (std::find(std::begin(g_cfg.spa_links), std::end(g_cfg.spa_links), req.target()) == g_cfg.spa_links.end()) return not_found(req.target());
    path = path_cat(doc_root, "/index.html");
  } else {
    path = path_cat(doc_root, req.target());
  }

  // Attempt to open the file
  beast::error_code ec;
  http::file_body::value_type body;
  body.open(path.c_str(), beast::file_mode::scan, ec);

  // Handle the case where the file doesn't exist
  if (ec == beast::errc::no_such_file_or_directory)
    return not_found(req.target());

  // Handle an unknown error
  if (ec) return server_error(ec.message());

  // Cache the size since we need it after the move
  auto const size = body.size();

  // Respond to HEAD request
  if (req.method() == http::verb::head) {
    http::response<http::empty_body> res{http::status::ok, req.version()};
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::content_type, mime_type(path));
    res.content_length(size);
    res.keep_alive(req.keep_alive());
    return res;
  }

  // Respond to GET request
  http::response<http::file_body> res{
      std::piecewise_construct,
      std::make_tuple(std::move(body)),
      std::make_tuple(http::status::ok, req.version())};
  res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
  res.set(http::field::content_type, mime_type(path));
  res.content_length(size);
  res.keep_alive(req.keep_alive());
  return res;
}

// Handles an HTTP server connection.
// This uses the Curiously Recurring Template Pattern so that
// the same code works with both SSL streams and regular sockets.
template <class Derived>
class http_session {
  std::shared_ptr<std::string const> doc_root_;

  // Access the derived class, this is part of
  // the Curiously Recurring Template Pattern idiom.
  Derived &
  derived() {
    return static_cast<Derived &>(*this);
  }

  static constexpr auto timeout_sec = std::chrono::seconds(6);
  static constexpr std::size_t queue_limit = 8; // max responses
  std::queue<http::message_generator> response_queue_;

  // The parser is stored in an optional container so we can
  // construct it from scratch it at the beginning of each new message.
  boost::optional<http::request_parser<http::string_body>> parser_;

protected:
  beast::flat_buffer buffer_;

public:
  // Construct the session
  http_session(
      beast::flat_buffer buffer,
      std::shared_ptr<std::string const> const &doc_root)
    : doc_root_(doc_root)
    , buffer_(std::move(buffer)) 
  {
  }

  void
  do_read() {
    // Construct a new parser for each message
    parser_.emplace();

    // Apply a reasonable limit to the allowed size
    // of the body in bytes to prevent abuse.
    parser_->body_limit(10000);

    // Set the timeout.
    beast::get_lowest_layer(
        derived().stream())
        .expires_after(timeout_sec);

    // Read a request using the parser-oriented interface
    http::async_read(
        derived().stream(),
        buffer_,
        *parser_,
        beast::bind_front_handler(
            &http_session::on_read,
            derived().shared_from_this()));
  }

  void
  on_read(beast::error_code ec, std::size_t bytes_transferred) {
    boost::ignore_unused(bytes_transferred);

    // This means they closed the connection
    if (ec == http::error::end_of_stream)
      return derived().do_eof();

    if (ec)
      return fail(ec, "read");

    // See if it is a WebSocket Upgrade
    if (websocket::is_upgrade(parser_->get())) {
      // Disable the timeout.
      // The websocket::stream uses its own timeout settings.
      beast::get_lowest_layer(derived().stream()).expires_never();

      // Create a websocket session, transferring ownership
      // of both the socket and the HTTP request.
      return make_accepting_websocket_session(
        derived().release_stream(), parser_->release());
    }

    // Send the response
    queue_write(handle_request(*doc_root_, parser_->release()));

    // If we aren't at the queue limit, try to pipeline another request
    if (response_queue_.size() < queue_limit)
      do_read();
  }

  void
  queue_write(http::message_generator response) {
    // Allocate and store the work
    response_queue_.push(std::move(response));

    // If there was no previous work, start the write loop
    if (response_queue_.size() == 1)
      do_write();
  }

  // Called to start/continue the write-loop. Should not be called when
  // write_loop is already active.
  void
  do_write() {
    // Always reset the timeout on the underlying stream before we start a new write
    // probably is related to this issue:
    // https://github.com/boostorg/beast/issues/1599
    beast::get_lowest_layer(
        derived().stream())
        .expires_after(timeout_sec);

    if (!response_queue_.empty()) {
      bool keep_alive = response_queue_.front().keep_alive();

      beast::async_write(
          derived().stream(),
          std::move(response_queue_.front()),
          beast::bind_front_handler(
              &http_session::on_write,
              derived().shared_from_this(),
              keep_alive));
    }
  }

  void
  on_write(
      bool keep_alive,
      beast::error_code ec,
      std::size_t bytes_transferred) {
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
    public std::enable_shared_from_this<plain_http_session> {
  beast_tcp_stream_strand stream_;

public:
  // Create the session
  plain_http_session(
      beast_tcp_stream_strand &&stream,
      flat_buffer &&buffer,
      std::shared_ptr<std::string const> const &doc_root)
    : http_session<plain_http_session>(std::move(buffer), doc_root)
    , stream_(std::move(stream)) 
  {
  }

  // Start the session
  void run() {
    this->do_read();
  }

  // Called by the base class
  beast_tcp_stream_strand &stream() {
    return stream_;
  }

  // Called by the base class
  beast_tcp_stream_strand release_stream() {
    return std::move(stream_);
  }

  // Called by the base class
  void do_eof() {
    // Send a TCP shutdown
    beast::error_code ec;
    stream_.socket().shutdown(tcp::socket::shutdown_send, ec);

    // At this point the connection is closed gracefully
  }
};

//------------------------------------------------------------------------------

// Handles an SSL HTTP connection
class ssl_http_session
  : public http_session<ssl_http_session>,
    public std::enable_shared_from_this<ssl_http_session> {
  beast::ssl_stream<beast_tcp_stream_strand> stream_;

public:
  // Create the http_session
  ssl_http_session(
      beast_tcp_stream_strand &&stream,
      ssl::context &ctx,
      flat_buffer &&buffer,
      std::shared_ptr<std::string const> const &doc_root)
    : http_session<ssl_http_session>(std::move(buffer)
    , doc_root)
    , stream_(std::move(stream), ctx) 
    {}

  // Start the session
  void
  run() {
    // Set the timeout.
    beast::get_lowest_layer(stream_).expires_after(std::chrono::seconds(6));

    // Perform the SSL handshake
    // Note, this is the buffered version of the handshake.
    stream_.async_handshake(
        ssl::stream_base::server,
        buffer_.data(),
        beast::bind_front_handler(
            &ssl_http_session::on_handshake,
            shared_from_this()));
  }

  // Called by the base class
  beast::ssl_stream<beast_tcp_stream_strand> &
  stream() {
    return stream_;
  }

  // Called by the base class
  beast::ssl_stream<beast_tcp_stream_strand>
  release_stream() {
    return std::move(stream_);
  }

  // Called by the base class
  void
  do_eof() {
    // Set the timeout.
    beast::get_lowest_layer(stream_).expires_after(std::chrono::seconds(6));

    // Perform the SSL shutdown
    stream_.async_shutdown(
        beast::bind_front_handler(
            &ssl_http_session::on_shutdown,
            shared_from_this()));
  }

private:
  void
  on_handshake(
      beast::error_code ec,
      std::size_t bytes_used) {
    if (ec)
      return fail(ec, "handshake");

    // Consume the portion of the buffer used by the handshake
    buffer_.consume(bytes_used);

    do_read();
  }

  void
  on_shutdown(beast::error_code ec) {
    if (ec)
      return fail(ec, "shutdown");

    // At this point the connection is closed gracefully
  }
};

//------------------------------------------------------------------------------

// Detects SSL handshakes
class detect_session : public std::enable_shared_from_this<detect_session> {
  beast_tcp_stream_strand stream_;
  ssl::context &ctx_;
  std::shared_ptr<std::string const> doc_root_;
  beast::flat_buffer buffer_;

public:
  explicit detect_session(
      beast_tcp_stream_strand&& socket,
      ssl::context &ctx,
      std::shared_ptr<std::string const> const &doc_root)
    : stream_(std::move(socket))
    , ctx_(ctx)
    , doc_root_(doc_root) {
  }

  // Launch the detector
  void
  run() {
    // We need to be executing within a strand to perform async operations
    // on the I/O objects in this session. Although not strictly necessary
    // for single-threaded contexts, this example code is written to be
    // thread-safe by default.
    net::dispatch(
        stream_.get_executor(),
        beast::bind_front_handler(
            &detect_session::on_run,
            this->shared_from_this()));
  }

  void
  on_run() {
    // Set the timeout.
    stream_.expires_after(std::chrono::seconds(6));

    beast::async_detect_ssl(
        stream_,
        buffer_,
        beast::bind_front_handler(
            &detect_session::on_detect,
            this->shared_from_this()));
  }

  void
  on_detect(beast::error_code ec, bool result) {
    if (ec)
      return fail(ec, "detect");

    if (result) {
      // Launch SSL session
      std::make_shared<ssl_http_session>(
          std::move(stream_),
          ctx_,
          std::move(buffer_),
          doc_root_)
          ->run();
      return;
    }

    // Launch plain session
    std::make_shared<plain_http_session>(
        std::move(stream_),
        std::move(buffer_),
        doc_root_)
        ->run();
  }
};

// Accepts incoming connections and launches the sessions
class listener : public std::enable_shared_from_this<listener> {
  net::io_context &ioc_;
  ssl::context &ctx_;
  tcp::acceptor acceptor_;
  std::shared_ptr<std::string const> doc_root_;

public:
  listener(
      net::io_context &ioc,
      ssl::context &ctx,
      tcp::endpoint endpoint,
      std::shared_ptr<std::string const> const &doc_root)
    : ioc_(ioc)
    , ctx_(ctx)
    , acceptor_(net::make_strand(ioc))
    , doc_root_(doc_root) {
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
    acceptor_.listen(
        net::socket_base::max_listen_connections, ec);
    if (ec) {
      fail(ec, "listen");
      return;
    }
  }

  // Start accepting incoming connections
  void
  run() {
    do_accept();
  }

private:
  void
  do_accept() {
    // The new connection gets its own strand
    acceptor_.async_accept(
        net::make_strand(ioc_),
        beast::bind_front_handler(
            &listener::on_accept,
            shared_from_this()));
  }

  void
  on_accept(beast::error_code ec, tcp_stream_strand socket) {
    if (ec) {
      fail(ec, "accept");
    } else {
      // Create the detector http_session and run it
      std::make_shared<detect_session>(
          beast_tcp_stream_strand(std::move(socket)),
          ctx_,
          doc_root_)
          ->run();
    }

    // Accept another connection
    do_accept();
  }
};

void init_http_server(boost::asio::io_context &ioc) {
  if (!nprpc::impl::g_cfg.listen_http_port) {
    std::cout << "HTTP server port is not set, skipping HTTP server initialization." << std::endl;
    return;
  }

  // Create and launch a listening port
  std::make_shared<listener>(
    ioc,
    g_cfg.ssl_context_server,
    tcp::endpoint{net::ip::make_address(g_cfg.listen_address), g_cfg.listen_http_port},
    std::make_shared<std::string const>(g_cfg.http_root_dir)
  )->run();
}

} // namespace nprpc::impl
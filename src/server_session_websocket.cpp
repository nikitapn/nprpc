// Copyright (c) 2021 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

#include <algorithm>
#include <cstdlib>
#include <cassert>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <future>
#include <vector>
#include <deque>
#include <stack>
#include <thread>
#include <optional>
#include <fstream>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/make_unique.hpp>
#include <boost/optional.hpp>
#include <openssl/sha.h>

#include <nprpc/nprpc_impl.hpp>
#include <nprpc/session.hpp>
#include <nprpc/nprpc_base.hpp>

namespace nprpc::impl {

namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
namespace beast = boost::beast;
namespace http = boost::beast::http;
namespace websocket = boost::beast::websocket;

using beast::flat_buffer;

using tcp = net::ip::tcp;
using error_code = boost::system::error_code;

class SessionImpl {
public:
	virtual void send(std::string&& data) = 0;
	virtual ~SessionImpl() = default;
};

template<typename WebSocket_Session>
class SessionImplT : public SessionImpl {
	std::weak_ptr<WebSocket_Session> ws_;
public:
	virtual void send(std::string&& data) {
		if (auto locked = ws_.lock()) {
			locked->write(std::move(data), std::move(locked));
		} else {
			// std::cerr << "websocket was closed...";
		}
	}

	SessionImplT(std::weak_ptr<WebSocket_Session> ws) : ws_(ws) {}
};

// Return a reasonable mime type based on the extension of a file.
beast::string_view mime_type(beast::string_view path)
{
	using beast::iequals;
	auto const ext = [&path]
	{
		auto const pos = path.rfind(".");
		if (pos == beast::string_view::npos)
			return beast::string_view{};
		return path.substr(pos);
	}();
	if (iequals(ext, ".htm"))  return "text/html";
	if (iequals(ext, ".html")) return "text/html";
	if (iequals(ext, ".php"))  return "text/html";
	if (iequals(ext, ".css"))  return "text/css";
	if (iequals(ext, ".txt"))  return "text/plain";
	if (iequals(ext, ".pdf"))  return "application/pdf";
	if (iequals(ext, ".js"))   return "application/javascript";
	if (iequals(ext, ".json")) return "application/json";
	if (iequals(ext, ".xml"))  return "application/xml";
	if (iequals(ext, ".swf"))  return "application/x-shockwave-flash";
	if (iequals(ext, ".wasm")) return "application/wasm";
	if (iequals(ext, ".flv"))  return "video/x-flv";
	if (iequals(ext, ".png"))  return "image/png";
	if (iequals(ext, ".jpe"))  return "image/jpeg";
	if (iequals(ext, ".jpeg")) return "image/jpeg";
	if (iequals(ext, ".jpg"))  return "image/jpeg";
	if (iequals(ext, ".gif"))  return "image/gif";
	if (iequals(ext, ".bmp"))  return "image/bmp";
	if (iequals(ext, ".ico"))  return "image/vnd.microsoft.icon";
	if (iequals(ext, ".tiff")) return "image/tiff";
	if (iequals(ext, ".tif"))  return "image/tiff";
	if (iequals(ext, ".svg"))  return "image/svg+xml";
	if (iequals(ext, ".svgz")) return "image/svg+xml";
	return "application/text";
}

// Append an HTTP rel-path to a local filesystem path.
// The returned path is normalized for the platform.
std::string
path_cat(
	beast::string_view base,
	beast::string_view path)
{
	if (base.empty())
		return std::string(path);
	std::string result(base);
#ifdef BOOST_MSVC
	char constexpr path_separator = '\\';
	if (result.back() == path_separator)
		result.resize(result.size() - 1);
	result.append(path.data(), path.size());
	for (auto& c : result)
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

// This function produces an HTTP response for the given
// request. The type of the response object depends on the
// contents of the request, so the interface requires the
// caller to pass a generic lambda for receiving the response.
template<
	class Body, class Allocator,
	class Send>
	void
	handle_request(
		beast::string_view doc_root,
		http::request<Body, http::basic_fields<Allocator>>&& req,
		Send&& send)
{
	// Returns a bad request response
	auto const bad_request =
		[&req](beast::string_view why)
	{
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
		[&req](beast::string_view target)
	{
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
		[&req](beast::string_view what)
	{
		http::response<http::string_body> res{http::status::internal_server_error, req.version()};
		res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
		res.set(http::field::content_type, "text/html");
		res.keep_alive(req.keep_alive());
		res.body() = "An error occurred: '" + std::string(what) + "'";
		res.prepare_payload();
		return res;
	};

	// send response in json
	auto const ajax_json_request = [&req](std::string val, std::string msg) {
		std::string json = "{ \"" + val + "\": \"" + msg + "\" }";
		auto size = json.size();
		http::response<http::string_body> res{
			std::piecewise_construct,
			std::make_tuple(std::move(json)),
			std::make_tuple(http::status::ok, req.version())
		};
		res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
		res.set(http::field::content_type, "application/json");
		res.content_length(size);
		res.keep_alive(req.keep_alive());
		return res;
	};

	// Make sure we can handle the method
	if (req.method() != http::verb::get
		&& req.method() != http::verb::post
		&& req.method() != http::verb::head)
		return send(bad_request("Unknown HTTP-method"));

	if (g_cfg.http_root_dir.empty())
		return send(bad_request("Illegal request: only Upgrade is allowed"));

	// Request path must be absolute and not contain "..".
	if (req.target().empty() ||
		req.target()[0] != '/' ||
		req.target().find("..") != beast::string_view::npos)
		return send(bad_request("Illegal request-target"));

	// std::cout << "target: " << req.target() << std::endl;

	std::string path = path_cat(doc_root, req.target());
	if (req.target().length() == 1 && req.target().back() == '/') path.append("index.html");

	// Attempt to open the file
	beast::error_code ec;
	http::file_body::value_type body;
	body.open(path.c_str(), beast::file_mode::scan, ec);

	// Handle the case where the file doesn't exist
	if (ec == beast::errc::no_such_file_or_directory)
		return send(not_found(req.target()));

	// Handle an unknown error
	if (ec)
		return send(server_error(ec.message()));

	// Cache the size since we need it after the move
	auto const size = body.size();

	// Respond to HEAD request
	if (req.method() == http::verb::head)
	{
		http::response<http::empty_body> res{http::status::ok, req.version()};
		res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
		res.set(http::field::content_type, mime_type(path));
		res.content_length(size);
		res.keep_alive(req.keep_alive());
		return send(std::move(res));
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
	return send(std::move(res));
}

//------------------------------------------------------------------------------

// Report a failure
void
beast_fail(beast::error_code ec, char const* what)
{
	// ssl::error::stream_truncated, also known as an SSL "short read",
	// indicates the peer closed the connection without performing the
	// required closing handshake (for example, Google does this to
	// improve performance). Generally this can be a security issue,
	// but if your communication protocol is self-terminated (as
	// it is with both HTTP and WebSocket) then you may simply
	// ignore the lack of close_notify.
	//
	// https://github.com/boostorg/beast/issues/38
	//
	// https://security.stackexchange.com/questions/91435/how-to-handle-a-malicious-ssl-tls-shutdown
	//
	// When a short read would cut off the end of an HTTP message,
	// Beast returns the error beast::http::error::partial_message.
	// Therefore, if we see a short read here, it has occurred
	// after the message has been completed, so it is safe to ignore it.

	if (ec == net::ssl::error::stream_truncated) return;

	std::cerr << what << ": " << ec.message() << "\n";
}

//------------------------------------------------------------------------------

template<class Derived>
class websocket_session : public nprpc::impl::Session
{
	struct work {
    virtual void operator()() = 0;
		virtual void on_failed(const boost::system::error_code& ec) noexcept = 0;
    virtual void on_executed(flat_buffer&& rx_buffer) noexcept = 0;
    virtual ~work() = default;
  };

	std::deque<std::shared_ptr<work>> requests_; // change to shared_ptr in plain sockets also. will do it tomorrow...
	std::deque<std::shared_ptr<work>> answers_; // change to unique_ptr maybe?

	bool reading_ = false;
	bool request_sended_waiting_for_answer_ = false;

	Derived& derived() { return static_cast<Derived&>(*this); }

	void dump(std::string_view fn) {
		// std::cerr << fn << "(): requests_.size = " << requests_.size() << ", answers_.size = " << answers_.size() << '\n';
	}
protected:
	void close() {
		const boost::system::error_code ec{boost::asio::error::connection_aborted};
		for (auto& r : requests_) {
			r->on_failed(ec);
		}
		requests_.clear();
		answers_.clear();
		nprpc::impl::Session::close();
	}
public:
	virtual void timeout_action() final {

	}

	virtual void send_receive(
		flat_buffer& buffer,
		uint32_t timeout_ms
	) {
		assert(*(uint32_t*)buffer.data().data() == buffer.size() - 4);

		if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryMessageContent) {
			dump_message(buffer, false);
		}

		struct work_impl : work {
			boost::beast::flat_buffer& buffer_;
			Derived& this_;
			uint32_t timeout_ms;

			std::promise<boost::system::error_code> promise;

			void operator()() noexcept override {
				//this_.set_timeout(timeout_ms);
				this_.ws().text(false); // binary mode only
				this_.dump("send_receive");
				this_.ws().async_write(
					buffer_.data(),
					std::bind(
						&websocket_session::on_write,
						this_.shared_from_this(),
						std::placeholders::_1,
						std::placeholders::_2,
						false));
			}

			void on_failed(const boost::system::error_code& ec) noexcept override {
				promise.set_value(ec);
			}

			void on_executed(boost::beast::flat_buffer&& buffer) noexcept override {
				buffer_ = std::move(buffer);
				promise.set_value({});
			}

			std::future<boost::system::error_code> get_future() { return promise.get_future(); }

			work_impl(Derived& _this_, boost::beast::flat_buffer& _buf, uint32_t _timeout_ms)
				: this_(_this_)
				, buffer_(_buf)
				, timeout_ms(_timeout_ms)
			{
			}
		};

	
		auto w = std::make_shared<work_impl>(derived(), buffer, timeout_ms);
		auto f = w->get_future();
		
		add_work(w);

		auto ec = f.get();

		if (!ec) {
			if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryMessageContent) {
				dump_message(buffer, true);
			}
		} else {
			throw nprpc::ExceptionCommFailure();
		}
	}

	virtual void send_receive_async(
		flat_buffer&& buffer,
		std::function<void(const boost::system::error_code&, flat_buffer&)>&& completion_handler,
		uint32_t timeout_ms
	) {
		assert(*(uint32_t*)buffer.data().data() == buffer.size() - 4);

		struct work_impl : work {
			boost::beast::flat_buffer buffer_;
			Derived& this_;
			uint32_t timeout_ms;
			std::function<void(const boost::system::error_code&, boost::beast::flat_buffer&)> handler;

			void operator()() noexcept override {
				//this_.set_timeout(timeout_ms);
				this_.ws().text(false); // binary mode only
				this_.dump("send_receive_async");
				this_.ws().async_write(
					buffer_.data(),
					std::bind(
						&websocket_session::on_write,
						this_.shared_from_this(),
						std::placeholders::_1,
						std::placeholders::_2,
						false));
			}

			void on_failed(const boost::system::error_code& ec) noexcept override {
				handler(ec, buffer_);
			}

			void on_executed(boost::beast::flat_buffer&& buffer) noexcept override {
				buffer_ = std::move(buffer);
				handler(boost::system::error_code{}, buffer_);
			}

			work_impl(boost::beast::flat_buffer&& _buf,
				Derived& _this_,
				std::function<void(const boost::system::error_code&, boost::beast::flat_buffer&)>&& _handler,
				uint32_t _timeout_ms)
				: buffer_(std::move(_buf))
				, this_(_this_)
				, timeout_ms(_timeout_ms)
				, handler(std::move(_handler))
			{
			}
		};

		add_work(std::make_shared<work_impl>(std::move(buffer), derived(), std::move(completion_handler), timeout_ms));
	}

	void add_work(std::shared_ptr<work> w) {
		boost::asio::post(derived().ws().get_executor(), [w, this]() mutable {
			dump("add_work");
			//std::cerr << "add_work() wq_.size = " << wq_.size() << '\n';
			requests_.push_back(std::move(w));
			if (requests_.size() == 1 && answers_.empty()) (*requests_.front())();
		});
	}
private:
	// Start the asynchronous operation
	template<class Body, class Allocator>
	void do_accept(http::request<Body, http::basic_fields<Allocator>> req) {
		// Set suggested timeout settings for the websocket
		derived().ws().set_option(
			websocket::stream_base::timeout::suggested(
				beast::role_type::server));

		// Set a decorator to change the Server of the handshake
		derived().ws().set_option(
			websocket::stream_base::decorator(
				[](websocket::response_type& res)
				{
					res.set(http::field::server,
						std::string(BOOST_BEAST_VERSION_STRING));
				}));

		// Accept the websocket handshake
		derived().ws().async_accept(
			req,
			beast::bind_front_handler(
				&websocket_session::on_accept,
				derived().shared_from_this()));
	}

	void on_accept(beast::error_code ec) {
		if (ec) {
			close();
			return beast_fail(ec, "accept");
		}
		do_read();
	}

	void do_read() {
		reading_ = true;
		derived().ws().async_read(
			rx_buffer_(),
			beast::bind_front_handler(
				&websocket_session::on_read,
				derived().shared_from_this()));
	}

	void on_read(beast::error_code ec, std::size_t bytes_transferred) {
		reading_ = false;
		boost::ignore_unused(bytes_transferred);

		// This indicates that the websocket_session was closed
		if (ec == websocket::error::closed) {
			close();
			return;
		}

		if (ec) {
			close();
			return beast_fail(ec, "read");
		}

		nprpc::impl::flat::Header_Direct header(rx_buffer_(), 0);

		if (header.msg_type() == nprpc::impl::MessageType::Request) {
			dump("on_read_req");
			//std::cerr << "on_read_request: " << bytes_transferred << ", wq_.size = " << wq_.size() << '\n';
			handle_request();

			struct work_send_reply : work {
				Derived& self_;
				flat_buffer buffer_;

				work_send_reply(Derived& self, flat_buffer&& buffer)
					: self_(self)
					, buffer_(std::move(buffer))
				{
				}

				virtual void operator()() final {
					self_.ws().text(false); // binary mode only
					self_.ws().async_write(
						buffer_.data(),
						std::bind(
							&websocket_session::on_write,
							self_.shared_from_this(),
							std::placeholders::_1,
							std::placeholders::_2,
							true));
				}
				virtual void on_failed(const boost::system::error_code& ec) noexcept {}
				virtual void on_executed(boost::beast::flat_buffer&&) noexcept {}
			};

			answers_.emplace_front(std::make_shared<work_send_reply>(derived(), std::move(rx_buffer_(true))));
			(*answers_.front())();
		} else { // received an answer
			request_sended_waiting_for_answer_ = false;
			dump("on_read_asw");
			assert(!requests_.empty());
			//std::cerr << "on_read_answer: " << bytes_transferred << ", wq_.size = " << wq_.size() << '\n';
			if (!ec) {
				requests_.front()->on_executed(rx_buffer_(true));
			} else {
				requests_.front()->on_failed(ec);
			}
			requests_.pop_front();
			if (requests_.size()) (*requests_.front())();
			else do_read();
		}
	}

	void on_write(beast::error_code ec, std::size_t bytes_transferred, bool is_answer) {
		//std::cerr << "on_write: " << bytes_transferred << ", wq_.size = " << wq_.size() << '\n';

		if (ec == websocket::error::closed) {
			close();
			return;
		}

		boost::ignore_unused(bytes_transferred);

		if (ec) {
			close();
			return beast_fail(ec, "write");
		}

		if (is_answer) { // answer sended 
			dump("on_write_answer_sended");
			assert(answers_.size() >= 1);
			answers_.pop_front();
			if (answers_.empty() == false) (*answers_.front())();
			else if (requests_.empty() == false && request_sended_waiting_for_answer_ == false) (*requests_.front())();
			else do_read();
		} else {
			dump("on_write_request_sended");
			request_sended_waiting_for_answer_ = true;
			assert(requests_.size() >= 1);
			//std::cerr << "request sent: " << bytes_transferred << '\n';
			if (reading_ == false) do_read(); // read an answer to the request
		}
	}
public:
	// Start the asynchronous operation
	template<class Body, class Allocator>
	void run(http::request<Body, http::basic_fields<Allocator>> req) {
		// std::cout << "upgrade: " << req.target() << std::endl;
		/*
		derived().ws().set_option(
						websocket::stream_base::decorator(
								[](http::response_header<> &hdr) {
										hdr.set(
												http::field::sec_websocket_protocol,
												"binary");
								}));
		*/
		// Accept the WebSocket upgrade request
		g_orb->add_connection(derived().shared_from_this());
		do_accept(std::move(req));
	}

	websocket_session(net::any_io_executor executor)
		: Session(executor)
	{
	}

	~websocket_session()
	{
		std::cerr << "~websocket_session()\n";
	}
};

//------------------------------------------------------------------------------

// Handles a plain WebSocket connection
class plain_websocket_session
	: public websocket_session<plain_websocket_session>
	, public std::enable_shared_from_this<plain_websocket_session>
{
	using base = websocket_session<plain_websocket_session>;

	websocket::stream<beast::tcp_stream> ws_;
public:
	// Create the session
	explicit plain_websocket_session(beast::tcp_stream&& stream)
		: websocket_session<plain_websocket_session>(stream.get_executor())
		, ws_(std::move(stream)) {
		auto endpoint = ws().next_layer().socket().remote_endpoint();
		remote_endpoint_.ip4 = endpoint.address().to_v4().to_uint();
		remote_endpoint_.port = endpoint.port();

		//std::cerr << "ip4: " << remote_endpoint_.ip4 << "port: " << remote_endpoint_.port << '\n';
	}

	// Called by the base class
	websocket::stream<beast::tcp_stream>& ws() { return ws_; }
};

//------------------------------------------------------------------------------

// Handles an SSL WebSocket connection
class ssl_websocket_session
	: public websocket_session<ssl_websocket_session>
	, public std::enable_shared_from_this<ssl_websocket_session> {
	using base = websocket_session<ssl_websocket_session>;

	websocket::stream<beast::ssl_stream<beast::tcp_stream>> ws_;
public:
	// Create the ssl_websocket_session
	explicit ssl_websocket_session(beast::ssl_stream<beast::tcp_stream>&& stream)
		: websocket_session<ssl_websocket_session>(stream.get_executor())
		, ws_(std::move(stream)) {
		auto endpoint = ws().next_layer().next_layer().socket().remote_endpoint();
		remote_endpoint_.ip4 = endpoint.address().to_v4().to_uint();
		remote_endpoint_.port = endpoint.port();
	}

	// Called by the base class
	websocket::stream<beast::ssl_stream<beast::tcp_stream>>& ws() { return ws_; }
};

//------------------------------------------------------------------------------

template<class Body, class Allocator>
void
make_websocket_session(
	beast::tcp_stream stream,
	http::request<Body, http::basic_fields<Allocator>> req)
{
	std::make_shared<plain_websocket_session>(
		std::move(stream)
		)->run(std::move(req));
}

template<class Body, class Allocator>
void
make_websocket_session(
	beast::ssl_stream<beast::tcp_stream> stream,
	http::request<Body, http::basic_fields<Allocator>> req)
{
	std::make_shared<ssl_websocket_session>(
		std::move(stream)
		)->run(std::move(req));
}

//------------------------------------------------------------------------------

// Handles an HTTP server connection.
// This uses the Curiously Recurring Template Pattern so that
// the same code works with both SSL streams and regular sockets.
template<class Derived>
class http_session
{
	Derived& derived() { return static_cast<Derived&>(*this); }

	// This queue is used for HTTP pipelining.
	class queue
	{
		enum
		{
			// Maximum number of responses we will queue
			limit = 8
		};

		// The type-erased, saved work item
		struct work
		{
			virtual ~work() = default;
			virtual void operator()() = 0;
		};

		http_session& self_;
		std::vector<std::unique_ptr<work>> items_;

	public:
		explicit queue(http_session& self)
			: self_(self) {
			static_assert(limit > 0, "queue limit must be positive");
			items_.reserve(limit);
		}

		// Returns `true` if we have reached the queue limit
		bool is_full() const {
			return items_.size() >= limit;
		}

		// Called when a message finishes sending
		// Returns `true` if the caller should initiate a read
		bool on_write() {
			BOOST_ASSERT(!items_.empty());
			auto const was_full = is_full();
			items_.erase(items_.begin());
			if (!items_.empty()) (*items_.front())();
			return was_full;
		}

		// Called by the HTTP handler to send a response.
		template<bool isRequest, class Body, class Fields>
		void operator()(http::message<isRequest, Body, Fields>&& msg) {
			// This holds a work item
			struct work_impl : work {
				http_session& self_;
				http::message<isRequest, Body, Fields> msg_;

				work_impl(
					http_session& self,
					http::message<isRequest, Body, Fields>&& msg)
					: self_(self)
					, msg_(std::move(msg))
				{
				}

				void operator()() {
					http::async_write(
						self_.derived().stream(),
						msg_,
						beast::bind_front_handler(
							&http_session::on_write,
							self_.derived().shared_from_this(),
							msg_.need_eof()));
				}
			};

			// Allocate and store the work
			items_.push_back(
				boost::make_unique<work_impl>(self_, std::move(msg))
			);

			// If there was no previous work, start this one
			if (items_.size() == 1) (*items_.front())();
		}
	};

	std::shared_ptr<std::string const> doc_root_;
	queue queue_;
	// The parser is stored in an optional container so we can
	// construct it from scratch it at the beginning of each new message.
	boost::optional<http::request_parser<http::string_body>> parser_;
protected:
	flat_buffer buffer_;
public:
	// Construct the session
	http_session(
		flat_buffer buffer,
		std::shared_ptr<std::string const> const& doc_root
	)
		: doc_root_(doc_root)
		, queue_(*this)
		, buffer_(std::move(buffer))
	{
	}

	void do_read()
	{
		// Construct a new parser for each message
		parser_.emplace();

		// Apply a reasonable limit to the allowed size
		// of the body in bytes to prevent abuse.
		parser_->body_limit(10000);

		// Set the timeout.
		beast::get_lowest_layer(
			derived().stream()
		).expires_after(std::chrono::seconds(30));

		// Read a request using the parser-oriented interface
		http::async_read(
			derived().stream(),
			buffer_,
			*parser_,
			beast::bind_front_handler(
				&http_session::on_read,
				derived().shared_from_this()));
	}

	void on_read(beast::error_code ec, std::size_t bytes_transferred) {
		boost::ignore_unused(bytes_transferred);

		// This means they closed the connection
		if (ec == http::error::end_of_stream)
			return derived().do_eof();

		if (ec) return beast_fail(ec, "read");

		// See if it is a WebSocket Upgrade
		if (websocket::is_upgrade(parser_->get()))
		{
			// Disable the timeout.
			// The websocket::stream uses its own timeout settings.
			beast::get_lowest_layer(derived().stream()).expires_never();

			// Create a websocket session, transferring ownership
			// of both the socket and the HTTP request.
			return make_websocket_session(
				derived().release_stream(),
				parser_->release());
		}

		// Send the response
		handle_request(*doc_root_, parser_->release(), queue_);

		// If we aren't at the queue limit, try to pipeline another request
		if (!queue_.is_full()) do_read();
	}

	void on_write(bool close, beast::error_code ec, std::size_t bytes_transferred) {
		boost::ignore_unused(bytes_transferred);

		if (ec) return beast_fail(ec, "write");

		if (close) {
			// This means we should close the connection, usually because
			// the response indicated the "Connection: close" semantic.
			return derived().do_eof();
		}

		// Inform the queue that a write completed
		if (queue_.on_write()) {
			// Read another request
			do_read();
		}
	}
};

//------------------------------------------------------------------------------

// Handles a plain HTTP connection
class plain_http_session
	: public http_session<plain_http_session>
	, public std::enable_shared_from_this<plain_http_session>
{
	beast::tcp_stream stream_;

public:
	// Create the session
	plain_http_session(
		beast::tcp_stream&& stream,
		flat_buffer&& buffer,
		std::shared_ptr<std::string const> const& doc_root)
		: http_session<plain_http_session>(std::move(buffer), doc_root)
		, stream_(std::move(stream))
	{
	}

	// Start the session
	void run() {
		this->do_read();
	}

	// Called by the base class
	beast::tcp_stream& stream() {
		return stream_;
	}

	// Called by the base class
	beast::tcp_stream release_stream() {
		return std::move(stream_);
	}

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

// Handles an SSL HTTP connection
class ssl_http_session
	: public http_session<ssl_http_session>
	, public std::enable_shared_from_this<ssl_http_session>
{
	beast::ssl_stream<beast::tcp_stream> stream_;
public:
	// Create the http_session
	ssl_http_session(
		beast::tcp_stream&& stream,
		ssl::context& ctx,
		flat_buffer&& buffer,
		std::shared_ptr<std::string const> const& doc_root)
		: http_session<ssl_http_session>(std::move(buffer), doc_root)
		, stream_(std::move(stream), ctx)
	{
	}

	// Start the session
	void
		run()
	{
		// Set the timeout.
		beast::get_lowest_layer(stream_).expires_after(std::chrono::seconds(30));

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
	beast::ssl_stream<beast::tcp_stream>&
		stream()
	{
		return stream_;
	}

	// Called by the base class
	beast::ssl_stream<beast::tcp_stream>
		release_stream()
	{
		return std::move(stream_);
	}

	// Called by the base class
	void
		do_eof()
	{
		// Set the timeout.
		beast::get_lowest_layer(stream_).expires_after(std::chrono::seconds(30));

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
			std::size_t bytes_used)
	{
		if (ec)
			return beast_fail(ec, "handshake");

		// Consume the portion of the buffer used by the handshake
		buffer_.consume(bytes_used);

		do_read();
	}

	void
		on_shutdown(beast::error_code ec)
	{
		if (ec)
			return beast_fail(ec, "shutdown");

		// At this point the connection is closed gracefully
	}
};

//------------------------------------------------------------------------------

// Detects SSL handshakes
class detect_session : public std::enable_shared_from_this<detect_session>
{
	beast::tcp_stream stream_;
	ssl::context& ctx_;
	std::shared_ptr<std::string const> doc_root_;
	flat_buffer buffer_;
public:
	explicit
		detect_session(
			tcp::socket&& socket,
			ssl::context& ctx,
			std::shared_ptr<std::string const> const& doc_root)
		: stream_(std::move(socket))
		, ctx_(ctx)
		, doc_root_(doc_root)
	{
	}

	// Launch the detector
	void
		run()
	{
		// Set the timeout.
		stream_.expires_after(std::chrono::seconds(30));

		beast::async_detect_ssl(
			stream_,
			buffer_,
			beast::bind_front_handler(
				&detect_session::on_detect,
				this->shared_from_this()));
	}

	void
		on_detect(beast::error_code ec, bool result)
	{
		if (ec)
			return beast_fail(ec, "detect");

		if (result)
		{
			// Launch SSL session
			std::make_shared<ssl_http_session>(
				std::move(stream_),
				ctx_,
				std::move(buffer_),
				doc_root_)->run();
			return;
		}

		// Launch plain session
		std::make_shared<plain_http_session>(
			std::move(stream_),
			std::move(buffer_),
			doc_root_)->run();
	}
};

// Accepts incoming connections and launches the sessions
class listener : public std::enable_shared_from_this<listener> {
	net::io_context& ioc_;
	ssl::context& ctx_;
	tcp::acceptor acceptor_;
	std::shared_ptr<std::string const> doc_root_;
public:
	listener(
		net::io_context& ioc,
		ssl::context& ctx,
		tcp::endpoint endpoint,
		std::shared_ptr<std::string const> const& doc_root)
		: ioc_(ioc)
		, ctx_(ctx)
		, acceptor_(net::make_strand(ioc))
		, doc_root_(doc_root)
	{
		beast::error_code ec;

		// Open the acceptor
		acceptor_.open(endpoint.protocol(), ec);
		if (ec)
		{
			beast_fail(ec, "open");
			return;
		}

		// Allow address reuse
		acceptor_.set_option(net::socket_base::reuse_address(true), ec);
		if (ec)
		{
			beast_fail(ec, "set_option");
			return;
		}

		// Bind to the server address
		acceptor_.bind(endpoint, ec);
		if (ec)
		{
			beast_fail(ec, "bind");
			return;
		}

		// Start listening for connections
		acceptor_.listen(
			net::socket_base::max_listen_connections, ec);
		if (ec)
		{
			beast_fail(ec, "listen");
			return;
		}
	}

	// Start accepting incoming connections
	void
		run()
	{
		do_accept();
	}

private:
	void do_accept() {
		// The new connection gets its own strand
		acceptor_.async_accept(
			net::make_strand(ioc_),
			beast::bind_front_handler(
				&listener::on_accept,
				shared_from_this()));
	}

	void on_accept(beast::error_code ec, tcp::socket socket) {
		if (ec) {
			beast_fail(ec, "accept");
		} else {
			// Create the detector http_session and run it
			std::make_shared<detect_session>(
				std::move(socket),
				ctx_,
				doc_root_)->run();
		}
		// Accept another connection
		do_accept();
	}
};

// The SSL context is required, and holds certificates
ssl::context ctx{ssl::context::tlsv12};


void init_web_socket(boost::asio::io_context& ioc) {
	if (!nprpc::impl::g_cfg.websocket_port) return;

	if (nprpc::impl::g_cfg.use_ssl) {
		auto read_file_to_string = [](std::string const file) {
			std::ifstream is(file);
			if (is.bad()) { throw std::runtime_error("could not open certificate file: \"" + file + "\""); }
			return std::string(std::istreambuf_iterator<char>(is),
				std::istreambuf_iterator<char>());
		};

		std::string const cert = read_file_to_string(nprpc::impl::g_cfg.ssl_public_key);
		std::string const key = read_file_to_string(nprpc::impl::g_cfg.ssl_secret_key);

		ctx.set_password_callback(
			[](std::size_t,
				boost::asio::ssl::context_base::password_purpose)
			{
				return "test";
			});

		ctx.set_options(
			boost::asio::ssl::context::default_workarounds |
			boost::asio::ssl::context::no_sslv2 |
			boost::asio::ssl::context::single_dh_use);

		ctx.use_certificate_chain(
			boost::asio::buffer(cert.data(), cert.size()));

		ctx.use_private_key(
			boost::asio::buffer(key.data(), key.size()),
			boost::asio::ssl::context::file_format::pem);

		//ctx.use_tmp_dh(
		//	boost::asio::buffer(dh.data(), dh.size()));
	}

	// Create and launch a listening port
	std::make_shared<listener>(ioc, ctx,
		tcp::endpoint{net::ip::make_address("0.0.0.0"), nprpc::impl::g_cfg.websocket_port },
		std::make_shared<std::string const>(nprpc::impl::g_cfg.http_root_dir))->run();
}

} // namespace nprpc::impl
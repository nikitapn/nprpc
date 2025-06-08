// Copyright (c) 2021-2025 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by
// LICENSING file in the topmost directory

#include <nprpc/impl/websocket_session.hpp>
#include <nprpc/impl/nprpc_impl.hpp>
#include <future>

namespace nprpc::impl {

void fail(beast::error_code ec, char const *what) {
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

  if (ec == net::ssl::error::stream_truncated || ec == beast::error::timeout)
    return;

  // Do not report these
  if (ec == beast::error::timeout)
    return;

  std::cerr << what << ": " << ec.message() << '\n';
}

template <class Derived>
void WebSocketSession<Derived>::do_read() {
  if (closed_.load()) return;
  
  bool expected = false;
  if (!reading_.compare_exchange_strong(expected, true)) {
    return; // Already reading
  }

  std::cout << "WebSocketSession::do_read() - starting async read" << std::endl;
  
  derived().ws().async_read(
      rx_buffer_(),
      beast::bind_front_handler(
          &WebSocketSession::on_read,
          derived().shared_from_this()));
}

template <class Derived>
void WebSocketSession<Derived>::on_read(beast::error_code ec, std::size_t bytes_transferred) {
  boost::ignore_unused(bytes_transferred);
  reading_.store(false);

  if (ec == websocket::error::closed) {
    close();
    return;
  }

  if (ec) {
    close();
    return fail(ec, "read");
  }

  nprpc::impl::flat::Header_Direct header(rx_buffer_(), 0);
  const uint32_t request_id = header.reserved();

  if (header.msg_type() == nprpc::impl::MessageType::Request) {

    // std::cout << "WebSocketSession::on_read() - received request - id=" << request_id << std::endl;

    // Handle incoming request
    handle_request();

    // Queue response for sending
    std::function<void(const boost::system::error_code&)> completion_handler = 
      [](const boost::system::error_code&) {};
 
    // Inject request ID into the response header: rx_buffer_() was flipped in handle_request()
    inject_request_id(rx_buffer_(), request_id);
    // write_queue_.emplace_back(std::move(buffer), std::move(completion_handler));
    write_queue_.emplace_back(rx_buffer_(true), std::move(completion_handler));
    std::cout << "WebSocketSession::on_read() - queuing response - pending_requests size: " 
               << pending_requests_.size() << ", id=" << request_id << std::endl;
    do_write();
  } else { // received an answer
    // Extract request ID from header
    // uint32_t request_id = extract_request_id(rx_buffer_());

    std::cout << "WebSocketSession::on_read() - received response - pending_requests size: " 
               << pending_requests_.size() << ", id=" << request_id << std::endl;
 
    auto it = pending_requests_.find(request_id);
    if (it != pending_requests_.end()) {
      auto response = rx_buffer_(true);
      it->second.completion_handler(boost::system::error_code{}, response);
      pending_requests_.erase(it);
    }
  }

  rx_buffer_().clear();
  rx_buffer_.flip().clear();
  
  // Continue reading immediately - this is the key difference!
  do_read();
}

template <class Derived>
void WebSocketSession<Derived>::do_write() {
  if (closed_.load()) return;
  
  bool expected = false;
  if (!writing_.compare_exchange_strong(expected, true)) {
    return; // Already writing
  }
  
  if (write_queue_.empty()) {
    writing_.store(false);
    return;
  }

  auto& msg = write_queue_.front();
  auto header = nprpc::impl::flat::Header_Direct(msg.buffer, 0);
  std::cout << "WebSocketSession::do_write() - writing message to WebSocket - id=" << header.reserved() << std::endl;

  derived().ws().text(false); // binary mode
  derived().ws().async_write(
      msg.buffer.data(),
      beast::bind_front_handler(
          &WebSocketSession::on_write,
          derived().shared_from_this()));
}

template <class Derived>
void WebSocketSession<Derived>::on_write(beast::error_code ec, std::size_t bytes_transferred) {
  boost::ignore_unused(bytes_transferred);
  
  if (ec == websocket::error::closed) {
    close();
    return;
  }

  if (ec) {
    close();
    return fail(ec, "write");
  }

  // Call completion handler for the sent message
  if (!write_queue_.empty()) {
    auto& msg = write_queue_.front();
    if (msg.completion_handler) {
      msg.completion_handler(ec);
    }
    write_queue_.pop_front();
  }
  
  writing_.store(false);
  
  std::cout << "WebSocketSession::on_write() - write completed, write_queue_ size: " 
            << write_queue_.size() << std::endl;

  // Continue writing if there are more messages
  if (!write_queue_.empty()) {
    do_write();
  }
}

template <class Derived>
void WebSocketSession<Derived>::close() {
  closed_.store(true);
  
  const boost::system::error_code ec{boost::asio::error::connection_aborted};
  
  // Fail all pending requests
  for (auto& [id, req] : pending_requests_) {
    flat_buffer empty_response{};
    req.completion_handler(ec, empty_response);
  }
  pending_requests_.clear();
  
  // Fail all pending writes
  for (auto& msg : write_queue_) {
    if (msg.completion_handler) {
      msg.completion_handler(ec);
    }
  }
  write_queue_.clear();
  
  Session::close();
}

template <class Derived>
void WebSocketSession<Derived>::timeout_action() {
  // Handle timeouts for pending requests
  auto now = std::chrono::steady_clock::now();
  auto it = pending_requests_.begin();
  
  while (it != pending_requests_.end()) {
    if (now > it->second.timeout_point) {
      flat_buffer empty_response{};
      it->second.completion_handler(boost::asio::error::timed_out, empty_response);
      it = pending_requests_.erase(it);
    } else {
      ++it;
    }
  }
}

template <class Derived>
void WebSocketSession<Derived>::send_receive(flat_buffer &buffer, uint32_t timeout_ms) {
  assert(*(uint32_t *)buffer.data().data() == buffer.size() - 4);

  std::cout << "WebSocketSession::send_receive() - sending message - size: " 
            << buffer.size() << ", timeout: " << timeout_ms << " ms" << std::endl;

  if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryMessageContent) {
    dump_message(buffer, false);
  }

  std::promise<std::pair<boost::system::error_code, flat_buffer>> promise;
  auto future = promise.get_future();
  
  send_receive_async(
    flat_buffer{buffer}, 
    [&promise](const boost::system::error_code& ec, flat_buffer& response) {
      promise.set_value({ec, std::move(response)});
    },
    timeout_ms
  );

  auto [ec, response] = future.get();
  
  if (!ec) {
    buffer = std::move(response);
    if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryMessageContent) {
      dump_message(buffer, true);
    }
  } else {
    throw nprpc::ExceptionCommFailure();
  }
}

// Called outside of strand context, so we need to ensure thread safety
template <class Derived>
void WebSocketSession<Derived>::send_receive_async(
    flat_buffer &&buffer,
    std::optional<std::function<void(const boost::system::error_code &, flat_buffer &)>> &&completion_handler,
    uint32_t timeout_ms) {
  
  assert(*(uint32_t *)buffer.data().data() == buffer.size() - 4);

  uint32_t request_id = generate_request_id();
  
  // Inject request ID into message header
  inject_request_id(buffer, request_id);

  // Queue the request for sending
  boost::asio::post(derived().ws().get_executor(), [this, buffer = std::move(buffer), request_id,
    completion_handler = std::move(completion_handler), timeout_ms]() mutable {
    // Store the pending request
    if (completion_handler) {
      pending_requests_.emplace(request_id, pending_request{
        std::move(*completion_handler),
        std::chrono::milliseconds(timeout_ms)
      });
    }
    std::function<void(const boost::system::error_code&)> write_completion = 
      [this, request_id](const boost::system::error_code& ec) {
        if (ec) {
          // Writing failed, remove pending request and notify
          auto it = pending_requests_.find(request_id);
          if (it != pending_requests_.end()) {
            flat_buffer empty_response{};
            it->second.completion_handler(ec, empty_response);
            pending_requests_.erase(it);
          }
        }
        // If write succeeded, we wait for the response in on_read
      };
    std::cout << "WebSocketSession::send_receive_async() - queuing message for write - id=" << request_id << std::endl;
    write_queue_.emplace_back(std::move(buffer), std::move(write_completion));
    do_write();
  });
}

template <class Derived>
void WebSocketSession<Derived>::start_read_loop() {
  do_read();
}

template <class Derived>
void WebSocketSession<Derived>::start_write_loop() {
  // Write loop is driven by incoming requests, no need for separate start
}

template <class Derived>
void WebSocketSession<Derived>::inject_request_id(flat_buffer& buffer, uint32_t request_id) {
  if (buffer.size() >= sizeof(impl::Header)) {
    impl::flat::Header_Direct header(buffer, 0);
    header.reserved() = request_id;
  }
}

template <class Derived>
uint32_t WebSocketSession<Derived>::extract_request_id(const flat_buffer& buffer) {
  if (buffer.size() >= sizeof(impl::Header)) {
    const impl::flat::Header_Direct header(const_cast<flat_buffer&>(buffer), 0);
    return header.reserved();
  }
  return 0;
}

// Explicit instantiation for the WebSocketSession class template
template class WebSocketSession<AcceptingPlainWebSocketSession>;
template class WebSocketSession<AcceptingSSLWebSocketSession>;
template class WebSocketSession<ClientPlainWebSocketSession>;
template class WebSocketSession<ClientSSLWebSocketSession>;

} // namespace nprpc::impl

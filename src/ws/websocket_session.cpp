// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include <future>
#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/impl/websocket_session.hpp>
#include "../logging.hpp"

namespace nprpc::impl {

void fail(beast::error_code ec, char const* what)
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

  if (ec == net::ssl::error::stream_truncated || ec == beast::error::timeout)
    return;

  // Do not report these
  if (ec == beast::error::timeout)
    return;

  std::cerr << "[nprpc] WebSocketSession: " << what << ": " << ec.message()
            << '\n';
}

template <class Derived> void WebSocketSession<Derived>::do_read()
{
  if (closed_.load())
    return;

  bool expected = false;
  if (!reading_.compare_exchange_strong(expected, true)) {
    return; // Already reading
  }

  rx_buffer_.consume(rx_buffer_.size()); // Clear the buffer before reading

  derived().ws().async_read(
      rx_buffer_, beast::bind_front_handler(&WebSocketSession::on_read,
                                            derived().shared_from_this()));
}

template <class Derived>
void WebSocketSession<Derived>::on_read(
    beast::error_code ec, [[maybe_unused]] std::size_t bytes_transferred)
{
  reading_.store(false);

  if (ec == websocket::error::closed) {
    close();
    return;
  }

  if (ec) {
    close();
    fail(ec, "read");
    return;
  }

  // Additional safety check: verify message size
  if (rx_buffer_.size() > max_message_size) {
    NPRPC_LOG_WARN("WebSocketSession::on_read: Rejected oversized WebSocket message: {} bytes (max: {} bytes)",
      rx_buffer_.size(), max_message_size);
    close();
    return;
  }

  nprpc::impl::flat::Header_Direct header(rx_buffer_, 0);
  const uint32_t request_id = header.request_id();

  NPRPC_LOG_DEBUG("WebSocketSession::on_read: size={}, msg_id={}, msg_type={}, request_id={}",
    rx_buffer_.size(), static_cast<uint32_t>(header.msg_id()), static_cast<uint32_t>(header.msg_type()), request_id);

  if (header.msg_type() == nprpc::impl::MessageType::Request) {
    // Handle incoming request
    bool needs_reply = handle_request(rx_buffer_, tx_buffer_);

    if (needs_reply) {
      // Queue response for sending
      std::function<void(const boost::system::error_code&)> completion_handler =
          [](const boost::system::error_code&) {};

      // Inject request ID into the response header
      inject_request_id(tx_buffer_, request_id);
      write_queue_.emplace_back(std::move(tx_buffer_),
                                std::move(completion_handler));
      do_write();
    }
  } else { // received an answer
    auto it = pending_requests_.find(request_id);
    if (it != pending_requests_.end()) {
      // FIXME: Maybe I don't need to move here? I forgot the reason
      auto response = std::move(rx_buffer_);
      it->second.completion_handler(boost::system::error_code{}, response);
      pending_requests_.erase(it);
    } else {
      // Received response for unknown request - possible attack or bug
      std::cerr << "[nprcp] WebSocketSession: Received response for unknown "
                   "request ID: "
                << request_id << '\n';
    }
  }

  // Continue reading immediately - this is the key difference!
  do_read();
}

template <class Derived> void WebSocketSession<Derived>::do_write()
{
  if (closed_.load())
    return;

  bool expected = false;
  if (!writing_.compare_exchange_strong(expected, true)) {
    return; // Already writing
  }

  if (write_queue_.empty()) {
    writing_.store(false);
    return;
  }

  // NOTE: commented out code for write queue size check
  // It was causing issues with legitimate high-throughput scenarios in proxy
  // server setups Check write queue size - if too large, client is too slow
  // if (write_queue_.size() > max_write_queue_size) {
  //   std::cerr << "[nprpc] WebSocketSession: Write queue exceeded maximum
  //   size
  //   (" << max_write_queue_size
  //             << "), closing slow connection\n";
  //   writing_.store(false);
  //   close();
  //   return;
  // }

  auto& msg = write_queue_.front();
  derived().ws().text(false); // binary mode
  derived().ws().async_write(
      msg.buffer.data(),
      beast::bind_front_handler(&WebSocketSession::on_write,
                                derived().shared_from_this()));
}

template <class Derived>
void WebSocketSession<Derived>::on_write(beast::error_code ec,
                                         std::size_t bytes_transferred)
{
  boost::ignore_unused(bytes_transferred);

  if (ec == websocket::error::closed) {
    close();
    return;
  }

  if (ec) {
    close();
    fail(ec, "write");
    return;
  }

  // Call completion handler for the sent message
  if (!write_queue_.empty()) {
    auto& msg = write_queue_.front();
    if (msg.completion_handler)
      msg.completion_handler(ec);

    write_queue_.pop_front();
  }

  writing_.store(false);

  // Continue writing if there are more messages
  if (!write_queue_.empty()) {
    do_write();
  }
}

template <class Derived> void WebSocketSession<Derived>::close()
{
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
    if (msg.completion_handler)
      msg.completion_handler(ec);
  }
  write_queue_.clear();

  Session::close();
}

template <class Derived> void WebSocketSession<Derived>::timeout_action()
{
  // Handle timeouts for pending requests
  auto now = std::chrono::steady_clock::now();
  auto it = pending_requests_.begin();

  while (it != pending_requests_.end()) {
    if (now > it->second.timeout_point) {
      flat_buffer empty_response{};
      it->second.completion_handler(boost::asio::error::timed_out,
                                    empty_response);
      std::cout << "[nprpc] WebSocketSession: Timing out request ID: "
                << it->first << '\n';
      it = pending_requests_.erase(it);
    } else {
      ++it;
    }
  }
}

template <class Derived>
void WebSocketSession<Derived>::send_receive(flat_buffer& buffer,
                                             uint32_t timeout_ms)
{
  assert(*(uint32_t*)buffer.data().data() == buffer.size() - 4);

  // dump_message(buffer, false);

  std::promise<std::pair<boost::system::error_code, flat_buffer>> promise;
  auto future = promise.get_future();

  send_receive_async(
      std::move(buffer),
      [&promise](const boost::system::error_code& ec, flat_buffer& response) {
        promise.set_value({ec, std::move(response)});
      },
      timeout_ms);

  auto [ec, response] = future.get();

  if (!ec) {
    buffer = std::move(response);
    // dump_message(buffer, true);
  } else {
    throw nprpc::ExceptionCommFailure();
  }
}

// Called outside of strand context, so we need to ensure thread safety
template <class Derived>
void WebSocketSession<Derived>::send_receive_async(
    flat_buffer&& buffer,
    std::optional<std::function<void(const boost::system::error_code&,
                                     flat_buffer&)>>&& completion_handler,
    uint32_t timeout_ms)
{
  assert(*(uint32_t*)buffer.data().data() == buffer.size() - 4);

  uint32_t request_id = generate_request_id();

  // Inject request ID into message header
  inject_request_id(buffer, request_id);

  // nprpc::impl::flat::Header_Direct header(buffer, 0);
  // std::cout << "[nprpc] WebSocketSession: send_receive_async called. size: "
  //           << buffer.size()
  //           << ", msg_id: " << static_cast<uint32_t>(header.msg_id())
  //           << ", request_id: " << header.request_id() << std::endl;

  // Queue the request for sending
  boost::asio::post(
      derived().ws().get_executor(),
      [this, buffer = std::move(buffer), request_id,
       completion_handler = std::move(completion_handler),
       timeout_ms]() mutable {
        // Store the pending request
        // This is not really necessary if completion_handler is not
        // provided, but we do it anyway to keep track of all pending
        // requests.
        pending_requests_.emplace(
            request_id, pending_request{completion_handler
                                            ? std::move(*completion_handler)
                                            : pending_request::empty_handler(),
                                        std::chrono::milliseconds(timeout_ms)});

        std::function<void(const boost::system::error_code&)> write_completion =
            [this, request_id](const boost::system::error_code& ec) {
              if (ec) {
                // Writing failed, remove pending request and notify
                auto it = pending_requests_.find(request_id);
                if (it != pending_requests_.end()) {
                  std::cerr << "[nprpc] WebSocketSession: Write "
                               "failed for request ID: "
                            << request_id << ", error: " << ec.message()
                            << '\n';
                  flat_buffer empty_response{};
                  it->second.completion_handler(ec, empty_response);
                  pending_requests_.erase(it);
                }
              }
              // If write succeeded, we wait for the response in on_read
            };
        write_queue_.emplace_back(std::move(buffer),
                                  std::move(write_completion));
        do_write();
      });
}

template <class Derived>
void WebSocketSession<Derived>::send_stream_message(flat_buffer&& buffer)
{
  // Fire-and-forget message for streaming - no response expected
  boost::asio::post(
      derived().ws().get_executor(),
      [this, buffer = std::move(buffer)]() mutable {
        NPRPC_LOG_INFO("WebSocketSession::send_stream_message posted: buffer.size()={}", buffer.size());
        // Queue with no completion handler (fire and forget)
        write_queue_.emplace_back(std::move(buffer), nullptr);
        do_write();
      });
}

template <class Derived> void WebSocketSession<Derived>::start_read_loop()
{
  do_read();
}

template <class Derived> void WebSocketSession<Derived>::start_write_loop()
{
  // Write loop is driven by incoming requests, no need for separate start
}

template <class Derived>
void WebSocketSession<Derived>::inject_request_id(flat_buffer& buffer,
                                                  uint32_t request_id)
{
  if (buffer.size() >= sizeof(impl::Header)) {
    impl::flat::Header_Direct header(buffer, 0);
    header.request_id() = request_id;
  }
}

template <class Derived>
uint32_t
WebSocketSession<Derived>::extract_request_id(const flat_buffer& buffer)
{
  if (buffer.size() >= sizeof(impl::Header)) {
    const impl::flat::Header_Direct header(const_cast<flat_buffer&>(buffer), 0);
    return header.request_id();
  }
  return 0;
}

// Explicit instantiation for the WebSocketSession class template
template class WebSocketSession<AcceptingPlainWebSocketSession>;
template class WebSocketSession<AcceptingSSLWebSocketSession>;
template class WebSocketSession<ClientPlainWebSocketSession>;
template class WebSocketSession<ClientSSLWebSocketSession>;

} // namespace nprpc::impl

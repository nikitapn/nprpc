// Copyright (c) 2021-2025 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by
// LICENSING file in the topmost directory

#include <nprpc/impl/async_connect.hpp>

#include <nprpc/exception.hpp>
#include <nprpc/impl/nprpc_impl.hpp>

#include <boost/asio/dispatch.hpp>
#include <boost/asio/post.hpp>

namespace nprpc::impl {

template<typename Type>
std::shared_ptr<std::promise<typename Type::stream_t>>
AsyncConnector<Type>::run() {
  stream_promise_ = std::make_shared<std::promise<StreamType>>();
  // try to create address from hostname
  boost::system::error_code ec;
  net::ip::make_address_v4(host_, ec);

  if (!ec) {
    if (std::is_same_v<Type, type_wss>) {
      throw nprpc::Exception(
        "SSL connections require a valid FQDN, instead got an IP address: " + host_);
    }
  }

  // Start the timer
  // timer_.async_wait(
  //   net::bind_executor(ioc_, [self = this->shared_from_this()](
  //     const beast::error_code& ec) {
  //       if (!ec) self->do_timeout();
  //     }));

  // Start the resolver
  resolver_.async_resolve(
    host_,
    std::to_string(port_),
    net::bind_executor(ioc_,[self = this->shared_from_this()](
      const beast::error_code& ec, net::ip::tcp::resolver::results_type results)
      {
        self->on_resolve(ec, std::move(results));
      }));

  return stream_promise_;
}

template<typename Type>
void AsyncConnector<Type>::on_resolve(
  const beast::error_code& ec,
  tcp::resolver::results_type results)
{
  if (ec) {
    cleanup();
    stream_promise_->set_exception(
      std::make_exception_ptr(
        std::runtime_error("Resolve failed: " + ec.message())));
    return;
  }

  auto asyncon = [this] <typename EndpointType> (EndpointType& ep) {
    // Make the connection on the IP address we get from a lookup
    // wrap in unique_ptr to manage the stream's lifetime
    auto stream =  std::make_unique<
      beast_tcp_stream_strand>(net::make_strand(ioc_));
    
    // Set a timeout on the operation
    stream->expires_after(std::chrono::seconds(6));

    stream->async_connect(
      ep,
      [stream = std::move(stream), self = this->shared_from_this()]
        (const beast::error_code& ec, tcp::resolver::results_type::endpoint_type endpoint) mutable
      {
        self->on_connect(ec, endpoint, std::move(stream));
      });
  };

  if (results.empty()) {
    cleanup();
    stream_promise_->set_exception(
      std::make_exception_ptr(
        std::runtime_error("No endpoints found for host: " + host_)));
    return;
  }

  asyncon(results);
}

template<typename Type>
void AsyncConnector<Type>::on_connect(
  const beast::error_code& ec,
  tcp::resolver::results_type::endpoint_type endpoint,
  std::unique_ptr<beast_tcp_stream_strand> stream)
{
  if (ec) {
    cleanup();
    stream_promise_->set_exception(
      std::make_exception_ptr(
        nprpc::Exception("Error while connecting to " + host_ + " - "+ ec.message())));
    return;
  }

  // For plain TCP streams, we're done
  if constexpr (std::is_same_v<Type, type_tcp>) {
    cleanup();
    stream_promise_->set_value(std::move(*stream.release()));
    return;
  }
  
  // WebSocket handshake for WebSocket streams
  if constexpr (std::is_same_v<Type, type_ws>) {
    auto ws = std::make_unique<plain_ws>(std::move(*stream.release()));
    ws->async_handshake(
      host_,
      "/",
      net::bind_executor(ioc_,
        [self = this->shared_from_this(), 
         ws = std::move(ws)]
        (const beast::error_code& ec) mutable {
          self->on_ws_handshake(ec, std::move(ws));
        }));
    return;
  }

  // SSL handshake for SSL streams
  // if constexpr (std::is_same_v<Type, type_wss>) {
  //   stream.async_handshake(
  //     ssl::stream_base::client,
  //     net::bind_executor(ioc_,
  //       [self = this->shared_from_this(), 
  //        stream = std::move(stream)]
  //       (const beast::error_code& ec) mutable {
  //         self->on_ssl_handshake(ec, std::move(stream));
  //       }));
  //   return;
  // }
}

template<typename Type>
void AsyncConnector<Type>::on_ws_handshake(
  const beast::error_code& ec,
  std::unique_ptr<plain_ws>&& ws)
{
  if constexpr (std::is_same_v<Type, type_ws>) {
    if (ec) {
      cleanup();
      stream_promise_->set_exception(
        std::make_exception_ptr(
          std::runtime_error("WebSocket handshake failed: " + ec.message())));
      return;
    }

    cleanup();

    ws->next_layer().expires_never(); // Disable any timeouts on the tcp_stream
    stream_promise_->set_value(std::move(*ws.release()));
  }
}

template<typename Type>
void AsyncConnector<Type>::on_ssl_handshake(
  const beast::error_code& ec,
  StreamType&& stream)
{
  // if (ec) {
  //   cleanup();
  //   stream_promise_.set_exception(
  //     std::make_exception_ptr(
  //       std::runtime_error("SSL handshake failed: " + ec.message())));
  //   return;
  // }

  // // For SSL WebSocket streams, perform WebSocket handshake
  // if constexpr (Type::is_websocket) {
  //   stream.next_layer().async_handshake(
  //     host_,
  //     "/",
  //     net::bind_executor(ioc_,
  //       [self = this->shared_from_this(), 
  //        stream = std::move(stream)]
  //       (const beast::error_code& ec) mutable {
  //         self->on_ws_handshake(ec, std::move(stream));
  //       }));
  //   return;
  // }

  // // For plain SSL streams, we're done
  // cleanup();
  // ws->next_layer().next_layer().expires_never(); // Disable any timeouts on the tcp_stream
  // stream_promise_.set_value(std::move(stream));
}

template<typename Type>
void AsyncConnector<Type>::do_timeout() {
  // auto self = this->shared_from_this();
  // net::post(ioc_, [self]() {
  //   self->cleanup();
  //   self->stream_promise_.set_exception(
  //     std::make_exception_ptr(
  //       std::runtime_error("Connection timed out")));
  // });
}

template<typename Type>
void AsyncConnector<Type>::cleanup() {
  // Cancel timer and resolver
  try {
    // timer_.cancel();
    resolver_.cancel();
  } catch (const std::exception& e) {
    // Log or handle the exception if needed
    std::cerr << "Cleanup error: " << e.what() << std::endl;
  }
}

// Explicit template instantiations
template class AsyncConnector<type_tcp>;
template class AsyncConnector<type_ws>;
template class AsyncConnector<type_wss>;

} // namespace nprpc::impl

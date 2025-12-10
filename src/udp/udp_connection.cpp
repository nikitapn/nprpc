// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include "../logging.hpp"
#include <boost/asio/ip/udp.hpp>
#include <mutex>
#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/impl/udp_connection.hpp>
#include <unordered_map>

namespace nprpc::impl {

UdpConnection::UdpConnection(boost::asio::io_context& ioc,
                             const endpoint_type& remote_endpoint)
    : ioc_(ioc)
    , socket_(ioc, boost::asio::ip::udp::v4())
    , remote_endpoint_(remote_endpoint)
{
  // Set socket options for better performance
  socket_.set_option(boost::asio::socket_base::send_buffer_size(65536));
  socket_.set_option(boost::asio::socket_base::receive_buffer_size(65536));

  NPRPC_LOG_INFO("[UDP] Connection created to {}:{}",
                 remote_endpoint_.address().to_string(),
                 remote_endpoint_.port());
}

UdpConnection::UdpConnection(boost::asio::io_context& ioc,
                             const std::string& host,
                             uint16_t port)
    : ioc_(ioc)
    , socket_(ioc, boost::asio::ip::udp::v4())
{
  // Resolve hostname
  boost::asio::ip::udp::resolver resolver(ioc);
  auto endpoints = resolver.resolve(host, std::to_string(port));

  if (endpoints.empty()) {
    throw std::runtime_error("Failed to resolve UDP endpoint: " + host);
  }

  remote_endpoint_ = *endpoints.begin();

  // Set socket options
  socket_.set_option(boost::asio::socket_base::send_buffer_size(65536));
  socket_.set_option(boost::asio::socket_base::receive_buffer_size(65536));

  NPRPC_LOG_INFO("[UDP] Connection created to {}:{}",
                 remote_endpoint_.address().to_string(),
                 remote_endpoint_.port());
}

UdpConnection::~UdpConnection()
{
  close();

  NPRPC_LOG_INFO("[UDP] Connection destroyed");
}

void UdpConnection::send(flat_buffer&& buffer)
{
  // Fire-and-forget - no callback needed
  auto data = buffer.cdata();
  NPRPC_LOG_INFO("[UDP] send() called with {} bytes to {}:{}", data.size(),
                 remote_endpoint_.address().to_string(),
                 remote_endpoint_.port());
  send_async(std::move(buffer), nullptr);
}

void UdpConnection::send_async(
    flat_buffer&& buffer,
    std::function<void(const boost::system::error_code&, std::size_t)> handler)
{
  // Post to strand to ensure thread safety
  boost::asio::post(socket_.get_executor(), [this, self = shared_from_this(),
                                             buf = std::move(buffer),
                                             h = std::move(handler)]() mutable {
    send_queue_.push_back(PendingSend{std::move(buf), std::move(h)});

    if (!sending_) {
      do_send();
    }
  });
}

void UdpConnection::do_send()
{
  if (send_queue_.empty()) {
    sending_ = false;
    return;
  }

  sending_ = true;
  auto& pending = send_queue_.front();

  auto data = pending.buffer.cdata();

  NPRPC_LOG_INFO("[UDP] Sending {} bytes to {}:{}", data.size(),
                 remote_endpoint_.address().to_string(),
                 remote_endpoint_.port());

  socket_.async_send_to(
      boost::asio::buffer(data.data(), data.size()), remote_endpoint_,
      [this, self = shared_from_this()](const boost::system::error_code& ec,
                                        std::size_t bytes_sent) {
        auto pending = std::move(send_queue_.front());
        send_queue_.pop_front();

        if (pending.handler) {
          pending.handler(ec, bytes_sent);
        }

        if (ec) {
          NPRPC_LOG_ERROR("[UDP] Send error: {}", ec.message());
        }

        // Continue with next queued send
        do_send();
      });
}

UdpConnection::endpoint_type UdpConnection::local_endpoint() const
{
  boost::system::error_code ec;
  auto ep = socket_.local_endpoint(ec);
  if (ec) {
    return endpoint_type();
  }
  return ep;
}

void UdpConnection::send_reliable(flat_buffer& buffer,
                                  response_handler handler,
                                  uint32_t timeout_ms,
                                  uint32_t max_retries)
{
  // Blocking variant: buffer is a reference, caller is blocked
  // We can send directly from the buffer without copying
  // Only copy on first timeout for retransmit

  auto* header = reinterpret_cast<Header*>(buffer.data().data());
  uint32_t request_id = header->request_id;

  if (request_id == 0) {
    request_id = next_request_id_++;
    header->request_id = request_id;
  }

  NPRPC_LOG_INFO("[UDP] send_reliable() blocking request_id={} timeout={}ms",
                 request_id, timeout_ms);

  auto timer = std::make_unique<boost::asio::steady_timer>(ioc_);

  // Store pending call WITHOUT copying the buffer yet
  // We pass a pointer to the caller's buffer for potential copy on retransmit
  pending_calls_[request_id] = PendingCall{
      flat_buffer{}, // Empty - will be filled on first timeout
      std::move(handler),
      std::move(timer),
      timeout_ms,
      max_retries,
      0,
      false // request_saved = false (lazy copy)
  };

  if (!receiving_) {
    start_receive();
  }

  auto& pending = pending_calls_[request_id];

  // Start timeout timer - on timeout, we'll copy the buffer for retransmit
  pending.timer->expires_after(std::chrono::milliseconds(timeout_ms));
  pending.timer->async_wait([this, self = shared_from_this(), request_id,
                             &buffer](const boost::system::error_code& ec) {
    if (!ec) {
      // First timeout - need to save the buffer for retransmit
      auto it = pending_calls_.find(request_id);
      if (it != pending_calls_.end() && !it->second.request_saved) {
        // Copy buffer now for retransmit
        flat_buffer saved(buffer.size());
        auto src = buffer.cdata();
        auto mb = saved.prepare(src.size());
        std::memcpy(mb.data(), src.data(), src.size());
        saved.commit(src.size());
        it->second.request = std::move(saved);
        it->second.request_saved = true;
      }
      do_retransmit(request_id);
    }
  });

  // Send directly from caller's buffer - NO COPY!
  auto data = buffer.cdata();
  socket_.async_send_to(
      boost::asio::buffer(data.data(), data.size()), remote_endpoint_,
      [this, self = shared_from_this(), request_id](
          const boost::system::error_code& ec, std::size_t bytes_sent) {
        if (ec) {
          NPRPC_LOG_ERROR("[UDP] Send error for request_id={}: {}", request_id,
                          ec.message());
        }
      });
}

void UdpConnection::send_reliable_async(flat_buffer&& buffer,
                                        response_handler handler,
                                        uint32_t timeout_ms,
                                        uint32_t max_retries)
{
  // Async variant: buffer is moved, caller may be gone
  // Must copy buffer immediately for retransmit

  auto* header = reinterpret_cast<Header*>(buffer.data().data());
  uint32_t request_id = header->request_id;

  if (request_id == 0) {
    request_id = next_request_id_++;
    header->request_id = request_id;
  }

  NPRPC_LOG_INFO("[UDP] send_reliable_async() request_id={} timeout={}ms",
                 request_id, timeout_ms);

  auto timer = std::make_unique<boost::asio::steady_timer>(ioc_);

  // Store pending call WITH the moved buffer
  pending_calls_[request_id] = PendingCall{
      std::move(buffer), // Take ownership of the buffer
      std::move(handler),
      std::move(timer),
      timeout_ms,
      max_retries,
      0,
      true // request_saved = true (we own it)
  };

  if (!receiving_) {
    start_receive();
  }

  auto& pending = pending_calls_[request_id];

  pending.timer->expires_after(std::chrono::milliseconds(timeout_ms));
  pending.timer->async_wait([this, self = shared_from_this(),
                             request_id](const boost::system::error_code& ec) {
    if (!ec) {
      do_retransmit(request_id);
    }
  });

  // Send from our owned buffer
  auto data = pending.request.cdata();
  socket_.async_send_to(
      boost::asio::buffer(data.data(), data.size()), remote_endpoint_,
      [this, self = shared_from_this(), request_id](
          const boost::system::error_code& ec, std::size_t bytes_sent) {
        if (ec) {
          NPRPC_LOG_ERROR("[UDP] Send error for request_id={}: {}", request_id,
                          ec.message());
        }
      });
}

void UdpConnection::start_receive()
{
  if (receiving_ || !socket_.is_open())
    return;

  receiving_ = true;

  socket_.async_receive_from(
      boost::asio::buffer(recv_buffer_), recv_endpoint_,
      [this, self = shared_from_this()](const boost::system::error_code& ec,
                                        std::size_t bytes_received) {
        if (ec) {
          if (ec != boost::asio::error::operation_aborted) {
            NPRPC_LOG_ERROR("[UDP] Receive error: {}", ec.message());
          }
          receiving_ = false;
          return;
        }

        if (bytes_received > 0) {
          handle_response(bytes_received);
        }

        // Continue receiving if we still have pending calls
        if (!pending_calls_.empty()) {
          receiving_ = false;
          start_receive();
        } else {
          receiving_ = false;
        }
      });
}

void UdpConnection::handle_response(size_t bytes_received)
{
  if (bytes_received < sizeof(Header)) {
    NPRPC_LOG_ERROR("[UDP] Response too small: {}", bytes_received);
    return;
  }

  auto* header = reinterpret_cast<const Header*>(recv_buffer_.data());
  uint32_t request_id = header->request_id;

  NPRPC_LOG_INFO("[UDP] Received response request_id={} size={}", request_id,
                 bytes_received);

  // Find pending call
  auto it = pending_calls_.find(request_id);
  if (it == pending_calls_.end()) {
    NPRPC_LOG_INFO("[UDP] No pending call for request_id={}", request_id);
    return;
  }

  // Cancel timer
  it->second.timer->cancel();

  // Copy response to flat_buffer
  flat_buffer response(bytes_received);
  auto mb = response.prepare(bytes_received);
  std::memcpy(mb.data(), recv_buffer_.data(), bytes_received);
  response.commit(bytes_received);

  // Move handler out before erasing
  auto handler = std::move(it->second.handler);
  pending_calls_.erase(it);

  // Call handler with success
  if (handler) {
    handler(boost::system::error_code{}, response);
  }
}

void UdpConnection::do_retransmit(uint32_t request_id)
{
  auto it = pending_calls_.find(request_id);
  if (it == pending_calls_.end()) {
    return; // Already completed or cancelled
  }

  auto& pending = it->second;
  pending.retry_count++;

  if (pending.retry_count > pending.max_retries) {
    // Max retries exceeded - call handler with timeout error
    NPRPC_LOG_ERROR("[UDP] Timeout after {} retries for request_id={}",
                    pending.max_retries, request_id);

    auto handler = std::move(pending.handler);
    pending_calls_.erase(it);

    if (handler) {
      flat_buffer empty_buf;
      handler(boost::asio::error::timed_out, empty_buf);
    }
    return;
  }

  NPRPC_LOG_INFO("[UDP] Retransmit #{} for request_id={}", pending.retry_count,
                 request_id);

  // Restart timer
  pending.timer->expires_after(std::chrono::milliseconds(pending.timeout_ms));
  pending.timer->async_wait([this, self = shared_from_this(),
                             request_id](const boost::system::error_code& ec) {
    if (!ec) {
      do_retransmit(request_id);
    }
  });

  // Send directly from the saved request buffer - NO copy needed!
  auto data = pending.request.cdata();
  socket_.async_send_to(
      boost::asio::buffer(data.data(), data.size()), remote_endpoint_,
      [this, self = shared_from_this(), request_id](
          const boost::system::error_code& ec, std::size_t bytes_sent) {
        if (ec) {
          NPRPC_LOG_ERROR("[UDP] Retransmit error for request_id={}: {}",
                          request_id, ec.message());
        }
      });
}

void UdpConnection::close()
{
  // Cancel all pending calls
  for (auto& [id, pending] : pending_calls_) {
    pending.timer->cancel();
    if (pending.handler) {
      flat_buffer empty_buf;
      pending.handler(boost::asio::error::operation_aborted, empty_buf);
    }
  }
  pending_calls_.clear();
  if (socket_.is_open()) {
    boost::system::error_code ec;
    socket_.close(ec);
  }
}

// Connection cache for reusing UDP connections
// Uses shared_ptr to keep connections alive across calls
namespace {
std::mutex udp_connections_mutex_;
std::unordered_map<std::string, std::shared_ptr<UdpConnection>>
    udp_connections_;

std::string make_key(const std::string& host, uint16_t port)
{
  return host + ":" + std::to_string(port);
}
} // namespace

void clear_udp_connections()
{
  std::lock_guard<std::mutex> lock(udp_connections_mutex_);
  udp_connections_.clear();
}

NPRPC_API std::shared_ptr<UdpConnection> get_udp_connection(
    boost::asio::io_context& ioc, const std::string& host, uint16_t port)
{
  std::lock_guard<std::mutex> lock(udp_connections_mutex_);

  auto key = make_key(host, port);

  // Check if we have a cached connection
  auto it = udp_connections_.find(key);
  if (it != udp_connections_.end()) {
    if (it->second->is_open()) {
      return it->second;
    }
    // Connection closed, remove it
    udp_connections_.erase(it);
  }

  // Create new connection and cache it
  auto conn = std::make_shared<UdpConnection>(ioc, host, port);
  udp_connections_[key] = conn;

  return conn;
}

} // namespace nprpc::impl

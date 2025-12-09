// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// This file is a part of npsystem (Distributed Control System) and covered by
// LICENSING file in the topmost directory

#pragma once

#include <array>
#include <functional>
#include <memory>

#include <boost/asio/ip/udp.hpp>

#include <nprpc/flat_buffer.hpp>
#include <nprpc/nprpc.hpp>

namespace nprpc::impl {

/**
 * @brief UDP listener for receiving RPC datagrams
 *
 * Listens on a UDP port and dispatches incoming messages to the
 * appropriate object servants. Unlike TCP, each datagram is
 * independent and doesn't maintain session state.
 *
 * For fire-and-forget calls, no response is sent.
 * For reliable calls, a response is sent back to the sender.
 */
class UdpListener : public std::enable_shared_from_this<UdpListener>
{
public:
  static constexpr size_t MAX_DATAGRAM_SIZE = 65507; // Max UDP payload

  using endpoint_type = boost::asio::ip::udp::endpoint;
  using receive_handler = std::function<void(
      const endpoint_type& sender, const char* data, size_t size)>;

  /**
   * @brief Create UDP listener on specified port
   *
   * @param ioc IO context for async operations
   * @param port UDP port to listen on
   */
  UdpListener(boost::asio::io_context& ioc, uint16_t port);

  ~UdpListener();

  /**
   * @brief Start listening for datagrams
   *
   * Begins async receive loop. Call this after setting up handlers.
   */
  void start();

  /**
   * @brief Stop listening
   */
  void stop();

  /**
   * @brief Send response datagram to specific endpoint
   *
   * Used for reliable UDP calls that need a reply.
   *
   * @param target Destination endpoint
   * @param buffer Response buffer
   */
  void send_response(const endpoint_type& target, flat_buffer&& buffer);

  /**
   * @brief Get the local endpoint
   */
  endpoint_type local_endpoint() const;

  /**
   * @brief Check if listener is running
   */
  bool is_running() const noexcept { return running_; }

  /**
   * @brief Get listening port
   */
  uint16_t port() const noexcept { return port_; }

private:
  void do_receive();
  void handle_datagram(const endpoint_type& sender, size_t bytes_received);

  boost::asio::ip::udp::socket socket_;
  uint16_t port_;
  bool running_ = false;

  // Receive buffer
  std::array<char, MAX_DATAGRAM_SIZE> recv_buffer_;
  endpoint_type sender_endpoint_;
};

/**
 * @brief Start UDP listener on the RPC
 *
 * @param ioc IO context
 * @param port UDP port to listen on
 * @return Shared pointer to listener
 */
NPRPC_API std::shared_ptr<UdpListener>
start_udp_listener(boost::asio::io_context& ioc, uint16_t port);

/**
 * @brief Stop and release the global UDP listener
 */
void stop_udp_listener();

} // namespace nprpc::impl

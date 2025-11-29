// Copyright (c) 2025 nikitapnn1@gmail.com
// SPDX-License-Identifier: MIT
#pragma once

#ifdef NPRPC_QUIC_ENABLED

#include <nprpc/export.hpp>
#include <msquic.h>
#include <boost/asio.hpp>
#include <memory>
#include <string>
#include <functional>
#include <mutex>
#include <unordered_map>

namespace nprpc::impl {

// Forward declarations
class QuicConnection;
class QuicListener;

/**
 * MsQuic API wrapper - manages the QUIC API handle
 * 
 * This is a singleton that initializes MsQuic once and provides
 * access to the API table.
 */
class NPRPC_API QuicApi {
public:
  static QuicApi& instance();
  
  const QUIC_API_TABLE* api() const { return api_; }
  HQUIC registration() const { return registration_; }
  
  // Configuration helpers
  HQUIC create_configuration(
    const char* alpn,
    bool is_server,
    const char* cert_file = nullptr,
    const char* key_file = nullptr
  );
  
  ~QuicApi();

private:
  QuicApi();
  QuicApi(const QuicApi&) = delete;
  QuicApi& operator=(const QuicApi&) = delete;
  
  const QUIC_API_TABLE* api_ = nullptr;
  HQUIC registration_ = nullptr;
};

/**
 * QUIC Connection - client side
 * 
 * Manages a QUIC connection to a server.
 * Uses streams for RPC calls and optionally datagrams for fire-and-forget.
 */
class NPRPC_API QuicConnection : public std::enable_shared_from_this<QuicConnection> {
public:
  using ConnectCallback = std::function<void(bool success)>;
  using ReceiveCallback = std::function<void(const uint8_t* data, size_t len)>;
  
  QuicConnection(boost::asio::io_context& ioc);
  ~QuicConnection();
  
  // Connect to server
  void async_connect(
    const std::string& host,
    uint16_t port,
    ConnectCallback callback
  );
  
  // Send data on a stream (reliable)
  void async_send_stream(
    const uint8_t* data,
    size_t len,
    std::function<void(bool)> callback
  );
  
  // Send datagram (unreliable, if supported)
  bool send_datagram(const uint8_t* data, size_t len);
  
  // Set callback for received data
  void set_receive_callback(ReceiveCallback callback);
  
  // Close connection
  void close();
  
  bool is_connected() const { return connected_; }
  
private:
  // MsQuic callbacks
  static QUIC_STATUS QUIC_API connection_callback(
    HQUIC connection,
    void* context,
    QUIC_CONNECTION_EVENT* event
  );
  
  static QUIC_STATUS QUIC_API stream_callback(
    HQUIC stream,
    void* context,
    QUIC_STREAM_EVENT* event
  );
  
  void handle_connection_event(QUIC_CONNECTION_EVENT* event);
  void handle_stream_event(HQUIC stream, QUIC_STREAM_EVENT* event);
  
  boost::asio::io_context& ioc_;
  HQUIC connection_ = nullptr;
  HQUIC configuration_ = nullptr;
  HQUIC stream_ = nullptr;  // Main bidirectional stream
  
  std::atomic<bool> connected_{false};
  ConnectCallback connect_callback_;
  ReceiveCallback receive_callback_;
  
  std::mutex mutex_;
};

/**
 * QUIC Listener - server side
 * 
 * Listens for incoming QUIC connections and creates sessions.
 */
class NPRPC_API QuicListener {
public:
  using AcceptCallback = std::function<void(std::shared_ptr<QuicConnection>)>;
  
  QuicListener(
    boost::asio::io_context& ioc,
    const std::string& cert_file,
    const std::string& key_file
  );
  ~QuicListener();
  
  // Start listening
  void start(uint16_t port, AcceptCallback callback);
  
  // Stop listening
  void stop();
  
private:
  static QUIC_STATUS QUIC_API listener_callback(
    HQUIC listener,
    void* context,
    QUIC_LISTENER_EVENT* event
  );
  
  void handle_listener_event(QUIC_LISTENER_EVENT* event);
  
  boost::asio::io_context& ioc_;
  HQUIC listener_ = nullptr;
  HQUIC configuration_ = nullptr;
  
  AcceptCallback accept_callback_;
  std::string cert_file_;
  std::string key_file_;
};

} // namespace nprpc::impl

#endif // NPRPC_QUIC_ENABLED

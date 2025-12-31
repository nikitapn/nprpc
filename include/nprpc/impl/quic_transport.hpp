// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#ifdef NPRPC_QUIC_ENABLED

#include <boost/asio.hpp>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <msquic.h>
#include <mutex>
#include <nprpc/endpoint.hpp>
#include <nprpc/export.hpp>
#include <nprpc/flat_buffer.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace nprpc::impl {

// Forward declarations
class QuicConnection;
class QuicServerConnection;
class QuicListener;
class Session;

/**
 * MsQuic API wrapper - manages the QUIC API handle
 *
 * This is a singleton that initializes MsQuic once and provides
 * access to the API table.
 */
class NPRPC_API QuicApi
{
public:
  static QuicApi& instance();

  const QUIC_API_TABLE* api() const { return api_; }
  HQUIC registration() const { return registration_; }

  // Configuration helpers
  HQUIC create_configuration(const char* alpn,
                             bool is_server,
                             const char* cert_file = nullptr,
                             const char* key_file = nullptr);

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
 * Supports native QUIC streams for streaming RPC - the server opens dedicated
 * streams for each NPRPC stream, which the client accepts and routes data from.
 */
class NPRPC_API QuicConnection
    : public std::enable_shared_from_this<QuicConnection>
{
public:
  using ConnectCallback = std::function<void(bool success)>;
  using ReceiveCallback = std::function<void(const uint8_t* data, size_t len)>;
  using MessageCallback = std::function<void(std::vector<uint8_t>&&)>;
  // Callback for data received on a dedicated streaming QUIC stream
  // Parameters: stream_id, data
  using DataStreamCallback = std::function<void(uint64_t, std::vector<uint8_t>&&)>;

  QuicConnection(boost::asio::io_context& ioc);
  ~QuicConnection();

  // Connect to server
  void async_connect(const std::string& host,
                     uint16_t port,
                     ConnectCallback callback);

  // Send/receive for RPC - blocking style
  void send_receive(flat_buffer& buffer, uint32_t timeout_ms);

  // Send data on a stream (reliable)
  void async_send_stream(const uint8_t* data,
                         size_t len,
                         std::function<void(bool)> callback);

  // Send data without waiting for reply (fire-and-forget)
  bool send(const void* data, size_t len);

  // Send datagram (unreliable, if supported)
  bool send_datagram(const uint8_t* data, size_t len);

  // Set callback for received data (raw bytes)
  void set_receive_callback(ReceiveCallback callback);

  // Set callback for complete assembled messages (on main RPC stream)
  void set_message_callback(MessageCallback callback);

  // Set callback for data received on dedicated streaming QUIC streams
  void set_data_stream_callback(DataStreamCallback callback);

  // Close connection
  void close();

  bool is_connected() const { return connected_; }

  boost::asio::io_context& io_context() { return ioc_; }

private:
  friend class QuicListener;

  // MsQuic callbacks
  static QUIC_STATUS QUIC_API connection_callback(HQUIC connection,
                                                  void* context,
                                                  QUIC_CONNECTION_EVENT* event);

  static QUIC_STATUS QUIC_API stream_callback(HQUIC stream,
                                              void* context,
                                              QUIC_STREAM_EVENT* event);

  // Callback for server-opened data streams (for streaming RPC)
  static QUIC_STATUS QUIC_API data_stream_callback(HQUIC stream,
                                                   void* context,
                                                   QUIC_STREAM_EVENT* event);

  void handle_connection_event(QUIC_CONNECTION_EVENT* event);
  void handle_stream_event(HQUIC stream, QUIC_STREAM_EVENT* event);
  void handle_data_stream_event(HQUIC stream, QUIC_STREAM_EVENT* event);
  void process_receive_buffer();
  void process_data_stream_buffer(HQUIC stream);

  boost::asio::io_context& ioc_;
  HQUIC connection_ = nullptr;
  HQUIC configuration_ = nullptr;
  HQUIC stream_ = nullptr; // Main bidirectional stream for RPC

  // Server-opened data streams for streaming (QUIC stream -> receive buffer + stream_id)
  struct DataStreamInfo {
    std::vector<uint8_t> receive_buffer;
    uint64_t stream_id = 0;  // NPRPC stream_id (from first message)
    bool stream_id_known = false;
  };
  std::unordered_map<HQUIC, DataStreamInfo> data_streams_;
  std::mutex data_streams_mutex_;

  std::atomic<bool> connected_{false};
  std::atomic<bool> datagram_send_enabled_{
      false};                      // Set when peer accepts datagrams
  uint16_t max_datagram_size_ = 0; // Maximum datagram size peer supports
  ConnectCallback connect_callback_;
  ReceiveCallback receive_callback_;
  MessageCallback message_callback_;
  DataStreamCallback data_stream_callback_;  // Callback for data on dedicated streams
  std::vector<uint8_t> receive_buffer_;

  // For synchronous send/receive
  std::mutex mutex_;
  std::condition_variable cv_;
  std::vector<uint8_t> pending_receive_;
  bool receive_complete_ = false;
};

/**
 * QUIC Server Connection - wraps a server-side accepted connection
 *
 * This is similar to QuicConnection but manages the server side of an
 * accepted QUIC connection. It handles RPC request/response pairs.
 * Supports native QUIC streams for streaming RPC - each NPRPC stream
 * gets its own dedicated QUIC stream for zero head-of-line blocking.
 */
class NPRPC_API QuicServerConnection
    : public std::enable_shared_from_this<QuicServerConnection>
{
public:
  using MessageCallback = std::function<void(std::vector<uint8_t>&&)>;
  using DatagramCallback = std::function<void(std::vector<uint8_t>&&)>;

  QuicServerConnection(boost::asio::io_context& ioc,
                       HQUIC connection,
                       HQUIC configuration);
  ~QuicServerConnection();

  // Set callback for complete messages (stream - reliable, expects response)
  void set_message_callback(MessageCallback callback);

  // Set callback for datagram messages (unreliable, no response)
  void set_datagram_callback(DatagramCallback callback);

  // Send response data on main RPC stream
  bool send(const void* data, size_t len);

  // Native QUIC stream management for streaming RPC
  // Opens a new dedicated QUIC stream for an NPRPC stream
  bool open_data_stream(uint64_t stream_id);
  
  // Send data on a dedicated QUIC stream (for streaming)
  bool send_on_stream(uint64_t stream_id, const void* data, size_t len);
  
  // Close a dedicated QUIC stream
  void close_data_stream(uint64_t stream_id);

  // Get remote address for endpoint
  std::string remote_address() const { return remote_addr_; }
  uint16_t remote_port() const { return remote_port_; }

  void start();
  void close();

private:
  static QUIC_STATUS QUIC_API connection_callback(HQUIC connection,
                                                  void* context,
                                                  QUIC_CONNECTION_EVENT* event);

  static QUIC_STATUS QUIC_API stream_callback(HQUIC stream,
                                              void* context,
                                              QUIC_STREAM_EVENT* event);

  // Callback for dedicated data streams (different from main RPC stream)
  static QUIC_STATUS QUIC_API data_stream_callback(HQUIC stream,
                                                   void* context,
                                                   QUIC_STREAM_EVENT* event);

  void handle_connection_event(QUIC_CONNECTION_EVENT* event);
  void handle_stream_event(HQUIC stream, QUIC_STREAM_EVENT* event);
  void handle_data_stream_event(HQUIC stream, uint64_t stream_id, QUIC_STREAM_EVENT* event);
  void process_receive_buffer();

  boost::asio::io_context& ioc_;
  HQUIC connection_;
  HQUIC configuration_;
  HQUIC stream_ = nullptr;  // Main RPC stream

  // Native QUIC streams for streaming RPC (stream_id -> QUIC stream handle)
  std::unordered_map<uint64_t, HQUIC> data_streams_;
  std::mutex data_streams_mutex_;

  MessageCallback message_callback_;
  DatagramCallback datagram_callback_;
  std::vector<uint8_t> receive_buffer_;
  std::string remote_addr_;
  uint16_t remote_port_ = 0;

  std::mutex mutex_;
};

/**
 * QUIC Listener - server side
 *
 * Listens for incoming QUIC connections and creates server sessions.
 */
class NPRPC_API QuicListener
{
public:
  using AcceptCallback =
      std::function<void(std::shared_ptr<QuicServerConnection>)>;

  QuicListener(boost::asio::io_context& ioc,
               const std::string& cert_file,
               const std::string& key_file);
  ~QuicListener();

  // Start listening
  void start(uint16_t port, AcceptCallback callback);

  // Stop listening and close all connections
  void stop();

  // Track a connection (called internally)
  void add_connection(std::shared_ptr<QuicServerConnection> conn);

  // Remove a connection (called when connection closes)
  void remove_connection(QuicServerConnection* conn);

private:
  static QUIC_STATUS QUIC_API listener_callback(HQUIC listener,
                                                void* context,
                                                QUIC_LISTENER_EVENT* event);

  void handle_listener_event(QUIC_LISTENER_EVENT* event);

  boost::asio::io_context& ioc_;
  HQUIC listener_ = nullptr;
  HQUIC configuration_ = nullptr;

  AcceptCallback accept_callback_;
  std::string cert_file_;
  std::string key_file_;

  // Track active connections for cleanup
  std::mutex connections_mutex_;
  std::vector<std::shared_ptr<QuicServerConnection>> connections_;
};

// Global functions for QUIC transport initialization
NPRPC_API void init_quic(boost::asio::io_context& ioc);
NPRPC_API void stop_quic_listener();

// Create a client-side QUIC session (called from get_session)
NPRPC_API std::shared_ptr<Session>
make_quic_client_session(const EndPoint& endpoint,
                         boost::asio::io_context& ioc);

} // namespace nprpc::impl

#endif // NPRPC_QUIC_ENABLED

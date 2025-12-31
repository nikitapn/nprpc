// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include <nprpc/impl/quic_transport.hpp>

#ifdef NPRPC_QUIC_ENABLED

#include "../logging.hpp"
#include <chrono>
#include <cstring>
#include <format>
#include <iostream>
#include <nprpc/common.hpp>
#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/impl/session.hpp>
#include <nprpc/impl/stream_manager.hpp>

#define NPRPC_QUIC_DEBUG 0

#if NPRPC_QUIC_DEBUG
#define NPRPC_QUIC_DEBUG_LOG(format_string, ...)                               \
  NPRPC_LOG_DEBUG("[QUIC DEBUG] " format_string, ##__VA_ARGS__)
#else
#define NPRPC_QUIC_DEBUG_LOG(format_string, ...)                               \
  do {                                                                         \
  } while (0)
#endif

namespace nprpc::impl {

// ALPN for NPRPC over QUIC
static const QUIC_BUFFER alpn_buffer = {sizeof("nprpc") - 1, (uint8_t*)"nprpc"};

// Global QUIC listener instance
static std::shared_ptr<QuicListener> g_quic_listener;

//==============================================================================
// QuicApi - Singleton for MsQuic initialization
//==============================================================================

QuicApi& QuicApi::instance()
{
  static QuicApi instance;
  return instance;
}

QuicApi::QuicApi()
{
  // Open the MsQuic library
  QUIC_STATUS status = MsQuicOpen2(&api_);
  if (QUIC_FAILED(status)) {
    throw std::runtime_error("MsQuicOpen2 failed: " + std::to_string(status));
  }

  // Create registration
  QUIC_REGISTRATION_CONFIG reg_config = {"nprpc",
                                         QUIC_EXECUTION_PROFILE_LOW_LATENCY};

  status = api_->RegistrationOpen(&reg_config, &registration_);
  if (QUIC_FAILED(status)) {
    MsQuicClose(api_);
    throw std::runtime_error("RegistrationOpen failed: " +
                             std::to_string(status));
  }

  NPRPC_LOG_INFO("[QUIC] MsQuic initialized successfully");
}

QuicApi::~QuicApi()
{
  if (registration_) {
    api_->RegistrationClose(registration_);
  }
  if (api_) {
    MsQuicClose(api_);
  }
}

HQUIC QuicApi::create_configuration(const char* alpn,
                                    bool is_server,
                                    const char* cert_file,
                                    const char* key_file)
{
  QUIC_BUFFER alpn_buf = {(uint32_t)strlen(alpn), (uint8_t*)alpn};

  // Settings for the configuration
  QUIC_SETTINGS settings = {};
  settings.IdleTimeoutMs = 30000; // 30 second idle timeout
  settings.IsSet.IdleTimeoutMs = TRUE;
  settings.PeerBidiStreamCount = 100; // Allow 100 bidirectional streams
  settings.IsSet.PeerBidiStreamCount = TRUE;
  settings.PeerUnidiStreamCount = 100; // Allow 100 unidirectional streams
  settings.IsSet.PeerUnidiStreamCount = TRUE;
  settings.DatagramReceiveEnabled =
      TRUE; // Enable QUIC datagrams for unreliable
  settings.IsSet.DatagramReceiveEnabled = TRUE;

  HQUIC configuration = nullptr;
  QUIC_STATUS status =
      api_->ConfigurationOpen(registration_, &alpn_buf, 1, &settings,
                              sizeof(settings), nullptr, &configuration);

  if (QUIC_FAILED(status)) {
    NPRPC_LOG_ERROR("[QUIC] ConfigurationOpen failed: {}", status);
    return nullptr;
  }

  // Configure credentials
  QUIC_CREDENTIAL_CONFIG cred_config = {};

  if (is_server && cert_file && key_file) {
    // Server with certificate
    QUIC_CERTIFICATE_FILE cert = {};
    cert.CertificateFile = cert_file;
    cert.PrivateKeyFile = key_file;

    cred_config.Type = QUIC_CREDENTIAL_TYPE_CERTIFICATE_FILE;
    cred_config.CertificateFile = &cert;

    status = api_->ConfigurationLoadCredential(configuration, &cred_config);
  } else if (!is_server) {
    // Client - no certificate validation for now (development)
    cred_config.Type = QUIC_CREDENTIAL_TYPE_NONE;
    cred_config.Flags = QUIC_CREDENTIAL_FLAG_CLIENT;
    cred_config.Flags |=
        QUIC_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION; // TODO: remove in
                                                        // production

    status = api_->ConfigurationLoadCredential(configuration, &cred_config);
  }

  if (QUIC_FAILED(status)) {
    NPRPC_LOG_ERROR("[QUIC] ConfigurationLoadCredential failed: {}", status);
    api_->ConfigurationClose(configuration);
    return nullptr;
  }

  return configuration;
}

//==============================================================================
// QuicConnection - Client connection
//==============================================================================

QuicConnection::QuicConnection(boost::asio::io_context& ioc)
    : ioc_(ioc)
{
}

QuicConnection::~QuicConnection() { close(); }

void QuicConnection::async_connect(const std::string& host,
                                   uint16_t port,
                                   ConnectCallback callback)
{
  NPRPC_QUIC_DEBUG_LOG("async_connect called: {}:{}", host, port);
  auto& quic = QuicApi::instance();

  // Create client configuration
  configuration_ = quic.create_configuration("nprpc", false);
  if (!configuration_) {
    NPRPC_QUIC_DEBUG_LOG("create_configuration failed");
    boost::asio::post(ioc_, [callback]() { callback(false); });
    return;
  }
  NPRPC_QUIC_DEBUG_LOG("configuration created");

  connect_callback_ = std::move(callback);

  // Create connection
  QUIC_STATUS status = quic.api()->ConnectionOpen(
      quic.registration(), connection_callback, this, &connection_);

  if (QUIC_FAILED(status)) {
    NPRPC_LOG_ERROR("[QUIC] ConnectionOpen failed: {}", status);
    quic.api()->ConfigurationClose(configuration_);
    configuration_ = nullptr;
    boost::asio::post(ioc_,
                      [callback = connect_callback_]() { callback(false); });
    return;
  }
  NPRPC_QUIC_DEBUG_LOG("connection opened");

  // Start connection
  status = quic.api()->ConnectionStart(connection_, configuration_,
                                       QUIC_ADDRESS_FAMILY_UNSPEC, host.c_str(),
                                       port);

  if (QUIC_FAILED(status)) {
    NPRPC_LOG_ERROR("[QUIC] ConnectionStart failed: {}", status);
    quic.api()->ConnectionClose(connection_);
    connection_ = nullptr;
    quic.api()->ConfigurationClose(configuration_);
    configuration_ = nullptr;
    boost::asio::post(ioc_,
                      [callback = connect_callback_]() { callback(false); });
  } else {
    NPRPC_QUIC_DEBUG_LOG("connection start initiated");
  }
}

void QuicConnection::send_receive(flat_buffer& buffer, uint32_t timeout_ms)
{
  if (!connected_ || !stream_) {
    throw nprpc::ExceptionCommFailure("QUIC connection not established");
  }

  auto& quic = QuicApi::instance();

  // Prepare for receive - process_receive_buffer will set receive_complete_
  {
    std::lock_guard lock(mutex_);
    pending_receive_.clear();
    receive_complete_ = false;
  }

  // Send the request
  auto data = buffer.cdata();

  QUIC_BUFFER* send_buf = new QUIC_BUFFER;
  send_buf->Length = static_cast<uint32_t>(data.size());
  send_buf->Buffer = new uint8_t[data.size()];
  std::memcpy(send_buf->Buffer, data.data(), data.size());

  QUIC_STATUS status = quic.api()->StreamSend(stream_, send_buf, 1,
                                              QUIC_SEND_FLAG_NONE, send_buf);

  if (QUIC_FAILED(status)) {
    delete[] send_buf->Buffer;
    delete send_buf;
    throw nprpc::ExceptionCommFailure("QUIC StreamSend failed");
  }

  // Wait for response with timeout
  // process_receive_buffer() will set receive_complete_ and put data in pending_receive_
  std::unique_lock lock(mutex_);
  auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

  if (!cv_.wait_until(lock, deadline, [this] { return receive_complete_; })) {
    throw nprpc::ExceptionTimeout();
  }

  // Copy response to buffer
  buffer.consume(buffer.size());
  auto mb = buffer.prepare(pending_receive_.size());
  std::memcpy(mb.data(), pending_receive_.data(), pending_receive_.size());
  buffer.commit(pending_receive_.size());
}

bool QuicConnection::send(const void* data, size_t len)
{
  if (!connected_ || !stream_) {
    return false;
  }

  auto& quic = QuicApi::instance();

  QUIC_BUFFER* send_buf = new QUIC_BUFFER;
  send_buf->Length = static_cast<uint32_t>(len);
  send_buf->Buffer = new uint8_t[len];
  std::memcpy(send_buf->Buffer, data, len);

  QUIC_STATUS status = quic.api()->StreamSend(stream_, send_buf, 1,
                                              QUIC_SEND_FLAG_NONE, send_buf);

  if (QUIC_FAILED(status)) {
    delete[] send_buf->Buffer;
    delete send_buf;
    return false;
  }
  return true;
}

void QuicConnection::async_send_stream(const uint8_t* data,
                                       size_t len,
                                       std::function<void(bool)> callback)
{
  if (!connected_ || !stream_) {
    boost::asio::post(ioc_, [callback]() { callback(false); });
    return;
  }

  auto& quic = QuicApi::instance();

  // Allocate send buffer
  QUIC_BUFFER* send_buf = new QUIC_BUFFER;
  send_buf->Length = static_cast<uint32_t>(len);
  send_buf->Buffer = new uint8_t[len];
  std::memcpy(send_buf->Buffer, data, len);

  QUIC_STATUS status =
      quic.api()->StreamSend(stream_, send_buf, 1, QUIC_SEND_FLAG_NONE,
                             send_buf // Context for completion
      );

  if (QUIC_FAILED(status)) {
    delete[] send_buf->Buffer;
    delete send_buf;
    boost::asio::post(ioc_, [callback]() { callback(false); });
  } else {
    // Callback will be invoked from stream_callback on SEND_COMPLETE
    boost::asio::post(ioc_, [callback]() { callback(true); });
  }
}

bool QuicConnection::send_datagram(const uint8_t* data, size_t len)
{
  NPRPC_QUIC_DEBUG_LOG(std::format(
      "send_datagram called, len={}, connected={}, datagram_enabled={}", len,
      (bool)connected_, datagram_send_enabled_.load()));
  if (!connected_ || !connection_) {
    NPRPC_QUIC_DEBUG_LOG("send_datagram: not connected!");
    return false;
  }

  if (!datagram_send_enabled_) {
    NPRPC_QUIC_DEBUG_LOG("send_datagram: datagrams not enabled by peer!");
    return false;
  }

  if (len > max_datagram_size_) {
    NPRPC_QUIC_DEBUG_LOG(std::format("send_datagram: size {} exceeds max {}",
                                     len, max_datagram_size_));
    return false;
  }

  auto& quic = QuicApi::instance();

  // Allocate buffer that will be freed on DATAGRAM_SEND_STATE_CHANGED event
  QUIC_BUFFER* send_buf = new QUIC_BUFFER;
  send_buf->Length = static_cast<uint32_t>(len);
  send_buf->Buffer = new uint8_t[len];
  std::memcpy(send_buf->Buffer, data, len);

  QUIC_STATUS status =
      quic.api()->DatagramSend(connection_, send_buf, 1, QUIC_SEND_FLAG_NONE,
                               send_buf // Context for cleanup
      );

  if (QUIC_FAILED(status)) {
    NPRPC_QUIC_DEBUG_LOG(std::format("DatagramSend failed, status={}", status));
    delete[] send_buf->Buffer;
    delete send_buf;
    return false;
  }

  NPRPC_QUIC_DEBUG_LOG("DatagramSend succeeded");
  return true;
}

void QuicConnection::set_receive_callback(ReceiveCallback callback)
{
  std::lock_guard lock(mutex_);
  receive_callback_ = std::move(callback);
}

void QuicConnection::set_message_callback(MessageCallback callback)
{
  std::lock_guard lock(mutex_);
  message_callback_ = std::move(callback);
}

void QuicConnection::set_data_stream_callback(DataStreamCallback callback)
{
  std::lock_guard lock(data_streams_mutex_);
  data_stream_callback_ = std::move(callback);
}

void QuicConnection::set_datagram_callback(DatagramCallback callback)
{
  std::lock_guard lock(mutex_);
  datagram_callback_ = std::move(callback);
}

void QuicConnection::process_receive_buffer()
{
  // NPRPC message format: first 4 bytes = payload size (not including the
  // 4-byte size field) Total message size = payload_size + 4
  // Header format (after size): msg_id(4) + msg_type(4) + request_id(4)
  // msg_type: 0 = Request, 1 = Answer
  while (receive_buffer_.size() >= 4) {
    uint32_t payload_size =
        *reinterpret_cast<uint32_t*>(receive_buffer_.data());
    uint32_t total_size = payload_size + 4;
    NPRPC_QUIC_DEBUG_LOG(std::format("Client process_receive_buffer: payload_size={}, "
                                     "total_size={}, buffer_size={}",
                                     payload_size, total_size,
                                     receive_buffer_.size()));

    if (receive_buffer_.size() < total_size) {
      break; // Incomplete message
    }

    // Extract complete message
    std::vector<uint8_t> message(receive_buffer_.begin(),
                                 receive_buffer_.begin() + total_size);
    receive_buffer_.erase(receive_buffer_.begin(),
                          receive_buffer_.begin() + total_size);

    // Check message type: Answer (1) = reply to our request, Request (0) = server-initiated message (stream)
    // Header: size(4) + msg_id(4) + msg_type(4) + request_id(4)
    // msg_type is at offset 8 (after size and msg_id)
    uint32_t msg_type = 0;  // Default to Request
    if (message.size() >= 12) {
      msg_type = *reinterpret_cast<uint32_t*>(message.data() + 8);
    }
    
    const bool is_response = (msg_type == 1);  // MessageType::Answer = 1
    
    NPRPC_QUIC_DEBUG_LOG(std::format("Client message msg_type={}, is_response={}",
                                     msg_type, is_response));

    {
      std::lock_guard lock(mutex_);
      
      // If we're waiting for a synchronous response AND this is a response
      if (!receive_complete_ && is_response) {
        // This is the reply we're waiting for
        pending_receive_ = std::move(message);
        receive_complete_ = true;
        cv_.notify_one();
      } else if (message_callback_) {
        // Dispatch to message callback (for stream messages or if not waiting)
        NPRPC_QUIC_DEBUG_LOG(
            std::format("Client posting message to callback, size={}", message.size()));
        boost::asio::post(ioc_, [this, self = shared_from_this(),
                                 msg = std::move(message)]() mutable {
          MessageCallback cb;
          {
            std::lock_guard lk(mutex_);
            cb = message_callback_;
          }
          if (cb) {
            cb(std::move(msg));
          }
        });
      } else {
        NPRPC_QUIC_DEBUG_LOG("Client: message dropped (no callback and not waiting for response)");
      }
    }
  }
}

void QuicConnection::close()
{
  auto& quic = QuicApi::instance();

  if (stream_) {
    quic.api()->StreamClose(stream_);
    stream_ = nullptr;
  }

  if (connection_) {
    quic.api()->ConnectionClose(connection_);
    connection_ = nullptr;
  }

  if (configuration_) {
    quic.api()->ConfigurationClose(configuration_);
    configuration_ = nullptr;
  }

  connected_ = false;
}

QUIC_STATUS QUIC_API QuicConnection::connection_callback(
    HQUIC connection, void* context, QUIC_CONNECTION_EVENT* event)
{
  NPRPC_QUIC_DEBUG_LOG(
      std::format("connection_callback event type: {}", event->Type));
  auto* self = static_cast<QuicConnection*>(context);
  self->handle_connection_event(event);
  return QUIC_STATUS_SUCCESS;
}

void QuicConnection::handle_connection_event(QUIC_CONNECTION_EVENT* event)
{
  switch (event->Type) {
  case QUIC_CONNECTION_EVENT_CONNECTED: {
    NPRPC_QUIC_DEBUG_LOG("CONNECTED event received");
    NPRPC_LOG_INFO("[QUIC] Client connection established");
    connected_ = true;

    // Open a bidirectional stream for RPC
    auto& quic = QuicApi::instance();
    QUIC_STATUS status =
        quic.api()->StreamOpen(connection_, QUIC_STREAM_OPEN_FLAG_NONE,
                               stream_callback, this, &stream_);

    if (QUIC_SUCCEEDED(status)) {
      status = quic.api()->StreamStart(stream_, QUIC_STREAM_START_FLAG_NONE);
    }

    if (connect_callback_) {
      bool success = QUIC_SUCCEEDED(status);
      NPRPC_QUIC_DEBUG_LOG(
          "posting callback, success={}", success);
      auto cb = std::move(connect_callback_);
      boost::asio::post(ioc_, [cb, success]() {
        NPRPC_QUIC_DEBUG_LOG("callback executing");
        cb(success);
      });
    }
    break;
  }

  case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
    NPRPC_LOG_INFO("[QUIC] Connection shutdown by transport: {}",
                   event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status);
    connected_ = false;
    {
      std::lock_guard lock(mutex_);
      receive_complete_ = true;
      cv_.notify_all();
    }
    break;

  case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
    NPRPC_LOG_INFO("[QUIC] Connection shutdown by peer");
    connected_ = false;
    {
      std::lock_guard lock(mutex_);
      receive_complete_ = true;
      cv_.notify_all();
    }
    break;

  case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
    connected_ = false;
    datagram_send_enabled_ = false;
    // Clean up any data streams
    {
      std::lock_guard lock(data_streams_mutex_);
      for (auto& [handle, info] : data_streams_) {
        QuicApi::instance().api()->StreamClose(handle);
      }
      data_streams_.clear();
    }
    break;

  case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED: {
    // Server has opened a data stream for streaming RPC
    HQUIC stream = event->PEER_STREAM_STARTED.Stream;
    NPRPC_LOG_INFO("[QUIC] Client: Server opened a data stream");
    
    // Set up callback for this stream
    auto& quic = QuicApi::instance();
    quic.api()->SetCallbackHandler(
        stream, reinterpret_cast<void*>(data_stream_callback), this);
    
    // Initialize stream info
    {
      std::lock_guard lock(data_streams_mutex_);
      data_streams_[stream] = DataStreamInfo{};
    }
    break;
  }

  case QUIC_CONNECTION_EVENT_DATAGRAM_STATE_CHANGED: {
    // Peer has indicated whether they support receiving datagrams
    datagram_send_enabled_ = event->DATAGRAM_STATE_CHANGED.SendEnabled;
    max_datagram_size_ = event->DATAGRAM_STATE_CHANGED.MaxSendLength;
    NPRPC_QUIC_DEBUG_LOG(
        "DATAGRAM_STATE_CHANGED: SendEnabled={}, MaxSendLength={}",
                    (int)datagram_send_enabled_.load(), max_datagram_size_);
    break;
  }

  case QUIC_CONNECTION_EVENT_DATAGRAM_RECEIVED: {
    // Handle received datagram (unreliable stream data from server)
    NPRPC_QUIC_DEBUG_LOG("Client DATAGRAM_RECEIVED!");
    std::lock_guard lock(mutex_);
    auto& buf = event->DATAGRAM_RECEIVED.Buffer;
    if (datagram_callback_) {
      std::vector<uint8_t> data(buf->Buffer, buf->Buffer + buf->Length);
      boost::asio::post(ioc_, [this, self = shared_from_this(),
                               data = std::move(data)]() mutable {
        DatagramCallback cb;
        {
          std::lock_guard lk(mutex_);
          cb = datagram_callback_;
        }
        if (cb) {
          cb(std::move(data));
        }
      });
    } else if (receive_callback_) {
      // Fallback to raw receive callback
      receive_callback_(buf->Buffer, buf->Length);
    }
    break;
  }

  case QUIC_CONNECTION_EVENT_DATAGRAM_SEND_STATE_CHANGED: {
    // Clean up datagram send buffer only when send is complete
    // States: 0=Unknown, 1=Sent, 2=LostSuspect, 3=LostDiscarded,
    // 4=Acknowledged, 5=AcknowledgedSpurious, 6=Canceled
    auto state = event->DATAGRAM_SEND_STATE_CHANGED.State;
    NPRPC_QUIC_DEBUG_LOG(
        std::format("DATAGRAM_SEND_STATE_CHANGED: state={}", (int)state));

    // Only free on terminal states
    if (state == QUIC_DATAGRAM_SEND_ACKNOWLEDGED ||
        state == QUIC_DATAGRAM_SEND_ACKNOWLEDGED_SPURIOUS ||
        state == QUIC_DATAGRAM_SEND_LOST_DISCARDED ||
        state == QUIC_DATAGRAM_SEND_CANCELED) {
      auto* send_buf = static_cast<QUIC_BUFFER*>(
          event->DATAGRAM_SEND_STATE_CHANGED.ClientContext);
      if (send_buf && send_buf->Buffer) {
        delete[] send_buf->Buffer;
        delete send_buf;
      }
    }
    break;
  }

  default:
    break;
  }
}

QUIC_STATUS QUIC_API QuicConnection::stream_callback(HQUIC stream,
                                                     void* context,
                                                     QUIC_STREAM_EVENT* event)
{
  auto* self = static_cast<QuicConnection*>(context);
  self->handle_stream_event(stream, event);
  return QUIC_STATUS_SUCCESS;
}

void QuicConnection::handle_stream_event(HQUIC stream, QUIC_STREAM_EVENT* event)
{
  NPRPC_QUIC_DEBUG_LOG(
      "Client stream_event type: {}", event->Type);
  switch (event->Type) {
  case QUIC_STREAM_EVENT_RECEIVE: {
    NPRPC_QUIC_DEBUG_LOG("Client STREAM_RECEIVE, buffers: {}",
                                     event->RECEIVE.BufferCount);
    // Append data to receive buffer
    for (uint32_t i = 0; i < event->RECEIVE.BufferCount; ++i) {
      auto& buf = event->RECEIVE.Buffers[i];
      NPRPC_QUIC_DEBUG_LOG(
          "Client received {} bytes", buf.Length);
      receive_buffer_.insert(receive_buffer_.end(), buf.Buffer,
                             buf.Buffer + buf.Length);
    }

    // Process complete messages
    process_receive_buffer();
    break;
  }

  case QUIC_STREAM_EVENT_SEND_COMPLETE: {
    // Clean up send buffer
    auto* send_buf =
        static_cast<QUIC_BUFFER*>(event->SEND_COMPLETE.ClientContext);
    if (send_buf) {
      delete[] send_buf->Buffer;
      delete send_buf;
    }
    break;
  }

  case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN:
  case QUIC_STREAM_EVENT_PEER_SEND_ABORTED:
  case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
    break;

  default:
    break;
  }
}

// Callback for server-opened data streams (client side)
QUIC_STATUS QUIC_API QuicConnection::data_stream_callback(
    HQUIC stream, void* context, QUIC_STREAM_EVENT* event)
{
  auto* self = static_cast<QuicConnection*>(context);
  self->handle_data_stream_event(stream, event);
  return QUIC_STATUS_SUCCESS;
}

void QuicConnection::handle_data_stream_event(HQUIC stream, QUIC_STREAM_EVENT* event)
{
  NPRPC_QUIC_DEBUG_LOG(
      std::format("Client data_stream_event type: {}", event->Type));
      
  switch (event->Type) {
  case QUIC_STREAM_EVENT_RECEIVE: {
    NPRPC_QUIC_DEBUG_LOG("Client DATA_STREAM_RECEIVE, buffers: {}",
                         event->RECEIVE.BufferCount);
    // Append data to this stream's receive buffer
    {
      std::lock_guard lock(data_streams_mutex_);
      auto it = data_streams_.find(stream);
      if (it != data_streams_.end()) {
        for (uint32_t i = 0; i < event->RECEIVE.BufferCount; ++i) {
          auto& buf = event->RECEIVE.Buffers[i];
          it->second.receive_buffer.insert(it->second.receive_buffer.end(), 
                                           buf.Buffer, buf.Buffer + buf.Length);
        }
      }
    }
    
    // Process complete messages
    process_data_stream_buffer(stream);
    break;
  }
  
  case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN:
  case QUIC_STREAM_EVENT_PEER_SEND_ABORTED:
    NPRPC_LOG_DEBUG("[QUIC] Client: Data stream peer send shutdown/aborted");
    break;
    
  case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE: {
    NPRPC_LOG_DEBUG("[QUIC] Client: Data stream shutdown complete");
    // Remove from map
    std::lock_guard lock(data_streams_mutex_);
    data_streams_.erase(stream);
    break;
  }
  
  default:
    break;
  }
}

void QuicConnection::process_data_stream_buffer(HQUIC stream)
{
  // NPRPC message format: first 4 bytes = payload size
  // Total message size = payload_size + 4
  // For data streams, each message has stream_id in the header
  
  std::lock_guard lock(data_streams_mutex_);
  auto it = data_streams_.find(stream);
  if (it == data_streams_.end()) {
    return;
  }
  
  auto& info = it->second;
  
  while (info.receive_buffer.size() >= 4) {
    uint32_t payload_size = *reinterpret_cast<uint32_t*>(info.receive_buffer.data());
    uint32_t total_size = payload_size + 4;
    
    NPRPC_QUIC_DEBUG_LOG("Client data stream: payload_size={}, "
                                     "total_size={}, buffer_size={}",
                                     payload_size, total_size,
                                     info.receive_buffer.size());
    
    if (info.receive_buffer.size() < total_size) {
      break; // Incomplete message
    }
    
    // Extract complete message
    std::vector<uint8_t> message(info.receive_buffer.begin(),
                                 info.receive_buffer.begin() + total_size);
    info.receive_buffer.erase(info.receive_buffer.begin(),
                              info.receive_buffer.begin() + total_size);
    
    // Extract stream_id from message if not yet known
    // StreamChunk format: Header (size + msg_id + msg_type + request_id) + stream_id(8) + ...
    // stream_id is at offset 4 (size) + 4 (msg_id) + 4 (msg_type) + 4 (request_id) = 16
    if (!info.stream_id_known && message.size() >= 24) {
      info.stream_id = *reinterpret_cast<uint64_t*>(message.data() + 16);
      info.stream_id_known = true;
      NPRPC_LOG_INFO("[QUIC] Client: Learned stream_id={} for data stream", info.stream_id);
    }
    
    // Dispatch to callback
    if (data_stream_callback_) {
      uint64_t stream_id = info.stream_id;
      NPRPC_LOG_DEBUG("[QUIC] Client: Dispatching data stream message, stream_id={}, size={}",
                     stream_id, message.size());
      boost::asio::post(ioc_, [this, self = shared_from_this(), 
                               stream_id, msg = std::move(message)]() mutable {
        DataStreamCallback cb;
        {
          std::lock_guard lk(data_streams_mutex_);
          cb = data_stream_callback_;
        }
        if (cb) {
          cb(stream_id, std::move(msg));
        }
      });
    }
  }
}

//==============================================================================
// QuicServerConnection - Server-side connection wrapper
//==============================================================================

QuicServerConnection::QuicServerConnection(boost::asio::io_context& ioc,
                                           HQUIC connection,
                                           HQUIC configuration)
    : ioc_(ioc)
    , connection_(connection)
    , configuration_(configuration)
{
  // Get remote address
  QUIC_ADDR addr;
  uint32_t addr_len = sizeof(addr);
  auto& quic = QuicApi::instance();

  if (QUIC_SUCCEEDED(quic.api()->GetParam(
          connection_, QUIC_PARAM_CONN_REMOTE_ADDRESS, &addr_len, &addr))) {
    char ip_str[INET6_ADDRSTRLEN];
    if (addr.Ip.sa_family == QUIC_ADDRESS_FAMILY_INET) {
      inet_ntop(AF_INET, &addr.Ipv4.sin_addr, ip_str, sizeof(ip_str));
      remote_port_ = ntohs(addr.Ipv4.sin_port);
    } else {
      inet_ntop(AF_INET6, &addr.Ipv6.sin6_addr, ip_str, sizeof(ip_str));
      remote_port_ = ntohs(addr.Ipv6.sin6_port);
    }
    remote_addr_ = ip_str;
  }
}

QuicServerConnection::~QuicServerConnection() { close(); }

void QuicServerConnection::start()
{
  auto& quic = QuicApi::instance();

  // Set connection callback
  quic.api()->SetCallbackHandler(
      connection_, reinterpret_cast<void*>(connection_callback), this);
}

void QuicServerConnection::set_message_callback(MessageCallback callback)
{
  std::lock_guard lock(mutex_);
  message_callback_ = std::move(callback);
}

void QuicServerConnection::set_datagram_callback(DatagramCallback callback)
{
  std::lock_guard lock(mutex_);
  datagram_callback_ = std::move(callback);
}

bool QuicServerConnection::send(const void* data, size_t len)
{
  if (!stream_) {
    return false;
  }

  auto& quic = QuicApi::instance();

  QUIC_BUFFER* send_buf = new QUIC_BUFFER;
  send_buf->Length = static_cast<uint32_t>(len);
  send_buf->Buffer = new uint8_t[len];
  std::memcpy(send_buf->Buffer, data, len);

  QUIC_STATUS status = quic.api()->StreamSend(stream_, send_buf, 1,
                                              QUIC_SEND_FLAG_NONE, send_buf);

  if (QUIC_FAILED(status)) {
    delete[] send_buf->Buffer;
    delete send_buf;
    return false;
  }

  return true;
}

void QuicServerConnection::close()
{
  auto& quic = QuicApi::instance();

  // Close all data streams first
  {
    std::lock_guard lock(data_streams_mutex_);
    for (auto& [stream_id, handle] : data_streams_) {
      quic.api()->StreamClose(handle);
    }
    data_streams_.clear();
  }

  if (stream_) {
    quic.api()->StreamClose(stream_);
    stream_ = nullptr;
  }

  if (connection_) {
    quic.api()->ConnectionClose(connection_);
    connection_ = nullptr;
  }
}

// Context struct for data stream callbacks - contains both connection and stream_id
struct DataStreamContext {
  QuicServerConnection* connection;
  uint64_t stream_id;
};

bool QuicServerConnection::open_data_stream(uint64_t stream_id)
{
  // Check if already open
  {
    std::lock_guard lock(data_streams_mutex_);
    if (data_streams_.find(stream_id) != data_streams_.end()) {
      NPRPC_LOG_DEBUG("[QUIC] Data stream already open for stream_id={}", stream_id);
      return true;  // Already open, success
    }
  }
  
  auto& quic = QuicApi::instance();
  
  HQUIC stream = nullptr;
  
  // Open a server-initiated unidirectional stream (server -> client only)
  QUIC_STATUS status = quic.api()->StreamOpen(
      connection_,
      QUIC_STREAM_OPEN_FLAG_UNIDIRECTIONAL,  // Server sends, client receives
      data_stream_callback,
      new DataStreamContext{this, stream_id},  // Context includes stream_id
      &stream);
      
  if (QUIC_FAILED(status)) {
    NPRPC_LOG_ERROR("[QUIC] Failed to open data stream: {}", status);
    return false;
  }
  
  // Start the stream
  status = quic.api()->StreamStart(stream, QUIC_STREAM_START_FLAG_NONE);
  if (QUIC_FAILED(status)) {
    NPRPC_LOG_ERROR("[QUIC] Failed to start data stream: {}", status);
    quic.api()->StreamClose(stream);
    return false;
  }
  
  // Store the mapping
  {
    std::lock_guard lock(data_streams_mutex_);
    data_streams_[stream_id] = stream;
  }
  
  NPRPC_LOG_INFO("[QUIC] Opened native data stream for stream_id={}", stream_id);
  return true;
}

bool QuicServerConnection::send_on_stream(uint64_t stream_id, const void* data, size_t len)
{
  HQUIC stream = nullptr;
  
  {
    std::lock_guard lock(data_streams_mutex_);
    auto it = data_streams_.find(stream_id);
    if (it == data_streams_.end()) {
      NPRPC_LOG_ERROR("[QUIC] send_on_stream: stream_id {} not found", stream_id);
      return false;
    }
    stream = it->second;
  }
  
  auto& quic = QuicApi::instance();
  
  QUIC_BUFFER* send_buf = new QUIC_BUFFER;
  send_buf->Length = static_cast<uint32_t>(len);
  send_buf->Buffer = new uint8_t[len];
  std::memcpy(send_buf->Buffer, data, len);
  
  QUIC_STATUS status = quic.api()->StreamSend(
      stream, send_buf, 1, QUIC_SEND_FLAG_NONE, send_buf);
      
  if (QUIC_FAILED(status)) {
    NPRPC_LOG_ERROR("[QUIC] Failed to send on data stream: {}", status);
    delete[] send_buf->Buffer;
    delete send_buf;
    return false;
  }
  
  NPRPC_LOG_DEBUG("[QUIC] Sent {} bytes on native stream_id={}", len, stream_id);
  return true;
}

void QuicServerConnection::close_data_stream(uint64_t stream_id)
{
  auto& quic = QuicApi::instance();
  
  HQUIC stream = nullptr;
  {
    std::lock_guard lock(data_streams_mutex_);
    auto it = data_streams_.find(stream_id);
    if (it != data_streams_.end()) {
      stream = it->second;
      data_streams_.erase(it);
    }
  }
  
  if (stream) {
    // Send FIN to close the stream gracefully
    quic.api()->StreamShutdown(stream, QUIC_STREAM_SHUTDOWN_FLAG_GRACEFUL, 0);
    quic.api()->StreamClose(stream);
    NPRPC_LOG_INFO("[QUIC] Closed native data stream for stream_id={}", stream_id);
  }
}

bool QuicServerConnection::send_datagram(const uint8_t* data, size_t len)
{
  NPRPC_QUIC_DEBUG_LOG(std::format(
      "QuicServerConnection::send_datagram called, len={}, datagram_enabled={}", len,
      datagram_send_enabled_.load()));

  if (!connection_) {
    NPRPC_QUIC_DEBUG_LOG("send_datagram: no connection!");
    return false;
  }

  if (!datagram_send_enabled_) {
    NPRPC_QUIC_DEBUG_LOG("send_datagram: datagrams not enabled by peer!");
    return false;
  }

  if (len > max_datagram_size_) {
    NPRPC_QUIC_DEBUG_LOG(std::format("send_datagram: size {} exceeds max {}",
                                     len, max_datagram_size_));
    return false;
  }

  auto& quic = QuicApi::instance();

  // Allocate buffer that will be freed on DATAGRAM_SEND_STATE_CHANGED event
  QUIC_BUFFER* send_buf = new QUIC_BUFFER;
  send_buf->Length = static_cast<uint32_t>(len);
  send_buf->Buffer = new uint8_t[len];
  std::memcpy(send_buf->Buffer, data, len);

  QUIC_STATUS status =
      quic.api()->DatagramSend(connection_, send_buf, 1, QUIC_SEND_FLAG_NONE,
                               send_buf // Context for cleanup
      );

  if (QUIC_FAILED(status)) {
    NPRPC_QUIC_DEBUG_LOG(std::format("DatagramSend failed, status={}", status));
    delete[] send_buf->Buffer;
    delete send_buf;
    return false;
  }

  NPRPC_QUIC_DEBUG_LOG("Server DatagramSend succeeded");
  return true;
}

QUIC_STATUS QUIC_API QuicServerConnection::data_stream_callback(
    HQUIC stream, void* context, QUIC_STREAM_EVENT* event)
{
  auto* ctx = static_cast<DataStreamContext*>(context);
  ctx->connection->handle_data_stream_event(stream, ctx->stream_id, event);
  
  // Clean up context on stream shutdown
  if (event->Type == QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE) {
    delete ctx;
  }
  
  return QUIC_STATUS_SUCCESS;
}

void QuicServerConnection::handle_data_stream_event(
    HQUIC stream, uint64_t stream_id, QUIC_STREAM_EVENT* event)
{
  switch (event->Type) {
  case QUIC_STREAM_EVENT_SEND_COMPLETE: {
    // Clean up send buffer
    auto* send_buf = static_cast<QUIC_BUFFER*>(event->SEND_COMPLETE.ClientContext);
    if (send_buf) {
      delete[] send_buf->Buffer;
      delete send_buf;
    }
    break;
  }
  
  case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
    NPRPC_LOG_DEBUG("[QUIC] Data stream shutdown complete for stream_id={}", stream_id);
    break;
    
  default:
    break;
  }
}

QUIC_STATUS QUIC_API QuicServerConnection::connection_callback(
    HQUIC connection, void* context, QUIC_CONNECTION_EVENT* event)
{
  auto* self = static_cast<QuicServerConnection*>(context);
  self->handle_connection_event(event);
  return QUIC_STATUS_SUCCESS;
}

void QuicServerConnection::handle_connection_event(QUIC_CONNECTION_EVENT* event)
{
  NPRPC_QUIC_DEBUG_LOG(
      std::format("Server connection_event type: {}", event->Type));
  auto& quic = QuicApi::instance();

  switch (event->Type) {
  case QUIC_CONNECTION_EVENT_CONNECTED:
    NPRPC_QUIC_DEBUG_LOG("Server CONNECTED");
    NPRPC_LOG_INFO("[QUIC] Server connection established from {}:{}",
                   remote_addr_, remote_port_);
    break;

  case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED: {
    NPRPC_QUIC_DEBUG_LOG("Server PEER_STREAM_STARTED");
    // Accept the incoming stream
    stream_ = event->PEER_STREAM_STARTED.Stream;
    quic.api()->SetCallbackHandler(
        stream_, reinterpret_cast<void*>(stream_callback), this);

    NPRPC_LOG_INFO("[QUIC] Stream started from peer");
    break;
  }

  case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
  case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
  case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
    break;

  case QUIC_CONNECTION_EVENT_DATAGRAM_RECEIVED: {
    NPRPC_QUIC_DEBUG_LOG("Server DATAGRAM_RECEIVED!");
    // Handle unreliable datagram - use datagram_callback_ (no response
    // expected)
    std::lock_guard lock(mutex_);
    if (datagram_callback_) {
      auto& buf = event->DATAGRAM_RECEIVED.Buffer;
      NPRPC_QUIC_DEBUG_LOG(std::format("Server datagram size={}", buf->Length));
      std::vector<uint8_t> data(buf->Buffer, buf->Buffer + buf->Length);
      boost::asio::post(ioc_, [this, self = shared_from_this(),
                               data = std::move(data)]() mutable {
        datagram_callback_(std::move(data));
      });
    } else {
      NPRPC_QUIC_DEBUG_LOG("Server datagram_callback_ is null!");
    }
    break;
  }

  case QUIC_CONNECTION_EVENT_DATAGRAM_STATE_CHANGED: {
    // Peer has indicated whether they support receiving datagrams
    datagram_send_enabled_ = event->DATAGRAM_STATE_CHANGED.SendEnabled;
    max_datagram_size_ = event->DATAGRAM_STATE_CHANGED.MaxSendLength;
    NPRPC_QUIC_DEBUG_LOG(
        "Server DATAGRAM_STATE_CHANGED: SendEnabled={}, MaxSendLength={}",
                    (int)datagram_send_enabled_.load(), max_datagram_size_);
    break;
  }

  case QUIC_CONNECTION_EVENT_DATAGRAM_SEND_STATE_CHANGED: {
    // Clean up datagram send buffer only when send is complete
    auto state = event->DATAGRAM_SEND_STATE_CHANGED.State;
    NPRPC_QUIC_DEBUG_LOG(
        std::format("Server DATAGRAM_SEND_STATE_CHANGED: state={}", (int)state));

    // Only free on terminal states
    if (state == QUIC_DATAGRAM_SEND_ACKNOWLEDGED ||
        state == QUIC_DATAGRAM_SEND_ACKNOWLEDGED_SPURIOUS ||
        state == QUIC_DATAGRAM_SEND_LOST_DISCARDED ||
        state == QUIC_DATAGRAM_SEND_CANCELED) {
      auto* send_buf = static_cast<QUIC_BUFFER*>(
          event->DATAGRAM_SEND_STATE_CHANGED.ClientContext);
      if (send_buf && send_buf->Buffer) {
        delete[] send_buf->Buffer;
        delete send_buf;
      }
    }
    break;
  }

  default:
    break;
  }
}

QUIC_STATUS QUIC_API QuicServerConnection::stream_callback(
    HQUIC stream, void* context, QUIC_STREAM_EVENT* event)
{
  auto* self = static_cast<QuicServerConnection*>(context);
  self->handle_stream_event(stream, event);
  return QUIC_STATUS_SUCCESS;
}

void QuicServerConnection::handle_stream_event(HQUIC stream,
                                               QUIC_STREAM_EVENT* event)
{
  NPRPC_QUIC_DEBUG_LOG(
      std::format("Server stream_event type: {}", event->Type));
  switch (event->Type) {
  case QUIC_STREAM_EVENT_RECEIVE: {
    NPRPC_QUIC_DEBUG_LOG(std::format("Server STREAM_RECEIVE, buffers: {}",
                                     event->RECEIVE.BufferCount));
    // Append data to receive buffer
    for (uint32_t i = 0; i < event->RECEIVE.BufferCount; ++i) {
      auto& buf = event->RECEIVE.Buffers[i];
      NPRPC_QUIC_DEBUG_LOG(std::format("Server received {} bytes", buf.Length));
      receive_buffer_.insert(receive_buffer_.end(), buf.Buffer,
                             buf.Buffer + buf.Length);
    }

    // Process complete messages
    process_receive_buffer();
    break;
  }

  case QUIC_STREAM_EVENT_SEND_COMPLETE: {
    auto* send_buf =
        static_cast<QUIC_BUFFER*>(event->SEND_COMPLETE.ClientContext);
    if (send_buf) {
      delete[] send_buf->Buffer;
      delete send_buf;
    }
    break;
  }

  default:
    break;
  }
}

void QuicServerConnection::process_receive_buffer()
{
  // NPRPC message format: first 4 bytes = payload size (not including the
  // 4-byte size field) Total message size = payload_size + 4
  while (receive_buffer_.size() >= 4) {
    uint32_t payload_size =
        *reinterpret_cast<uint32_t*>(receive_buffer_.data());
    uint32_t total_size = payload_size + 4;
    NPRPC_QUIC_DEBUG_LOG(std::format("process_receive_buffer: payload_size={}, "
                                     "total_size={}, buffer_size={}",
                                     payload_size, total_size,
                                     receive_buffer_.size()));

    if (receive_buffer_.size() < total_size) {
      break; // Incomplete message
    }

    // Extract complete message
    std::vector<uint8_t> message(receive_buffer_.begin(),
                                 receive_buffer_.begin() + total_size);
    receive_buffer_.erase(receive_buffer_.begin(),
                          receive_buffer_.begin() + total_size);

    // Dispatch to callback
    std::lock_guard lock(mutex_);
    if (message_callback_) {
      NPRPC_QUIC_DEBUG_LOG(
          std::format("posting message to callback, size={}", message.size()));
      boost::asio::post(ioc_, [this, self = shared_from_this(),
                               msg = std::move(message)]() mutable {
        NPRPC_QUIC_DEBUG_LOG("executing message callback");
        message_callback_(std::move(msg));
      });
    } else {
      NPRPC_QUIC_DEBUG_LOG("no message_callback_ set!");
    }
  }
}

//==============================================================================
// QuicListener - Server listener
//==============================================================================

QuicListener::QuicListener(boost::asio::io_context& ioc,
                           const std::string& cert_file,
                           const std::string& key_file)
    : ioc_(ioc)
    , cert_file_(cert_file)
    , key_file_(key_file)
{
}

QuicListener::~QuicListener() { stop(); }

void QuicListener::start(uint16_t port, AcceptCallback callback)
{
  auto& quic = QuicApi::instance();

  accept_callback_ = std::move(callback);

  // Create server configuration with certificate
  configuration_ = quic.create_configuration("nprpc", true, cert_file_.c_str(),
                                             key_file_.c_str());

  if (!configuration_) {
    throw std::runtime_error("Failed to create QUIC server configuration");
  }

  // Open listener
  QUIC_STATUS status = quic.api()->ListenerOpen(
      quic.registration(), listener_callback, this, &listener_);

  if (QUIC_FAILED(status)) {
    quic.api()->ConfigurationClose(configuration_);
    configuration_ = nullptr;
    throw std::runtime_error("ListenerOpen failed: " + std::to_string(status));
  }

  // Create address to listen on
  QUIC_ADDR addr = {};
  QuicAddrSetFamily(&addr, QUIC_ADDRESS_FAMILY_UNSPEC);
  QuicAddrSetPort(&addr, port);

  QUIC_BUFFER alpn = alpn_buffer;

  status = quic.api()->ListenerStart(listener_, &alpn, 1, &addr);

  if (QUIC_FAILED(status)) {
    quic.api()->ListenerClose(listener_);
    listener_ = nullptr;
    quic.api()->ConfigurationClose(configuration_);
    configuration_ = nullptr;
    throw std::runtime_error("ListenerStart failed: " + std::to_string(status));
  }

  NPRPC_LOG_INFO("[QUIC] Listener started on port {}", port);
}

void QuicListener::stop()
{
  // First, close all tracked connections to break callback cycles
  std::vector<std::shared_ptr<QuicServerConnection>> connections_to_close;
  {
    std::lock_guard lock(connections_mutex_);
    connections_to_close = std::move(connections_);
    connections_.clear();
  }

  for (auto& conn : connections_to_close) {
    conn->set_message_callback(nullptr);
    conn->set_datagram_callback(nullptr);
    conn->close();
  }
  connections_to_close.clear();

  auto& quic = QuicApi::instance();

  if (listener_) {
    quic.api()->ListenerClose(listener_);
    listener_ = nullptr;
  }

  if (configuration_) {
    quic.api()->ConfigurationClose(configuration_);
    configuration_ = nullptr;
  }
}

void QuicListener::add_connection(std::shared_ptr<QuicServerConnection> conn)
{
  std::lock_guard lock(connections_mutex_);
  connections_.push_back(std::move(conn));
}

void QuicListener::remove_connection(QuicServerConnection* conn)
{
  std::lock_guard lock(connections_mutex_);
  connections_.erase(
      std::remove_if(connections_.begin(), connections_.end(),
                     [conn](const std::shared_ptr<QuicServerConnection>& c) {
                       return c.get() == conn;
                     }),
      connections_.end());
}

QUIC_STATUS QUIC_API QuicListener::listener_callback(HQUIC listener,
                                                     void* context,
                                                     QUIC_LISTENER_EVENT* event)
{
  auto* self = static_cast<QuicListener*>(context);
  self->handle_listener_event(event);
  return QUIC_STATUS_SUCCESS;
}

void QuicListener::handle_listener_event(QUIC_LISTENER_EVENT* event)
{
  switch (event->Type) {
  case QUIC_LISTENER_EVENT_NEW_CONNECTION: {
    NPRPC_LOG_INFO("[QUIC] New connection from client");

    auto& quic = QuicApi::instance();

    // Set configuration on the connection
    QUIC_STATUS status = quic.api()->ConnectionSetConfiguration(
        event->NEW_CONNECTION.Connection, configuration_);

    if (QUIC_FAILED(status)) {
      NPRPC_LOG_ERROR("[QUIC] Failed to set connection configuration");
      return;
    }

    // Create server connection wrapper
    auto conn = std::make_shared<QuicServerConnection>(
        ioc_, event->NEW_CONNECTION.Connection, configuration_);
    conn->start();

    // Track connection for cleanup
    add_connection(conn);

    if (accept_callback_) {
      accept_callback_(conn);
    }
    break;
  }

  case QUIC_LISTENER_EVENT_STOP_COMPLETE:
    NPRPC_LOG_INFO("[QUIC] Listener stopped");
    break;

  default:
    break;
  }
}

//==============================================================================
// QuicServerSession - RPC session over QUIC
//==============================================================================

class QuicServerSession : public Session,
                          public std::enable_shared_from_this<QuicServerSession>
{
  std::shared_ptr<QuicServerConnection> connection_;

  flat_buffer rx_buffer_{flat_buffer::default_initial_size()};
  flat_buffer tx_buffer_{flat_buffer::default_initial_size()};

public:
  virtual void timeout_action() final
  {
    // Server sessions don't have timeouts
  }

  virtual void shutdown() override
  {
    if (connection_) {
      connection_->set_message_callback(nullptr);
      connection_->set_datagram_callback(nullptr);
      connection_->close();
    }
    Session::shutdown();
  }

  virtual void send_receive(flat_buffer&, uint32_t) override
  {
    NPRPC_LOG_ERROR("QuicServerSession: send_receive called unexpectedly");
    assert(false && "send_receive should not be called on server session");
  }

  virtual void send_receive_async(
      flat_buffer&&,
      std::optional<std::function<void(const boost::system::error_code&,
                                       flat_buffer&)>>&&,
      uint32_t) override
  {
    NPRPC_LOG_ERROR("QuicServerSession: send_receive_async called unexpectedly");
    assert(false && "send_receive_async should not be called on server session");
  }

  // Override for streaming - send on native QUIC stream
  // Uses dedicated QUIC streams per NPRPC stream for zero head-of-line blocking
  virtual void send_stream_message(flat_buffer&& buffer) override
  {
    if (!connection_) {
      NPRPC_LOG_ERROR("QuicServerSession: No connection for stream message");
      return;
    }
    
    auto data = buffer.cdata();
    
    // Extract stream_id from the message
    // Format: size(4) + Header(msg_id(4) + msg_type(4) + request_id(4)) + stream_id(8)
    // stream_id is at offset 16
    if (data.size() < 24) {
      NPRPC_LOG_ERROR("QuicServerSession: Stream message too small to contain stream_id");
      return;
    }
    
    uint64_t stream_id = *reinterpret_cast<const uint64_t*>(
        static_cast<const uint8_t*>(data.data()) + 16);
    
    NPRPC_LOG_INFO("QuicServerSession::send_stream_message: stream_id={}, size={}", 
                   stream_id, data.size());
    
    // Open native QUIC stream if not already open
    if (!connection_->open_data_stream(stream_id)) {
      // Already open or failed - try to send anyway (might already exist)
      NPRPC_LOG_DEBUG("QuicServerSession: Data stream may already exist for stream_id={}", stream_id);
    }
    
    // Send on native QUIC stream
    if (!connection_->send_on_stream(stream_id, data.data(), data.size())) {
      NPRPC_LOG_ERROR("QuicServerSession: Failed to send on native stream for stream_id={}", stream_id);
      // Fallback to main stream (for compatibility)
      if (!connection_->send(data.data(), data.size())) {
        NPRPC_LOG_ERROR("QuicServerSession: Fallback send also failed");
      }
    } else {
      NPRPC_LOG_INFO("QuicServerSession: Sent {} bytes on native QUIC stream_id={}", 
                     data.size(), stream_id);
    }
  }

  // Override for control messages - send on main QUIC stream (always reliable)
  virtual void send_main_stream_message(flat_buffer&& buffer) override
  {
    if (!connection_) {
      NPRPC_LOG_ERROR("QuicServerSession: No connection for main stream message");
      return;
    }
    
    auto data = buffer.cdata();
    NPRPC_LOG_INFO("QuicServerSession::send_main_stream_message: size={}", data.size());
    
    if (!connection_->send(data.data(), data.size())) {
      NPRPC_LOG_ERROR("QuicServerSession: Failed to send on main stream");
    }
  }

  void on_message_received(std::vector<uint8_t>&& data)
  {
    NPRPC_QUIC_DEBUG_LOG(
        std::format("on_message_received called, data.size={}", data.size()));
    try {
      // Move data into rx_buffer
      rx_buffer_.consume(rx_buffer_.size());
      auto mb = rx_buffer_.prepare(data.size());
      std::memcpy(mb.data(), data.data(), data.size());
      rx_buffer_.commit(data.size());

      // Dispatch the RPC request
      NPRPC_QUIC_DEBUG_LOG("calling handle_request");
      bool needs_reply = handle_request(rx_buffer_, tx_buffer_);
      NPRPC_QUIC_DEBUG_LOG("handle_request returned, needs_reply={}", needs_reply);

      // Send response back only if needed
      if (needs_reply) {
        auto response_data = tx_buffer_.cdata();
        NPRPC_QUIC_DEBUG_LOG(
            std::format("sending response, size={}", response_data.size()));
        if (!connection_->send(response_data.data(), response_data.size())) {
          NPRPC_LOG_ERROR("QuicServerSession: Failed to send response");
        } else {
          NPRPC_QUIC_DEBUG_LOG("response sent successfully");
        }
      }
    } catch (const std::exception& e) {
      NPRPC_LOG_ERROR("QuicServerSession: Error processing message: {}",
                      e.what());
    }
  }

  /**
   * @brief Handle unreliable datagram message (no response expected)
   */
  void on_datagram_received(std::vector<uint8_t>&& data)
  {
    try {
      // Move data into rx_buffer
      rx_buffer_.consume(rx_buffer_.size());
      auto mb = rx_buffer_.prepare(data.size());
      std::memcpy(mb.data(), data.data(), data.size());
      rx_buffer_.commit(data.size());

      // Dispatch the RPC request (fire-and-forget, no response)
      handle_request(rx_buffer_, tx_buffer_);

      // No response sent for datagram messages
      NPRPC_LOG_INFO("QuicServerSession: Processed datagram (no response)");
    } catch (const std::exception& e) {
      NPRPC_LOG_ERROR("QuicServerSession: Error processing datagram: {}",
                      e.what());
    }
  }

  QuicServerSession(boost::asio::io_context& ioc,
                    std::shared_ptr<QuicServerConnection> connection)
      : Session(ioc.get_executor())
      , connection_(std::move(connection))
  {
    ctx_.remote_endpoint =
        EndPoint(EndPointType::Quic, connection_->remote_address(),
                 connection_->remote_port());

    NPRPC_LOG_INFO("QuicServerSession created for {}:{}",
                   connection_->remote_address(), connection_->remote_port());
  }

  void start()
  {
    connection_->set_message_callback(
        [this, self = shared_from_this()](std::vector<uint8_t>&& data) {
          on_message_received(std::move(data));
        });
    connection_->set_datagram_callback(
        [this, self = shared_from_this()](std::vector<uint8_t>&& data) {
          on_datagram_received(std::move(data));
        });
    
    // Set up datagram callback for unreliable stream chunks
    ctx_.stream_manager->set_send_datagram_callback(
        [this, self = shared_from_this()](flat_buffer&& buffer) -> bool {
          auto data = buffer.cdata();
          return connection_->send_datagram(
              reinterpret_cast<const uint8_t*>(data.data()), data.size());
        });
  }

  ~QuicServerSession() { NPRPC_LOG_INFO("QuicServerSession destroyed"); }
};

//==============================================================================
// QuicClientSession - Client-side RPC session over QUIC
//==============================================================================

class QuicClientSession : public Session,
                          public std::enable_shared_from_this<QuicClientSession>
{
  std::shared_ptr<QuicConnection> connection_;
  EndPoint endpoint_;
  bool connected_ = false;
  flat_buffer rx_buffer_;
  flat_buffer tx_buffer_;

  void setup_message_callback()
  {
    // Set up message callback for receiving stream messages on main RPC stream
    connection_->set_message_callback(
        [this, self = shared_from_this()](std::vector<uint8_t>&& data) {
          on_message_received(std::move(data));
        });
    
    // Set up callback for data received on native QUIC streams (for reliable streaming)
    connection_->set_data_stream_callback(
        [this, self = shared_from_this()](uint64_t stream_id, std::vector<uint8_t>&& data) {
          on_data_stream_received(stream_id, std::move(data));
        });
    
    // Set up callback for datagrams (for unreliable streaming)
    connection_->set_datagram_callback(
        [this, self = shared_from_this()](std::vector<uint8_t>&& data) {
          on_datagram_received(std::move(data));
        });
  }

  void on_message_received(std::vector<uint8_t>&& data)
  {
    NPRPC_LOG_INFO("QuicClientSession::on_message_received (main stream), size={}", data.size());
    
    // Copy to flat_buffer for handle_request
    rx_buffer_.consume(rx_buffer_.size());
    auto mb = rx_buffer_.prepare(data.size());
    std::memcpy(mb.data(), data.data(), data.size());
    rx_buffer_.commit(data.size());

    // handle_request dispatches to StreamManager for stream messages
    handle_request(rx_buffer_, tx_buffer_);
    
    // Note: Stream messages are fire-and-forget, no response needed
    // The handle_request will return false for stream messages and 
    // dispatch chunks to StreamManager automatically
  }

  void on_data_stream_received(uint64_t stream_id, std::vector<uint8_t>&& data)
  {
    NPRPC_LOG_INFO("QuicClientSession::on_data_stream_received (native stream), "
                   "stream_id={}, size={}", stream_id, data.size());
    
    // Copy to flat_buffer for handle_request
    rx_buffer_.consume(rx_buffer_.size());
    auto mb = rx_buffer_.prepare(data.size());
    std::memcpy(mb.data(), data.data(), data.size());
    rx_buffer_.commit(data.size());

    // handle_request dispatches to StreamManager for stream chunk messages
    handle_request(rx_buffer_, tx_buffer_);
  }

  void on_datagram_received(std::vector<uint8_t>&& data)
  {
    NPRPC_LOG_INFO("QuicClientSession::on_datagram_received (datagram), size={}", data.size());
    
    // Copy to flat_buffer for handle_request
    rx_buffer_.consume(rx_buffer_.size());
    auto mb = rx_buffer_.prepare(data.size());
    std::memcpy(mb.data(), data.data(), data.size());
    rx_buffer_.commit(data.size());

    // handle_request dispatches to StreamManager for stream chunk messages
    handle_request(rx_buffer_, tx_buffer_);
  }

public:
  virtual void timeout_action() final
  {
    // Cancel pending operations
  }

  virtual void shutdown() override
  {
    // Clear callbacks to break shared_ptr cycle
    if (connection_) {
      connection_->set_message_callback(nullptr);
      connection_->set_data_stream_callback(nullptr);
      connection_->set_datagram_callback(nullptr);
      connection_->close();
    }
    connected_ = false;
    Session::shutdown();
  }

  virtual void send_receive(flat_buffer& buffer, uint32_t timeout_ms) override
  {
    if (!connected_) {
      // Connect synchronously on first call
      std::promise<bool> promise;
      auto future = promise.get_future();

      connection_->async_connect(
          std::string(endpoint_.hostname()), endpoint_.port(),
          [&promise](bool success) { promise.set_value(success); });

      // Run io_context until connection completes
      // Note: This is a simplified blocking connect
      // In production, we'd want proper async handling
      if (!future.get()) {
        throw nprpc::ExceptionCommFailure("QUIC connection failed");
      }
      connected_ = true;
      
      // Set up message callback for receiving stream messages after connect
      setup_message_callback();
    }

    connection_->send_receive(buffer, timeout_ms);
  }

  virtual void send_receive_async(
      flat_buffer&& buffer,
      std::optional<std::function<void(const boost::system::error_code&,
                                       flat_buffer&)>>&& handler,
      uint32_t timeout_ms) override
  {
    // For now, use blocking version
    try {
      send_receive(buffer, timeout_ms);
      if (handler) {
        (*handler)(boost::system::error_code{}, buffer);
      }
    } catch (const nprpc::ExceptionTimeout&) {
      if (handler) {
        (*handler)(boost::asio::error::timed_out, buffer);
      }
    } catch (...) {
      if (handler) {
        (*handler)(boost::asio::error::connection_aborted, buffer);
      }
    }
  }

  /**
   * @brief Send stream message (fire-and-forget)
   * Override Session's default to use the QUIC connection directly
   */
  virtual void send_stream_message(flat_buffer&& buffer) override
  {
    NPRPC_LOG_INFO("QuicClientSession::send_stream_message called, size={}", buffer.size());
    if (connection_ && connected_) {
      auto data = buffer.cdata();
      if (!connection_->send(data.data(), data.size())) {
        NPRPC_LOG_ERROR("QuicClientSession: Failed to send stream message");
      } else {
        NPRPC_LOG_INFO("QuicClientSession: Stream message sent successfully");
      }
    }
  }

  /**
   * @brief Send unreliable datagram via QUIC DATAGRAM extension
   */
  virtual bool send_datagram(flat_buffer&& buffer) override
  {
    if (!connected_) {
      // Need to connect first
      std::promise<bool> promise;
      auto future = promise.get_future();

      connection_->async_connect(
          std::string(endpoint_.hostname()), endpoint_.port(),
          [&promise](bool success) { promise.set_value(success); });

      if (!future.get()) {
        return false;
      }
      connected_ = true;
    }

    auto data = buffer.cdata();
    return connection_->send_datagram(
        reinterpret_cast<const uint8_t*>(data.data()), data.size());
  }

  const EndPoint& remote_endpoint() const noexcept { return endpoint_; }

  QuicClientSession(const EndPoint& endpoint, boost::asio::io_context& ioc)
      : Session(ioc.get_executor())
      , connection_(std::make_shared<QuicConnection>(ioc))
      , endpoint_(endpoint)
  {
    ctx_.remote_endpoint = endpoint;
  }
};

//==============================================================================
// Global functions
//==============================================================================

NPRPC_API void init_quic(boost::asio::io_context& ioc)
{
  if (g_cfg.listen_quic_port == 0)
    return;

  try {
    g_quic_listener = std::make_shared<QuicListener>(ioc, g_cfg.quic_cert_file,
                                                     g_cfg.quic_key_file);

    g_quic_listener->start(g_cfg.listen_quic_port,
                           [&ioc](std::shared_ptr<QuicServerConnection> conn) {
                             auto session =
                                 std::make_shared<QuicServerSession>(ioc, conn);
                             session->start();
                           });
  } catch (const std::exception& e) {
    NPRPC_LOG_ERROR("[QUIC] Failed to start listener: {}", e.what());
  }
}

NPRPC_API void stop_quic_listener()
{
  if (g_quic_listener) {
    g_quic_listener->stop();
    g_quic_listener.reset();
  }
}

NPRPC_API std::shared_ptr<Session>
make_quic_client_session(const EndPoint& endpoint, boost::asio::io_context& ioc)
{
  return std::make_shared<QuicClientSession>(endpoint, ioc);
}

} // namespace nprpc::impl

#endif // NPRPC_QUIC_ENABLED

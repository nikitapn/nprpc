// Copyright (c) 2025 nikitapnn1@gmail.com
// SPDX-License-Identifier: MIT

#include <nprpc/impl/quic_transport.hpp>

#ifdef NPRPC_QUIC_ENABLED

#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/impl/session.hpp>
#include <nprpc/common.hpp>
#include <iostream>
#include <cstring>
#include <chrono>

namespace nprpc::impl {

// ALPN for NPRPC over QUIC
static const QUIC_BUFFER alpn_buffer = {
  sizeof("nprpc") - 1,
  (uint8_t*)"nprpc"
};

// Global QUIC listener instance
static std::shared_ptr<QuicListener> g_quic_listener;

//==============================================================================
// QuicApi - Singleton for MsQuic initialization
//==============================================================================

QuicApi& QuicApi::instance() {
  static QuicApi instance;
  return instance;
}

QuicApi::QuicApi() {
  // Open the MsQuic library
  QUIC_STATUS status = MsQuicOpen2(&api_);
  if (QUIC_FAILED(status)) {
    throw std::runtime_error("MsQuicOpen2 failed: " + std::to_string(status));
  }
  
  // Create registration
  QUIC_REGISTRATION_CONFIG reg_config = {
    "nprpc",
    QUIC_EXECUTION_PROFILE_LOW_LATENCY
  };
  
  status = api_->RegistrationOpen(&reg_config, &registration_);
  if (QUIC_FAILED(status)) {
    MsQuicClose(api_);
    throw std::runtime_error("RegistrationOpen failed: " + std::to_string(status));
  }
  
  if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
    std::cout << "[QUIC] MsQuic initialized successfully" << std::endl;
  }
}

QuicApi::~QuicApi() {
  if (registration_) {
    api_->RegistrationClose(registration_);
  }
  if (api_) {
    MsQuicClose(api_);
  }
}

HQUIC QuicApi::create_configuration(
  const char* alpn,
  bool is_server,
  const char* cert_file,
  const char* key_file
) {
  QUIC_BUFFER alpn_buf = {
    (uint32_t)strlen(alpn),
    (uint8_t*)alpn
  };
  
  // Settings for the configuration
  QUIC_SETTINGS settings = {};
  settings.IdleTimeoutMs = 30000;  // 30 second idle timeout
  settings.IsSet.IdleTimeoutMs = TRUE;
  settings.PeerBidiStreamCount = 100;  // Allow 100 bidirectional streams
  settings.IsSet.PeerBidiStreamCount = TRUE;
  settings.PeerUnidiStreamCount = 100;  // Allow 100 unidirectional streams
  settings.IsSet.PeerUnidiStreamCount = TRUE;
  settings.DatagramReceiveEnabled = TRUE;  // Enable QUIC datagrams for unreliable
  settings.IsSet.DatagramReceiveEnabled = TRUE;
  
  HQUIC configuration = nullptr;
  QUIC_STATUS status = api_->ConfigurationOpen(
    registration_,
    &alpn_buf,
    1,
    &settings,
    sizeof(settings),
    nullptr,
    &configuration
  );
  
  if (QUIC_FAILED(status)) {
    std::cerr << "[QUIC] ConfigurationOpen failed: " << status << std::endl;
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
    cred_config.Flags |= QUIC_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION;  // TODO: remove in production
    
    status = api_->ConfigurationLoadCredential(configuration, &cred_config);
  }
  
  if (QUIC_FAILED(status)) {
    std::cerr << "[QUIC] ConfigurationLoadCredential failed: " << status << std::endl;
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

QuicConnection::~QuicConnection() {
  close();
}

void QuicConnection::async_connect(
  const std::string& host,
  uint16_t port,
  ConnectCallback callback
) {
  auto& quic = QuicApi::instance();
  
  // Create client configuration
  configuration_ = quic.create_configuration("nprpc", false);
  if (!configuration_) {
    boost::asio::post(ioc_, [callback]() { callback(false); });
    return;
  }
  
  connect_callback_ = std::move(callback);
  
  // Create connection
  QUIC_STATUS status = quic.api()->ConnectionOpen(
    quic.registration(),
    connection_callback,
    this,
    &connection_
  );
  
  if (QUIC_FAILED(status)) {
    std::cerr << "[QUIC] ConnectionOpen failed: " << status << std::endl;
    quic.api()->ConfigurationClose(configuration_);
    configuration_ = nullptr;
    boost::asio::post(ioc_, [callback = connect_callback_]() { callback(false); });
    return;
  }
  
  // Start connection
  status = quic.api()->ConnectionStart(
    connection_,
    configuration_,
    QUIC_ADDRESS_FAMILY_UNSPEC,
    host.c_str(),
    port
  );
  
  if (QUIC_FAILED(status)) {
    std::cerr << "[QUIC] ConnectionStart failed: " << status << std::endl;
    quic.api()->ConnectionClose(connection_);
    connection_ = nullptr;
    quic.api()->ConfigurationClose(configuration_);
    configuration_ = nullptr;
    boost::asio::post(ioc_, [callback = connect_callback_]() { callback(false); });
  }
}

void QuicConnection::send_receive(flat_buffer& buffer, uint32_t timeout_ms) {
  if (!connected_ || !stream_) {
    throw nprpc::ExceptionCommFailure("QUIC connection not established");
  }
  
  auto& quic = QuicApi::instance();
  
  // Prepare for receive
  {
    std::lock_guard lock(mutex_);
    pending_receive_.clear();
    receive_complete_ = false;
  }
  
  // Set up receive callback to notify us
  set_receive_callback([this](const uint8_t* data, size_t len) {
    std::lock_guard lock(mutex_);
    pending_receive_.insert(pending_receive_.end(), data, data + len);
    
    // Check if we have a complete message (first 4 bytes = size)
    if (pending_receive_.size() >= 4) {
      uint32_t msg_size = *reinterpret_cast<uint32_t*>(pending_receive_.data());
      if (pending_receive_.size() >= msg_size) {
        receive_complete_ = true;
        cv_.notify_one();
      }
    }
  });
  
  // Send the request
  auto data = buffer.cdata();
  
  QUIC_BUFFER* send_buf = new QUIC_BUFFER;
  send_buf->Length = static_cast<uint32_t>(data.size());
  send_buf->Buffer = new uint8_t[data.size()];
  std::memcpy(send_buf->Buffer, data.data(), data.size());
  
  QUIC_STATUS status = quic.api()->StreamSend(
    stream_,
    send_buf,
    1,
    QUIC_SEND_FLAG_NONE,
    send_buf
  );
  
  if (QUIC_FAILED(status)) {
    delete[] send_buf->Buffer;
    delete send_buf;
    throw nprpc::ExceptionCommFailure("QUIC StreamSend failed");
  }
  
  // Wait for response with timeout
  std::unique_lock lock(mutex_);
  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  
  if (!cv_.wait_until(lock, deadline, [this] { return receive_complete_; })) {
    throw nprpc::ExceptionTimeout();
  }
  
  // Copy response to buffer
  buffer.consume(buffer.size());
  auto mb = buffer.prepare(pending_receive_.size());
  std::memcpy(mb.data(), pending_receive_.data(), pending_receive_.size());
  buffer.commit(pending_receive_.size());
}

void QuicConnection::async_send_stream(
  const uint8_t* data,
  size_t len,
  std::function<void(bool)> callback
) {
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
  
  QUIC_STATUS status = quic.api()->StreamSend(
    stream_,
    send_buf,
    1,
    QUIC_SEND_FLAG_NONE,
    send_buf  // Context for completion
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

bool QuicConnection::send_datagram(const uint8_t* data, size_t len) {
  if (!connected_ || !connection_) {
    return false;
  }
  
  auto& quic = QuicApi::instance();
  
  QUIC_BUFFER buf;
  buf.Length = static_cast<uint32_t>(len);
  buf.Buffer = const_cast<uint8_t*>(data);
  
  QUIC_STATUS status = quic.api()->DatagramSend(
    connection_,
    &buf,
    1,
    QUIC_SEND_FLAG_NONE,
    nullptr
  );
  
  return QUIC_SUCCEEDED(status);
}

void QuicConnection::set_receive_callback(ReceiveCallback callback) {
  std::lock_guard lock(mutex_);
  receive_callback_ = std::move(callback);
}

void QuicConnection::close() {
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
  HQUIC connection,
  void* context,
  QUIC_CONNECTION_EVENT* event
) {
  auto* self = static_cast<QuicConnection*>(context);
  self->handle_connection_event(event);
  return QUIC_STATUS_SUCCESS;
}

void QuicConnection::handle_connection_event(QUIC_CONNECTION_EVENT* event) {
  switch (event->Type) {
    case QUIC_CONNECTION_EVENT_CONNECTED: {
      if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
        std::cout << "[QUIC] Client connection established" << std::endl;
      }
      connected_ = true;
      
      // Open a bidirectional stream for RPC
      auto& quic = QuicApi::instance();
      QUIC_STATUS status = quic.api()->StreamOpen(
        connection_,
        QUIC_STREAM_OPEN_FLAG_NONE,
        stream_callback,
        this,
        &stream_
      );
      
      if (QUIC_SUCCEEDED(status)) {
        status = quic.api()->StreamStart(stream_, QUIC_STREAM_START_FLAG_NONE);
      }
      
      if (connect_callback_) {
        bool success = QUIC_SUCCEEDED(status);
        auto cb = std::move(connect_callback_);
        boost::asio::post(ioc_, [cb, success]() { cb(success); });
      }
      break;
    }
    
    case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
      if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
        std::cout << "[QUIC] Connection shutdown by transport: " 
                  << event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status << std::endl;
      }
      connected_ = false;
      {
        std::lock_guard lock(mutex_);
        receive_complete_ = true;
        cv_.notify_all();
      }
      break;
      
    case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
      if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
        std::cout << "[QUIC] Connection shutdown by peer" << std::endl;
      }
      connected_ = false;
      {
        std::lock_guard lock(mutex_);
        receive_complete_ = true;
        cv_.notify_all();
      }
      break;
      
    case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
      connected_ = false;
      break;
      
    case QUIC_CONNECTION_EVENT_DATAGRAM_RECEIVED: {
      // Handle received datagram (unreliable)
      std::lock_guard lock(mutex_);
      if (receive_callback_) {
        auto& buf = event->DATAGRAM_RECEIVED.Buffer;
        receive_callback_(buf->Buffer, buf->Length);
      }
      break;
    }
    
    default:
      break;
  }
}

QUIC_STATUS QUIC_API QuicConnection::stream_callback(
  HQUIC stream,
  void* context,
  QUIC_STREAM_EVENT* event
) {
  auto* self = static_cast<QuicConnection*>(context);
  self->handle_stream_event(stream, event);
  return QUIC_STATUS_SUCCESS;
}

void QuicConnection::handle_stream_event(HQUIC stream, QUIC_STREAM_EVENT* event) {
  switch (event->Type) {
    case QUIC_STREAM_EVENT_RECEIVE: {
      // Data received on stream
      std::lock_guard lock(mutex_);
      if (receive_callback_) {
        for (uint32_t i = 0; i < event->RECEIVE.BufferCount; ++i) {
          auto& buf = event->RECEIVE.Buffers[i];
          receive_callback_(buf.Buffer, buf.Length);
        }
      }
      break;
    }
    
    case QUIC_STREAM_EVENT_SEND_COMPLETE: {
      // Clean up send buffer
      auto* send_buf = static_cast<QUIC_BUFFER*>(event->SEND_COMPLETE.ClientContext);
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

//==============================================================================
// QuicServerConnection - Server-side connection wrapper
//==============================================================================

QuicServerConnection::QuicServerConnection(
  boost::asio::io_context& ioc,
  HQUIC connection,
  HQUIC configuration
)
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

QuicServerConnection::~QuicServerConnection() {
  close();
}

void QuicServerConnection::start() {
  auto& quic = QuicApi::instance();
  
  // Set connection callback
  quic.api()->SetCallbackHandler(
    connection_,
    reinterpret_cast<void*>(connection_callback),
    this
  );
}

void QuicServerConnection::set_message_callback(MessageCallback callback) {
  std::lock_guard lock(mutex_);
  message_callback_ = std::move(callback);
}

bool QuicServerConnection::send(const void* data, size_t len) {
  if (!stream_) {
    return false;
  }
  
  auto& quic = QuicApi::instance();
  
  QUIC_BUFFER* send_buf = new QUIC_BUFFER;
  send_buf->Length = static_cast<uint32_t>(len);
  send_buf->Buffer = new uint8_t[len];
  std::memcpy(send_buf->Buffer, data, len);
  
  QUIC_STATUS status = quic.api()->StreamSend(
    stream_,
    send_buf,
    1,
    QUIC_SEND_FLAG_NONE,
    send_buf
  );
  
  if (QUIC_FAILED(status)) {
    delete[] send_buf->Buffer;
    delete send_buf;
    return false;
  }
  
  return true;
}

void QuicServerConnection::close() {
  auto& quic = QuicApi::instance();
  
  if (stream_) {
    quic.api()->StreamClose(stream_);
    stream_ = nullptr;
  }
  
  if (connection_) {
    quic.api()->ConnectionClose(connection_);
    connection_ = nullptr;
  }
}

QUIC_STATUS QUIC_API QuicServerConnection::connection_callback(
  HQUIC connection,
  void* context,
  QUIC_CONNECTION_EVENT* event
) {
  auto* self = static_cast<QuicServerConnection*>(context);
  self->handle_connection_event(event);
  return QUIC_STATUS_SUCCESS;
}

void QuicServerConnection::handle_connection_event(QUIC_CONNECTION_EVENT* event) {
  auto& quic = QuicApi::instance();
  
  switch (event->Type) {
    case QUIC_CONNECTION_EVENT_CONNECTED:
      if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
        std::cout << "[QUIC] Server connection established from " 
                  << remote_addr_ << ":" << remote_port_ << std::endl;
      }
      break;
      
    case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED: {
      // Accept the incoming stream
      stream_ = event->PEER_STREAM_STARTED.Stream;
      quic.api()->SetCallbackHandler(
        stream_,
        reinterpret_cast<void*>(stream_callback),
        this
      );
      
      if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
        std::cout << "[QUIC] Stream started from peer" << std::endl;
      }
      break;
    }
    
    case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
    case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
    case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
      break;
      
    case QUIC_CONNECTION_EVENT_DATAGRAM_RECEIVED: {
      // Handle unreliable datagram
      std::lock_guard lock(mutex_);
      if (message_callback_) {
        auto& buf = event->DATAGRAM_RECEIVED.Buffer;
        std::vector<uint8_t> data(buf->Buffer, buf->Buffer + buf->Length);
        boost::asio::post(ioc_, [this, self = shared_from_this(), data = std::move(data)]() mutable {
          message_callback_(std::move(data));
        });
      }
      break;
    }
    
    default:
      break;
  }
}

QUIC_STATUS QUIC_API QuicServerConnection::stream_callback(
  HQUIC stream,
  void* context,
  QUIC_STREAM_EVENT* event
) {
  auto* self = static_cast<QuicServerConnection*>(context);
  self->handle_stream_event(stream, event);
  return QUIC_STATUS_SUCCESS;
}

void QuicServerConnection::handle_stream_event(HQUIC stream, QUIC_STREAM_EVENT* event) {
  switch (event->Type) {
    case QUIC_STREAM_EVENT_RECEIVE: {
      // Append data to receive buffer
      for (uint32_t i = 0; i < event->RECEIVE.BufferCount; ++i) {
        auto& buf = event->RECEIVE.Buffers[i];
        receive_buffer_.insert(receive_buffer_.end(), buf.Buffer, buf.Buffer + buf.Length);
      }
      
      // Process complete messages
      process_receive_buffer();
      break;
    }
    
    case QUIC_STREAM_EVENT_SEND_COMPLETE: {
      auto* send_buf = static_cast<QUIC_BUFFER*>(event->SEND_COMPLETE.ClientContext);
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

void QuicServerConnection::process_receive_buffer() {
  // NPRPC message format: first 4 bytes = message size (including header)
  while (receive_buffer_.size() >= 4) {
    uint32_t msg_size = *reinterpret_cast<uint32_t*>(receive_buffer_.data());
    
    if (receive_buffer_.size() < msg_size) {
      break;  // Incomplete message
    }
    
    // Extract complete message
    std::vector<uint8_t> message(receive_buffer_.begin(), receive_buffer_.begin() + msg_size);
    receive_buffer_.erase(receive_buffer_.begin(), receive_buffer_.begin() + msg_size);
    
    // Dispatch to callback
    std::lock_guard lock(mutex_);
    if (message_callback_) {
      boost::asio::post(ioc_, [this, self = shared_from_this(), msg = std::move(message)]() mutable {
        message_callback_(std::move(msg));
      });
    }
  }
}

//==============================================================================
// QuicListener - Server listener
//==============================================================================

QuicListener::QuicListener(
  boost::asio::io_context& ioc,
  const std::string& cert_file,
  const std::string& key_file
)
  : ioc_(ioc)
  , cert_file_(cert_file)
  , key_file_(key_file)
{
}

QuicListener::~QuicListener() {
  stop();
}

void QuicListener::start(uint16_t port, AcceptCallback callback) {
  auto& quic = QuicApi::instance();
  
  accept_callback_ = std::move(callback);
  
  // Create server configuration with certificate
  configuration_ = quic.create_configuration(
    "nprpc",
    true,
    cert_file_.c_str(),
    key_file_.c_str()
  );
  
  if (!configuration_) {
    throw std::runtime_error("Failed to create QUIC server configuration");
  }
  
  // Open listener
  QUIC_STATUS status = quic.api()->ListenerOpen(
    quic.registration(),
    listener_callback,
    this,
    &listener_
  );
  
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
  
  if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
    std::cout << "[QUIC] Listener started on port " << port << std::endl;
  }
}

void QuicListener::stop() {
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

QUIC_STATUS QUIC_API QuicListener::listener_callback(
  HQUIC listener,
  void* context,
  QUIC_LISTENER_EVENT* event
) {
  auto* self = static_cast<QuicListener*>(context);
  self->handle_listener_event(event);
  return QUIC_STATUS_SUCCESS;
}

void QuicListener::handle_listener_event(QUIC_LISTENER_EVENT* event) {
  switch (event->Type) {
    case QUIC_LISTENER_EVENT_NEW_CONNECTION: {
      if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
        std::cout << "[QUIC] New connection from client" << std::endl;
      }
      
      auto& quic = QuicApi::instance();
      
      // Set configuration on the connection
      QUIC_STATUS status = quic.api()->ConnectionSetConfiguration(
        event->NEW_CONNECTION.Connection,
        configuration_
      );
      
      if (QUIC_FAILED(status)) {
        std::cerr << "[QUIC] Failed to set connection configuration" << std::endl;
        return;
      }
      
      // Create server connection wrapper
      auto conn = std::make_shared<QuicServerConnection>(
        ioc_,
        event->NEW_CONNECTION.Connection,
        configuration_
      );
      conn->start();
      
      if (accept_callback_) {
        accept_callback_(conn);
      }
      break;
    }
    
    case QUIC_LISTENER_EVENT_STOP_COMPLETE:
      if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
        std::cout << "[QUIC] Listener stopped" << std::endl;
      }
      break;
      
    default:
      break;
  }
}

//==============================================================================
// QuicServerSession - RPC session over QUIC
//==============================================================================

class QuicServerSession
    : public Session
    , public std::enable_shared_from_this<QuicServerSession>
{
    std::shared_ptr<QuicServerConnection> connection_;

public:
    virtual void timeout_action() final {
        // Server sessions don't have timeouts
    }

    virtual void shutdown() override {
        if (connection_) {
            connection_->set_message_callback(nullptr);
            connection_->close();
        }
        Session::shutdown();
    }

    virtual void send_receive(flat_buffer&, uint32_t) override {
        assert(false && "send_receive should not be called on server session");
    }

    virtual void send_receive_async(
        flat_buffer&&,
        std::optional<std::function<void(const boost::system::error_code&, flat_buffer&)>>&&,
        uint32_t) override
    {
        assert(false && "send_receive_async should not be called on server session");
    }

    void on_message_received(std::vector<uint8_t>&& data) {
        try {
            // Move data into rx_buffer
            rx_buffer_().consume(rx_buffer_().size());
            auto mb = rx_buffer_().prepare(data.size());
            std::memcpy(mb.data(), data.data(), data.size());
            rx_buffer_().commit(data.size());

            // Dispatch the RPC request
            handle_request();

            // Send response back
            auto response_data = rx_buffer_().cdata();
            if (!connection_->send(response_data.data(), response_data.size())) {
                std::cerr << "QuicServerSession: Failed to send response" << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "QuicServerSession: Error processing message: " << e.what() << std::endl;
        }
    }

    QuicServerSession(
        boost::asio::io_context& ioc,
        std::shared_ptr<QuicServerConnection> connection)
        : Session(ioc.get_executor())
        , connection_(std::move(connection))
    {
        ctx_.remote_endpoint = EndPoint(
            EndPointType::Quic,
            connection_->remote_address(),
            connection_->remote_port());

        if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
            std::cout << "QuicServerSession created for " 
                      << connection_->remote_address() << ":" << connection_->remote_port() 
                      << std::endl;
        }
    }

    void start() {
        connection_->set_message_callback([this, self = shared_from_this()](std::vector<uint8_t>&& data) {
            on_message_received(std::move(data));
        });
    }

    ~QuicServerSession() {
        if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
            std::cout << "QuicServerSession destroyed" << std::endl;
        }
    }
};

//==============================================================================
// QuicClientSession - Client-side RPC session over QUIC
//==============================================================================

class QuicClientSession
    : public Session
    , public std::enable_shared_from_this<QuicClientSession>
{
    std::shared_ptr<QuicConnection> connection_;
    EndPoint endpoint_;
    bool connected_ = false;

public:
    virtual void timeout_action() final {
        // Cancel pending operations
    }

    virtual void send_receive(flat_buffer& buffer, uint32_t timeout_ms) override {
        if (!connected_) {
            // Connect synchronously on first call
            std::promise<bool> promise;
            auto future = promise.get_future();
            
            connection_->async_connect(
                std::string(endpoint_.hostname()),
                endpoint_.port(),
                [&promise](bool success) {
                    promise.set_value(success);
                }
            );
            
            // Run io_context until connection completes
            // Note: This is a simplified blocking connect
            // In production, we'd want proper async handling
            if (!future.get()) {
                throw nprpc::ExceptionCommFailure("QUIC connection failed");
            }
            connected_ = true;
        }
        
        connection_->send_receive(buffer, timeout_ms);
    }

    virtual void send_receive_async(
        flat_buffer&& buffer,
        std::optional<std::function<void(const boost::system::error_code&, flat_buffer&)>>&& handler,
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

NPRPC_API void init_quic(boost::asio::io_context& ioc) {
  if (g_cfg.listen_quic_port == 0) {
    if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
      std::cout << "[QUIC] Listen port not set, skipping QUIC initialization" << std::endl;
    }
    return;
  }
  
  if (g_cfg.quic_cert_file.empty() || g_cfg.quic_key_file.empty()) {
    std::cerr << "[QUIC] Certificate and key files required for QUIC server" << std::endl;
    return;
  }
  
  try {
    g_quic_listener = std::make_shared<QuicListener>(
      ioc,
      g_cfg.quic_cert_file,
      g_cfg.quic_key_file
    );
    
    g_quic_listener->start(g_cfg.listen_quic_port,
      [&ioc](std::shared_ptr<QuicServerConnection> conn) {
        auto session = std::make_shared<QuicServerSession>(ioc, conn);
        session->start();
      }
    );
  } catch (const std::exception& e) {
    std::cerr << "[QUIC] Failed to start listener: " << e.what() << std::endl;
  }
}

NPRPC_API void stop_quic_listener() {
  if (g_quic_listener) {
    g_quic_listener->stop();
    g_quic_listener.reset();
  }
}

NPRPC_API std::shared_ptr<Session> make_quic_client_session(
  const EndPoint& endpoint,
  boost::asio::io_context& ioc
) {
  return std::make_shared<QuicClientSession>(endpoint, ioc);
}

} // namespace nprpc::impl

#endif // NPRPC_QUIC_ENABLED

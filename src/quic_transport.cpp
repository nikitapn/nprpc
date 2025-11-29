// Copyright (c) 2025 nikitapnn1@gmail.com
// SPDX-License-Identifier: MIT

#include <nprpc/impl/quic_transport.hpp>

#ifdef NPRPC_QUIC_ENABLED

#include <iostream>
#include <cstring>

namespace nprpc::impl {

// ALPN for NPRPC over QUIC
static const QUIC_BUFFER alpn_buffer = {
  sizeof("nprpc") - 1,
  (uint8_t*)"nprpc"
};

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
  
  std::cout << "[QUIC] MsQuic initialized successfully" << std::endl;
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
  settings.DatagramReceiveEnabled = TRUE;  // Enable QUIC datagrams
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
      std::cout << "[QUIC] Connection established" << std::endl;
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
      std::cout << "[QUIC] Connection shutdown by transport: " 
                << event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status << std::endl;
      connected_ = false;
      break;
      
    case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
      std::cout << "[QUIC] Connection shutdown by peer" << std::endl;
      connected_ = false;
      break;
      
    case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
      std::cout << "[QUIC] Connection shutdown complete" << std::endl;
      connected_ = false;
      break;
      
    case QUIC_CONNECTION_EVENT_DATAGRAM_RECEIVED: {
      // Handle received datagram
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
      std::cout << "[QUIC] Peer finished sending on stream" << std::endl;
      break;
      
    case QUIC_STREAM_EVENT_PEER_SEND_ABORTED:
      std::cout << "[QUIC] Peer aborted stream" << std::endl;
      break;
      
    case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
      std::cout << "[QUIC] Stream shutdown complete" << std::endl;
      break;
      
    default:
      break;
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
  
  std::cout << "[QUIC] Listener started on port " << port << std::endl;
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
      std::cout << "[QUIC] New connection from client" << std::endl;
      
      auto& quic = QuicApi::instance();
      
      // Create a QuicConnection wrapper for the server-side connection
      auto conn = std::make_shared<QuicConnection>(ioc_);
      // Note: Server-side connection handling needs more work
      // For now, just accept the connection
      
      QUIC_STATUS status = quic.api()->ConnectionSetConfiguration(
        event->NEW_CONNECTION.Connection,
        configuration_
      );
      
      if (QUIC_FAILED(status)) {
        std::cerr << "[QUIC] Failed to set connection configuration" << std::endl;
      }
      
      if (accept_callback_) {
        accept_callback_(conn);
      }
      break;
    }
    
    case QUIC_LISTENER_EVENT_STOP_COMPLETE:
      std::cout << "[QUIC] Listener stopped" << std::endl;
      break;
      
    default:
      break;
  }
}

} // namespace nprpc::impl

#endif // NPRPC_QUIC_ENABLED

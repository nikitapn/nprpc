// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include <nprpc/impl/http3_server.hpp>

// This file implements HTTP/3 using ngtcp2 + nghttp3 backend
#if defined(NPRPC_HTTP3_ENABLED) && defined(NPRPC_HTTP3_BACKEND_NGHTTP3)

#include <nprpc/impl/http_file_cache.hpp>
#include <nprpc/impl/http_rpc_session.hpp>
#include <nprpc/impl/http_utils.hpp>
#include <nprpc/impl/nprpc_impl.hpp>
#ifdef NPRPC_SSR_ENABLED
#include <nprpc/impl/ssr_manager.hpp>
#endif
#include <nprpc/common.hpp>

#include <nghttp3/nghttp3.h>
#include <ngtcp2/ngtcp2.h>
#include <ngtcp2/ngtcp2_crypto.h>
#include <ngtcp2/ngtcp2_crypto_ossl.h>

#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>

#include <boost/asio.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/steady_timer.hpp>

#include "../logging.hpp"
#include <array>
#include <chrono>
#include <cstring>
#include <deque>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#define NPRPC_ENABLE_HTTP3_TRACE 0

#if NPRPC_ENABLE_HTTP3_TRACE
# include <format>
# define NPRPC_HTTP3_TRACE(format_string, ...)               \
  NPRPC_LOG_TRACE(                                           \
    "[HTTP/3] " format_string __VA_OPT__(, ) __VA_ARGS__);
#else
# define NPRPC_HTTP3_TRACE(format_string, ...) do {} while (0)
#endif

#define NPRPC_HTTP3_ERROR(format_string, ...)                \
  NPRPC_LOG_ERROR(                                           \
    "[HTTP/3] " format_string __VA_OPT__(, ) __VA_ARGS__);

#define NPRPC_ENABLE_HTTP3_RESPONSE_DEBUG 0

#if NPRPC_ENABLE_HTTP3_RESPONSE_DEBUG
# define NPRPC_HTTP3_DEBUG(format_string, ...)               \
  NPRPC_LOG_INFO(                                            \
    "[HTTP/3][DBG] " format_string __VA_OPT__(, ) __VA_ARGS__);
#else
# define NPRPC_HTTP3_DEBUG(format_string, ...) do {} while (0)
#endif

#define NPRPC_NGTCP2_ENABLE_LOGGING 0

namespace nprpc::impl {

//==============================================================================
// Constants
//==============================================================================

namespace {

// ALPN for HTTP/3
static constexpr uint8_t H3_ALPN[] = "\x02h3";
static constexpr size_t H3_ALPN_LEN = sizeof(H3_ALPN) - 1;

// Connection ID length
static constexpr size_t SCID_LEN = 18;

// Maximum UDP payload size (conservative for QUIC)
static constexpr size_t MAX_UDP_PAYLOAD_SIZE = 1350;

// Maximum number of packets to send at once
static constexpr size_t MAX_PKTS_BURST = 64;

// Static secret for tokens
static std::array<uint8_t, 32> g_static_secret;
static bool g_static_secret_initialized = false;

static constexpr std::string_view k_webtransport_path = "/wt";
static constexpr uint64_t k_webtransport_bidi_stream_type = 0x41;
static constexpr uint8_t k_webtransport_bind_control = 0;
static constexpr uint8_t k_webtransport_bind_native_stream = 1;

} // anonymous namespace

//==============================================================================
// Utility functions
//==============================================================================

namespace {

uint64_t timestamp_ns()
{
  auto now = std::chrono::steady_clock::now();
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             now.time_since_epoch())
      .count();
}

void append_bytes(flat_buffer& buffer, const uint8_t* data, size_t len)
{
  if (len == 0) {
    return;
  }

  auto writable = buffer.prepare(len);
  std::memcpy(writable.data(), data, len);
  buffer.commit(len);
}

flat_buffer copy_bytes_to_buffer(const uint8_t* data, size_t len)
{
  flat_buffer buffer(len == 0 ? flat_buffer::default_initial_size() : len);
  append_bytes(buffer, data, len);
  return buffer;
}

void random_bytes(uint8_t* data, size_t len)
{
  RAND_bytes(data, static_cast<int>(len));
}

void init_static_secret()
{
  if (!g_static_secret_initialized) {
    random_bytes(g_static_secret.data(), g_static_secret.size());
    g_static_secret_initialized = true;
  }
}

std::string cid_to_string(const ngtcp2_cid* cid)
{
  return std::string(reinterpret_cast<const char*>(cid->data), cid->datalen);
}

const char* crypto_default_ciphers()
{
  return "TLS_AES_128_GCM_SHA256:TLS_AES_256_GCM_SHA384:TLS_CHACHA20_"
         "POLY1305_"
         "SHA256";
}

const char* crypto_default_groups() { return "X25519:P-256:P-384:P-521"; }

#if NPRPC_NGTCP2_ENABLE_LOGGING
void log_printf(void* user_data, const char* fmt, ...)
{
  va_list ap;
  std::array<char, 4096> buf;

  va_start(ap, fmt);
  auto n = vsnprintf(buf.data(), buf.size(), fmt, ap);
  va_end(ap);

  if (static_cast<size_t>(n) >= buf.size()) {
    n = buf.size() - 1;
  }

  NPRPC_HTTP3_TRACE("{}", std::string_view(buf.data(), n));
}
#endif
} // anonymous namespace

//==============================================================================
// Http3Stream - Tracks state for a single HTTP/3 request stream
//==============================================================================

class WebTransportControlSession;

enum class WebTransportChildBinding : uint8_t {
  Unbound,
  Control,
  Native,
};

enum class RawWritePriority : uint8_t {
  Control = 0,
  Native = 1,
  Default = 2,
};

struct PreparedResponseHeaders {
  std::vector<nghttp3_nv> headers;
  std::string status;
  std::string content_length;
  std::string content_type;
  std::string allow_origin;
};

struct Http3Stream {
  int64_t stream_id = -1;
  std::string method;
  std::string path;
  std::string authority;
  std::string scheme;
  std::string protocol;
  std::string content_type;
  std::string accept;                         // Accept header for SSR detection
  std::map<std::string, std::string> headers; // All headers for SSR
  size_t content_length = 0;
  flat_buffer request_body;

  bool http_stream = false;
  bool webtransport_session = false;
  bool webtransport_child_stream = false;
  WebTransportChildBinding webtransport_child_binding =
      WebTransportChildBinding::Unbound;
  bool response_started = false;
  int64_t webtransport_session_id = -1;
  uint64_t webtransport_native_stream_id = 0;
  flat_buffer webtransport_probe_buffer;
  std::deque<flat_buffer> raw_write_queue;
  bool raw_write_scheduled = false;

  // Response data - kept alive for async sending
  // For cached files: cached_file keeps the data alive (zero-copy)
  // For static responses: response_data points to body data
  // For dynamic responses: use a string to hold the body
  flat_buffer dynamic_body;
  std::string response_content_type; // Store response content-type for lifetime
  PreparedResponseHeaders response_headers;
  CachedFileGuard cached_file;
  const uint8_t* response_data = nullptr;
  size_t response_len = 0;
  size_t response_offset = 0; // How much has been sent
  bool headers_sent = false;
  bool body_complete = false;
  bool fin_sent = false;
};

void log_http3_response_submit(std::string_view kind,
                               const Http3Stream* stream,
                               unsigned int status_code,
                               std::string_view content_type,
                               size_t content_length)
{
  NPRPC_HTTP3_DEBUG(
      "submit kind={} stream_id={} path='{}' status={} content_type='{}' content_length={} response_offset={} webtransport_session={}",
      kind, stream ? stream->stream_id : -1,
      stream ? std::string_view(stream->path) : std::string_view(""),
      status_code, content_type, content_length,
      stream ? stream->response_offset : 0,
      stream && stream->webtransport_session);
}

//==============================================================================
// Http3Connection - A single QUIC connection with HTTP/3
//==============================================================================

class Http3Server;

class Http3Connection : public std::enable_shared_from_this<Http3Connection>
{
public:
  Http3Connection(
      Http3Server* server,
      boost::asio::ip::udp::socket& socket,
      const boost::asio::ip::udp::endpoint& local_ep,
      const boost::asio::ip::udp::endpoint& remote_ep,
      const ngtcp2_cid* client_scid, // Client's Source CID (from hd.scid)
      const ngtcp2_cid* client_dcid, // Client's Destination CID (from hd.dcid)
      const ngtcp2_cid* ocid,
      const uint8_t* token,
      size_t tokenlen,
      uint32_t version,
      SSL_CTX* ssl_ctx);
  ~Http3Connection();

  bool init();
  int on_read(const uint8_t* data, size_t len, const ngtcp2_pkt_info* pi);
  int on_write();
  int handle_expiry();
  void schedule_timer();
  void enqueue_packet(flat_buffer&& data, ngtcp2_pkt_info pi);

  const ngtcp2_cid& scid() const { return scid_; }
  const boost::asio::ip::udp::endpoint& remote_endpoint() const
  {
    return remote_ep_;
  }
  ngtcp2_conn* conn() { return conn_; }

  bool is_closed() const { return closed_; }
  const void* debug_id() const noexcept { return this; }
  bool is_draining() const
  {
    return conn_ && (ngtcp2_conn_in_draining_period(conn_) ||
                     ngtcp2_conn_in_closing_period(conn_));
  }

  // Called by server when write is possible
  void signal_write();

  auto get_executor() { return socket_.get_executor(); }
  void queue_raw_stream_write(int64_t stream_id, flat_buffer&& data);

  // ngtcp2 crypto connection reference
  ngtcp2_crypto_conn_ref* conn_ref() { return &conn_ref_; }

private:
  // Stream management
  Http3Stream* find_stream(int64_t stream_id);
  Http3Stream* create_stream(int64_t stream_id);
  void remove_stream(int64_t stream_id);
  Http3Stream* next_raw_writable_stream();
  void schedule_raw_writable_stream(Http3Stream* stream);
  static RawWritePriority raw_write_priority(const Http3Stream* stream);

  // HTTP/3 handling
  int setup_httpconn();
  int recv_stream_data(uint32_t flags,
                       int64_t stream_id,
                       const uint8_t* data,
                       size_t datalen);
  int acked_stream_data_offset(int64_t stream_id, uint64_t datalen);
  void extend_max_remote_streams_bidi(uint64_t max_streams);
  void extend_max_stream_data(int64_t stream_id, uint64_t max_data);

  // HTTP callbacks
  void http_begin_headers(int64_t stream_id);
  void http_recv_header(Http3Stream* stream,
                        int32_t token,
                        nghttp3_rcbuf* name,
                        nghttp3_rcbuf* value);
  int http_end_headers(Http3Stream* stream);
  int http_end_stream(Http3Stream* stream);
  void http_stream_close(int64_t stream_id, uint64_t app_error_code);

  // Response handling
  int start_response(Http3Stream* stream);
  // Zero-copy response using cached file
  int send_cached_response(Http3Stream* stream,
                           unsigned int status_code,
                           CachedFileGuard cached_file);
  // Zero-copy response for static content
  int send_static_response(Http3Stream* stream,
                           unsigned int status_code,
                           std::string_view content_type,
                           std::string_view body);
  // FIXME: Use nprpc::flat_buffer
  // Copy-based response for dynamic content
  int send_dynamic_response(Http3Stream* stream,
                            unsigned int status_code,
                            std::string_view content_type,
                            std::string&& body);
  int send_dynamic_response(Http3Stream* stream,
                            unsigned int status_code,
                            std::string_view content_type,
                            flat_buffer&& body);
  int send_cors_preflight(Http3Stream* stream);
  int send_webtransport_connect_response(Http3Stream* stream);

  // RPC Handling
  int handle_rpc_request(Http3Stream* stream);
  int handle_webtransport_connect(Http3Stream* stream);
  int handle_webtransport_stream_data(Http3Stream* stream,
                                      uint32_t flags,
                                      const uint8_t* data,
                                      size_t datalen);
  bool has_webtransport_session(int64_t session_stream_id) const;
  std::shared_ptr<WebTransportControlSession>
  get_or_create_webtransport_control_session(int64_t session_stream_id);
  PreparedResponseHeaders&
  make_response_headers(Http3Stream* stream,
                        unsigned int status_code,
                        std::string_view content_type,
                        size_t content_length,
                        bool include_cors,
                        bool preflight = false);

  // Error handling
  int handle_error();
  void start_draining_period();
  int start_closing_period();

  // Debug
  void print_ssl_state(std::string_view prefix);

  // Static callbacks for ngtcp2
  static int on_handshake_completed(ngtcp2_conn* conn, void* user_data);
  static int on_recv_stream_data(ngtcp2_conn* conn,
                                 uint32_t flags,
                                 int64_t stream_id,
                                 uint64_t offset,
                                 const uint8_t* data,
                                 size_t datalen,
                                 void* user_data,
                                 void* stream_user_data);
  static int on_acked_stream_data_offset(ngtcp2_conn* conn,
                                         int64_t stream_id,
                                         uint64_t offset,
                                         uint64_t datalen,
                                         void* user_data,
                                         void* stream_user_data);
  static int
  on_stream_open(ngtcp2_conn* conn, int64_t stream_id, void* user_data);
  static int on_stream_close(ngtcp2_conn* conn,
                             uint32_t flags,
                             int64_t stream_id,
                             uint64_t app_error_code,
                             void* user_data,
                             void* stream_user_data);
  static int on_extend_max_remote_streams_bidi(ngtcp2_conn* conn,
                                               uint64_t max_streams,
                                               void* user_data);
  static int on_extend_max_stream_data(ngtcp2_conn* conn,
                                       int64_t stream_id,
                                       uint64_t max_data,
                                       void* user_data,
                                       void* stream_user_data);
  static int on_recv_tx_key(ngtcp2_conn* conn,
                            ngtcp2_encryption_level level,
                            void* user_data);
  static int on_get_new_connection_id(ngtcp2_conn* conn,
                                      ngtcp2_cid* cid,
                                      uint8_t* token,
                                      size_t cidlen,
                                      void* user_data);
  static int on_remove_connection_id(ngtcp2_conn* conn,
                                     const ngtcp2_cid* cid,
                                     void* user_data);
  static void
  on_rand(uint8_t* dest, size_t destlen, const ngtcp2_rand_ctx* rand_ctx);

  static int on_recv_client_initial(ngtcp2_conn* conn,
                                    const ngtcp2_cid* dcid,
                                    void* user_data)
  {
    NPRPC_HTTP3_TRACE("on_recv_client_initial: dcid len={}", dcid->datalen);
    int rv = ngtcp2_crypto_recv_client_initial_cb(conn, dcid, user_data);
    NPRPC_HTTP3_TRACE("on_recv_client_initial: returned {}", rv);
    return rv;
  }

  static int on_recv_crypto_data(ngtcp2_conn* conn,
                                 ngtcp2_encryption_level crypto_level,
                                 uint64_t offset,
                                 const uint8_t* data,
                                 size_t datalen,
                                 void* user_data)
  {
    NPRPC_HTTP3_TRACE("on_recv_crypto_data: level={}, offset={}, len={}",
                      (int)crypto_level, offset, datalen);

    // Get the ossl_ctx to check state before and after
    auto ossl_ctx = static_cast<ngtcp2_crypto_ossl_ctx*>(
        ngtcp2_conn_get_tls_native_handle(conn));
    auto ssl = ngtcp2_crypto_ossl_ctx_get_ssl(ossl_ctx);
    NPRPC_HTTP3_TRACE("on_recv_crypto_data: SSL state before: {}",
                      SSL_state_string_long(ssl));

    int rv = ngtcp2_crypto_recv_crypto_data_cb(conn, crypto_level, offset, data,
                                               datalen, user_data);

    NPRPC_HTTP3_TRACE("on_recv_crypto_data: SSL state after: {}",
                      SSL_state_string_long(ssl));
    NPRPC_HTTP3_TRACE("on_recv_crypto_data: returned {}", rv);

    return rv;
  }

  // Static callbacks for nghttp3
  static int http_acked_stream_data_cb(nghttp3_conn* conn,
                                       int64_t stream_id,
                                       uint64_t datalen,
                                       void* user_data,
                                       void* stream_user_data);
  static int http_recv_data_cb(nghttp3_conn* conn,
                               int64_t stream_id,
                               const uint8_t* data,
                               size_t datalen,
                               void* user_data,
                               void* stream_user_data);
  static int http_deferred_consume_cb(nghttp3_conn* conn,
                                      int64_t stream_id,
                                      size_t nconsumed,
                                      void* user_data,
                                      void* stream_user_data);
  static int http_begin_headers_cb(nghttp3_conn* conn,
                                   int64_t stream_id,
                                   void* user_data,
                                   void* stream_user_data);
  static int http_recv_header_cb(nghttp3_conn* conn,
                                 int64_t stream_id,
                                 int32_t token,
                                 nghttp3_rcbuf* name,
                                 nghttp3_rcbuf* value,
                                 uint8_t flags,
                                 void* user_data,
                                 void* stream_user_data);
  static int http_end_headers_cb(nghttp3_conn* conn,
                                 int64_t stream_id,
                                 int fin,
                                 void* user_data,
                                 void* stream_user_data);
  static int http_end_stream_cb(nghttp3_conn* conn,
                                int64_t stream_id,
                                void* user_data,
                                void* stream_user_data);
  static int http_stop_sending_cb(nghttp3_conn* conn,
                                  int64_t stream_id,
                                  uint64_t app_error_code,
                                  void* user_data,
                                  void* stream_user_data);
  static int http_reset_stream_cb(nghttp3_conn* conn,
                                  int64_t stream_id,
                                  uint64_t app_error_code,
                                  void* user_data,
                                  void* stream_user_data);
  static nghttp3_ssize http_read_data_cb(nghttp3_conn* conn,
                                         int64_t stream_id,
                                         nghttp3_vec* vec,
                                         size_t veccnt,
                                         uint32_t* pflags,
                                         void* user_data,
                                         void* stream_user_data);

  Http3Server* server_;
  boost::asio::ip::udp::socket& socket_;
  boost::asio::ip::udp::endpoint local_ep_;
  boost::asio::ip::udp::endpoint remote_ep_;
  boost::asio::strand<boost::asio::io_context::executor_type> strand_;
  boost::asio::steady_timer timer_;

  ngtcp2_conn* conn_ = nullptr;
  ngtcp2_crypto_conn_ref conn_ref_;
  nghttp3_conn* httpconn_ = nullptr;
  ngtcp2_crypto_ossl_ctx* ossl_ctx_ = nullptr;
  SSL_CTX* ssl_ctx_ = nullptr;

  ngtcp2_cid scid_;
  ngtcp2_cid dcid_;
  ngtcp2_cid ocid_;
  ngtcp2_cid
      original_client_dcid_; // What client sent as DCID in Initial packet
  std::vector<uint8_t> initial_token_;
  uint32_t version_;

  std::unordered_map<int64_t, std::unique_ptr<Http3Stream>> streams_;
  std::array<std::deque<int64_t>, 3> raw_writable_streams_;
  std::unordered_set<int64_t> webtransport_session_ids_;
  std::unordered_map<int64_t, std::shared_ptr<WebTransportControlSession>>
      webtransport_control_sessions_;

  // Send buffer
  std::vector<uint8_t> send_buf_;

  // Connection close buffer
  std::vector<uint8_t> conn_close_buf_;

  ngtcp2_ccerr last_error_{};
  bool closed_ = false;
  bool timer_active_ = false;
};

//==============================================================================
// WebTransportControlSession - minimal NPRPC tunnel on a WT child stream
//==============================================================================

class WebTransportControlSession : public Session,
                                   public std::enable_shared_from_this<WebTransportControlSession>
{
public:
  WebTransportControlSession(Http3Connection& connection,
                             int64_t session_stream_id,
                             const boost::asio::ip::udp::endpoint& remote_ep)
      : Session(connection.get_executor())
      , connection_(connection)
      , session_stream_id_(session_stream_id)
      , rx_buffer_(4 * 1024 * 1024)
  {
    ctx_.remote_endpoint = EndPoint(EndPointType::WebTransport,
                                    remote_ep.address().to_string(),
                                    remote_ep.port());
  }

  void process_bytes(const uint8_t* data, size_t datalen)
  {
    append_bytes(receive_buffer_, data, datalen);

    while (receive_buffer_.size() >= sizeof(std::uint32_t)) {
      std::uint32_t payload_len = 0;
      std::memcpy(&payload_len, receive_buffer_.data_ptr(), sizeof(payload_len));

      const size_t frame_len = static_cast<size_t>(payload_len) + sizeof(std::uint32_t);
      if (receive_buffer_.size() < frame_len) {
        break;
      }

      rx_buffer_.clear();
      auto mb = rx_buffer_.prepare(frame_len);
      std::memcpy(mb.data(), receive_buffer_.data_ptr(), frame_len);
      rx_buffer_.commit(frame_len);

      const auto request_id = extract_request_id(rx_buffer_);
      flat_buffer tx_buffer{std::max(last_tx_size_, flat_buffer::default_initial_size())};
      const bool needs_reply = handle_request(rx_buffer_, tx_buffer);

      if (needs_reply) {
        last_tx_size_ = std::max(tx_buffer.size(), flat_buffer::default_initial_size());
        inject_request_id(tx_buffer, request_id);
        if (control_stream_id_ < 0) {
          NPRPC_LOG_ERROR("[HTTP/3][WT] Cannot reply before control stream is bound");
          return;
        }
        queue_buffer(control_stream_id_, std::move(tx_buffer));
      }

      receive_buffer_.consume(frame_len);
    }
  }

  void send_receive(flat_buffer&, uint32_t) override
  {
    assert(false && "send_receive not supported on WebTransport server control session");
  }

  void send_receive_async(
      flat_buffer&&,
      std::optional<std::function<void(const boost::system::error_code&, flat_buffer&)>>&&,
      uint32_t) override
  {
    assert(false && "send_receive_async not supported on WebTransport server control session");
  }

  void send_stream_message(flat_buffer&& buffer) override
  {
    const auto stream_id = extract_stream_id(buffer);
    if (!stream_id) {
      NPRPC_LOG_ERROR("[HTTP/3][WT] Missing stream_id in native stream message");
      return;
    }

    auto it = native_stream_bindings_.find(*stream_id);
    if (it == native_stream_bindings_.end()) {
      pending_native_writes_[*stream_id].emplace_back(std::move(buffer));
      return;
    }

    queue_buffer(it->second, std::move(buffer));
  }

  void send_main_stream_message(flat_buffer&& buffer) override
  {
    if (control_stream_id_ < 0) {
      NPRPC_LOG_ERROR("[HTTP/3][WT] Control stream is not bound for session {}",
                      session_stream_id_);
      return;
    }

    queue_buffer(control_stream_id_, std::move(buffer));
  }

  void bind_control_stream(int64_t transport_stream_id)
  {
    control_stream_id_ = transport_stream_id;
  }

  void bind_native_stream(uint64_t stream_id, int64_t transport_stream_id)
  {
    native_stream_bindings_[stream_id] = transport_stream_id;

    auto pending_it = pending_native_writes_.find(stream_id);
    if (pending_it == pending_native_writes_.end()) {
      return;
    }

    for (auto& buffer : pending_it->second) {
      queue_buffer(transport_stream_id, std::move(buffer));
    }
    pending_native_writes_.erase(pending_it);
  }

  void on_transport_stream_closed(int64_t transport_stream_id)
  {
    if (control_stream_id_ == transport_stream_id) {
      control_stream_id_ = -1;
    }

    for (auto it = native_stream_bindings_.begin();
         it != native_stream_bindings_.end();) {
      if (it->second == transport_stream_id) {
        it = native_stream_bindings_.erase(it);
      } else {
        ++it;
      }
    }
  }

  int64_t session_stream_id() const noexcept { return session_stream_id_; }

private:
  static void inject_request_id(flat_buffer& buffer, uint32_t request_id)
  {
    if (buffer.size() >= sizeof(impl::Header)) {
      impl::flat::Header_Direct header(buffer, 0);
      header.request_id() = request_id;
    }
  }

  static uint32_t extract_request_id(const flat_buffer& buffer)
  {
    if (buffer.size() >= sizeof(impl::Header)) {
      const impl::flat::Header_Direct header(const_cast<flat_buffer&>(buffer), 0);
      return header.request_id();
    }

    return 0;
  }

  static std::optional<uint64_t> extract_stream_id(flat_buffer& buffer)
  {
    if (buffer.size() < sizeof(impl::flat::Header) + sizeof(uint64_t)) {
      return std::nullopt;
    }

    const impl::flat::Header_Direct header(buffer, 0);
    const auto offset = static_cast<uint32_t>(sizeof(impl::flat::Header));

    switch (header.msg_id()) {
    case MessageId::StreamDataChunk:
      return impl::flat::StreamChunk_Direct(buffer, offset).stream_id();
    case MessageId::StreamCompletion:
      return impl::flat::StreamComplete_Direct(buffer, offset).stream_id();
    case MessageId::StreamError:
      return impl::flat::StreamError_Direct(buffer, offset).stream_id();
    case MessageId::StreamCancellation:
      return impl::flat::StreamCancel_Direct(buffer, offset).stream_id();
    case MessageId::StreamWindowUpdate:
      return impl::flat::StreamWindowUpdate_Direct(buffer, offset).stream_id();
    default:
      return std::nullopt;
    }
  }

  void queue_buffer(int64_t transport_stream_id, flat_buffer&& buffer)
  {
    connection_.queue_raw_stream_write(transport_stream_id, std::move(buffer));
  }

  void timeout_action() override {}

  Http3Connection& connection_;
  int64_t session_stream_id_;
  int64_t control_stream_id_{-1};
  flat_buffer rx_buffer_;
  flat_buffer receive_buffer_;
  size_t last_tx_size_{flat_buffer::default_initial_size()};
  std::unordered_map<uint64_t, int64_t> native_stream_bindings_;
  std::unordered_map<uint64_t, std::deque<flat_buffer>> pending_native_writes_;
};

namespace {

std::optional<std::pair<uint64_t, size_t>> decode_quic_varint(const uint8_t* data,
                                                              size_t size)
{
  if (size == 0) {
    return std::nullopt;
  }

  const uint8_t first = data[0];
  const size_t encoded_len = size_t{1} << (first >> 6);
  if (size < encoded_len) {
    return std::nullopt;
  }

  uint64_t value = first & 0x3f;
  for (size_t index = 1; index < encoded_len; ++index) {
    value = (value << 8) | data[index];
  }

  return std::pair<uint64_t, size_t>{value, encoded_len};
}

} // anonymous namespace

//==============================================================================
// Http3Server - Main HTTP/3 server using ngtcp2 + nghttp3
//==============================================================================

class Http3Server
{
public:
  Http3Server(boost::asio::io_context& ioc,
              const std::string& cert_file,
              const std::string& key_file,
              uint16_t port);
  ~Http3Server();

  bool start();
  void stop();

  boost::asio::io_context& io_context() { return ioc_; }
  boost::asio::ip::udp::socket& socket() { return socket_; }

  // Send packet to remote endpoint
  int send_packet(const boost::asio::ip::udp::endpoint& remote_ep,
                  const uint8_t* data,
                  size_t len);

  // Connection management
  void associate_cid(const ngtcp2_cid* cid,
                     std::shared_ptr<Http3Connection> conn);
  void dissociate_cid(const ngtcp2_cid* cid);
  void remove_connection(std::shared_ptr<Http3Connection> conn);

private:
  void do_receive();
  void handle_packet(const boost::asio::ip::udp::endpoint& remote_ep,
                     const uint8_t* data,
                     size_t len);
  int handle_new_connection(const boost::asio::ip::udp::endpoint& remote_ep,
                            const uint8_t* data,
                            size_t len,
                            const ngtcp2_version_cid& vc);
  int send_version_negotiation(const boost::asio::ip::udp::endpoint& remote_ep,
                               const ngtcp2_version_cid& vc);
  int send_stateless_reset(const boost::asio::ip::udp::endpoint& remote_ep,
                           const uint8_t* dcid,
                           size_t dcidlen);
  int send_retry(const boost::asio::ip::udp::endpoint& remote_ep,
                 const ngtcp2_pkt_hd& hd,
                 const boost::asio::ip::udp::endpoint& local_ep);

  std::shared_ptr<Http3Connection> find_connection(const ngtcp2_cid* dcid);

  boost::asio::io_context& ioc_;
  std::string cert_file_;
  std::string key_file_;
  uint16_t port_;

  boost::asio::ip::udp::socket socket_;
  boost::asio::ip::udp::endpoint local_ep_;  // Local bound address
  boost::asio::ip::udp::endpoint remote_ep_; // For async_receive_from
  std::array<uint8_t, 65536> recv_buf_;

  SSL_CTX* ssl_ctx_ = nullptr;

  std::mutex mutex_;
  // Map from CID to connection (includes both SCID and additional CIDs)
  std::unordered_map<std::string, std::shared_ptr<Http3Connection>>
      connections_;

  bool running_ = false;
};

//==============================================================================
// Http3Connection Implementation
//==============================================================================

Http3Connection::Http3Connection(
    Http3Server* server,
    boost::asio::ip::udp::socket& socket,
    const boost::asio::ip::udp::endpoint& local_ep,
    const boost::asio::ip::udp::endpoint& remote_ep,
    const ngtcp2_cid* client_scid, // Client's Source CID (from hd.scid)
    const ngtcp2_cid* client_dcid, // Client's Destination CID (from hd.dcid)
    const ngtcp2_cid* ocid,
    const uint8_t* token,
    size_t tokenlen,
    uint32_t version,
    SSL_CTX* ssl_ctx)
    : server_(server)
    , socket_(socket)
    , local_ep_(local_ep)
    , remote_ep_(remote_ep)
    , strand_(boost::asio::make_strand(server->io_context()))
    , timer_(server->io_context())
    , ssl_ctx_(ssl_ctx)
    , version_(version)
{
  // dcid_ stores the client's source CID - this is what
  // ngtcp2_conn_server_new expects as its 'dcid' parameter (the CID that
  // appears in client Initial as Source CID)
  dcid_ = *client_scid;

  // original_client_dcid_ stores what the client sent as Destination CID
  // This is used for params.original_dcid
  original_client_dcid_ = *client_dcid;

  if (ocid) {
    ocid_ = *ocid;
  } else {
    ocid_.datalen = 0;
  }

  if (token && tokenlen > 0) {
    initial_token_.assign(token, token + tokenlen);
  }

  // Generate our own source CID
  scid_.datalen = SCID_LEN;
  random_bytes(scid_.data, scid_.datalen);

  send_buf_.resize(MAX_UDP_PAYLOAD_SIZE * MAX_PKTS_BURST);

  // Initialize the connection reference for crypto callbacks
  conn_ref_.get_conn = [](ngtcp2_crypto_conn_ref* ref) -> ngtcp2_conn* {
    auto h = static_cast<Http3Connection*>(ref->user_data);
    NPRPC_HTTP3_TRACE("conn_ref_.get_conn called, conn_={}", (void*)h->conn_);
    return h->conn_;
  };
  conn_ref_.user_data = this;
}

Http3Connection::~Http3Connection()
{
  timer_.cancel();

  if (httpconn_) {
    nghttp3_conn_del(httpconn_);
  }
  if (conn_) {
    ngtcp2_conn_del(conn_);
  }
  if (ossl_ctx_) {
    auto ssl = ngtcp2_crypto_ossl_ctx_get_ssl(ossl_ctx_);
    if (ssl) {
      SSL_set_app_data(ssl, nullptr);
      SSL_free(ssl);
    }
    ngtcp2_crypto_ossl_ctx_del(ossl_ctx_);
  }
}

bool Http3Connection::init()
{
  NPRPC_HTTP3_TRACE("Http3Connection::init() starting...");

  // Create OpenSSL QUIC context
  if (ngtcp2_crypto_ossl_ctx_new(&ossl_ctx_, nullptr) != 0) {
    NPRPC_LOG_ERROR("[HTTP/3][E] Failed to create OpenSSL QUIC context");
    return false;
  }

  // Set up ngtcp2 callbacks
  ngtcp2_callbacks callbacks{
      .recv_client_initial = on_recv_client_initial,
      .recv_crypto_data = on_recv_crypto_data, // Use our wrapper
      .handshake_completed = on_handshake_completed,
      .encrypt = ngtcp2_crypto_encrypt_cb,
      .decrypt = ngtcp2_crypto_decrypt_cb,
      .hp_mask = ngtcp2_crypto_hp_mask_cb,
      .recv_stream_data = on_recv_stream_data,
      .acked_stream_data_offset = on_acked_stream_data_offset,
      .stream_open = on_stream_open,
      .stream_close = on_stream_close,
      .rand = on_rand,
      .get_new_connection_id = on_get_new_connection_id,
      .remove_connection_id = on_remove_connection_id,
      .update_key = ngtcp2_crypto_update_key_cb,
      .extend_max_remote_streams_bidi = on_extend_max_remote_streams_bidi,
      .extend_max_stream_data = on_extend_max_stream_data,
      .delete_crypto_aead_ctx = ngtcp2_crypto_delete_crypto_aead_ctx_cb,
      .delete_crypto_cipher_ctx = ngtcp2_crypto_delete_crypto_cipher_ctx_cb,
      .get_path_challenge_data = ngtcp2_crypto_get_path_challenge_data_cb,
      .version_negotiation = ngtcp2_crypto_version_negotiation_cb,
      .recv_tx_key = on_recv_tx_key,
  };

  // Configure settings
  ngtcp2_settings settings;
  ngtcp2_settings_default(&settings);
#if NPRPC_NGTCP2_ENABLE_LOGGING
  settings.log_printf = log_printf;
#else
  settings.log_printf = nullptr;
#endif
  settings.initial_ts = timestamp_ns();
  if (!initial_token_.empty()) {
    settings.token = initial_token_.data();
    settings.tokenlen = initial_token_.size();
  }
  settings.cc_algo = NGTCP2_CC_ALGO_CUBIC;
  settings.max_tx_udp_payload_size = MAX_UDP_PAYLOAD_SIZE;

  // Configure transport parameters
  ngtcp2_transport_params params;
  ngtcp2_transport_params_default(&params);
  params.initial_max_stream_data_bidi_local = 256 * 1024;
  params.initial_max_stream_data_bidi_remote = 256 * 1024;
  params.initial_max_stream_data_uni = 256 * 1024;
  params.initial_max_data = 1 * 1024 * 1024;
  params.initial_max_streams_bidi = 100;
  params.initial_max_streams_uni = 3; // Control + QPACK enc + QPACK dec
  params.max_datagram_frame_size = 65535;
  params.max_idle_timeout = 30 * NGTCP2_SECONDS;
  params.stateless_reset_token_present = 1;
  params.active_connection_id_limit = 7;
  params.grease_quic_bit = 1;

  // Set original DCID - this is what the client sent as Destination CID in
  // their Initial
  if (ocid_.datalen > 0) {
    // Retry case: ocid_ is the original DCID before retry
    params.original_dcid = ocid_;
    // retry_scid is what the client received in Retry (the
    // original_client_dcid)
    params.retry_scid = original_client_dcid_;
    params.retry_scid_present = 1;
  } else {
    // Normal case: original_client_dcid_ is what client sent as destination
    params.original_dcid = original_client_dcid_;
  }
  params.original_dcid_present = 1;

  // Generate stateless reset token
  if (ngtcp2_crypto_generate_stateless_reset_token(
          params.stateless_reset_token, g_static_secret.data(),
          g_static_secret.size(), &scid_) != 0) {
    NPRPC_LOG_ERROR("[HTTP/3][E] Failed to generate stateless reset token");
    return false;
  }

  // Convert endpoints to ngtcp2 path
  sockaddr_storage local_ss, remote_ss;
  memset(&local_ss, 0, sizeof(local_ss));
  memset(&remote_ss, 0, sizeof(remote_ss));

  auto local_ep_data = local_ep_.data();
  auto remote_ep_data = remote_ep_.data();
  memcpy(&local_ss, local_ep_data, local_ep_.size());
  memcpy(&remote_ss, remote_ep_data, remote_ep_.size());

  ngtcp2_path path{
      .local =
          {
              .addr = reinterpret_cast<sockaddr*>(&local_ss),
              .addrlen = static_cast<ngtcp2_socklen>(local_ep_.size()),
          },
      .remote =
          {
              .addr = reinterpret_cast<sockaddr*>(&remote_ss),
              .addrlen = static_cast<ngtcp2_socklen>(remote_ep_.size()),
          },
  };

  // Create server connection
  int rv =
      ngtcp2_conn_server_new(&conn_, &dcid_, &scid_, &path, version_,
                             &callbacks, &settings, &params, nullptr, this);
  if (rv != 0) {
    NPRPC_LOG_ERROR("[HTTP/3][E] ngtcp2_conn_server_new failed: {}",
                    ngtcp2_strerror(rv));
    return false;
  }

  // See example in
  // /home/nikita/projects/nprpc/third_party/ngtcp2/examples/tls_server_session_ossl.cc

  // Create SSL object
  SSL* ssl = SSL_new(ssl_ctx_);
  if (!ssl) {
    NPRPC_LOG_ERROR("[HTTP/3][E] SSL_new failed: {}",
                    ERR_error_string(ERR_get_error(), nullptr));
    return false;
  }

  ngtcp2_crypto_ossl_ctx_set_ssl(ossl_ctx_, ssl);

  // Configure SSL for QUIC server FIRST - this sets up the QUIC TLS callbacks
  if (ngtcp2_crypto_ossl_configure_server_session(ssl) != 0) {
    NPRPC_LOG_ERROR("[HTTP/3][E] Failed to configure server SSL session");
    SSL_free(ssl);
    return false;
  }

  // Set app data AFTER configuring for QUIC (reference order)
  SSL_set_app_data(ssl, &conn_ref_);
  SSL_set_accept_state(ssl);
  SSL_set_quic_tls_early_data_enabled(ssl, 1);

  ngtcp2_conn_set_tls_native_handle(conn_, ossl_ctx_);

  print_ssl_state("After SSL setup");

  NPRPC_HTTP3_TRACE("Connection initialized for {}",
                    remote_ep_.address().to_string());

  return true;
}

int Http3Connection::on_read(const uint8_t* data,
                             size_t len,
                             const ngtcp2_pkt_info* pi)
{
  if (closed_) {
    return -1;
  }

  print_ssl_state("on_read start");

  // If we're closing, just send connection close
  if (ngtcp2_conn_in_closing_period(conn_)) {
    NPRPC_HTTP3_TRACE(
        "Connection is in closing period, resend cached CONNECTION_CLOSE");
    if (!conn_close_buf_.empty()) {
      server_->send_packet(remote_ep_, conn_close_buf_.data(),
                           conn_close_buf_.size());
    }
    return 0;
  }

  if (ngtcp2_conn_in_draining_period(conn_)) {
    NPRPC_HTTP3_TRACE(
        "Connection is in draining period, ignoring received packet");
    return 0;
  }

  // Convert endpoints to ngtcp2 path
  sockaddr_storage local_ss, remote_ss;
  memset(&local_ss, 0, sizeof(local_ss));
  memset(&remote_ss, 0, sizeof(remote_ss));

  auto local_ep_data = local_ep_.data();
  auto remote_ep_data = remote_ep_.data();
  memcpy(&local_ss, local_ep_data, local_ep_.size());
  memcpy(&remote_ss, remote_ep_data, remote_ep_.size());

  ngtcp2_path path{
      .local =
          {
              .addr = reinterpret_cast<sockaddr*>(&local_ss),
              .addrlen = static_cast<ngtcp2_socklen>(local_ep_.size()),
          },
      .remote =
          {
              .addr = reinterpret_cast<sockaddr*>(&remote_ss),
              .addrlen = static_cast<ngtcp2_socklen>(remote_ep_.size()),
          },
  };

  NPRPC_HTTP3_TRACE("Reading packet, len={}", len);

  int rv = ngtcp2_conn_read_pkt(conn_, &path, pi, data, len, timestamp_ns());
  if (rv != 0) {
    switch (rv) {
    case NGTCP2_ERR_DRAINING:
      // Normal connection close - client is done, enter draining period
      NPRPC_HTTP3_TRACE("Connection entering draining period (client closed)");
      start_draining_period();
      return 0;
    case NGTCP2_ERR_DROP_CONN:
      // Silently drop connection (e.g., stateless reset)
      NPRPC_HTTP3_TRACE("Dropping connection");
      closed_ = true;
      return -1;
    case NGTCP2_ERR_CRYPTO:
      NPRPC_LOG_ERROR("[HTTP/3][E] ngtcp2_conn_read_pkt: {}",
                      ngtcp2_strerror(rv));
      if (!last_error_.error_code) {
        auto alert = ngtcp2_conn_get_tls_alert(conn_);
        NPRPC_LOG_ERROR("[HTTP/3][E] TLS alert: {}", (int)alert);
        ngtcp2_ccerr_set_tls_alert(&last_error_, alert, nullptr, 0);
      }
      break;
    default:
      NPRPC_LOG_ERROR("[HTTP/3][E] ngtcp2_conn_read_pkt: {}",
                      ngtcp2_strerror(rv));
      // Print OpenSSL errors for unexpected failures
      unsigned long err;
      while ((err = ERR_get_error()) != 0) {
        NPRPC_LOG_ERROR("[HTTP/3][E] OpenSSL error: {}",
                        ERR_error_string(err, nullptr));
      }
      if (!last_error_.error_code) {
        ngtcp2_ccerr_set_liberr(&last_error_, rv, nullptr, 0);
      }
    }
    return handle_error();
  }

  NPRPC_HTTP3_TRACE("Packet processed successfully, handshake_completed={}",
                    ngtcp2_conn_get_handshake_completed(conn_));

  print_ssl_state("on_read end");

  schedule_timer();

  // Send any pending data (handshake responses, etc.)
  on_write();

  return 0;
}

int Http3Connection::on_write()
{
  if (closed_) {
    return -1;
  }

  if (ngtcp2_conn_in_closing_period(conn_) ||
      ngtcp2_conn_in_draining_period(conn_)) {
    return 0;
  }

  NPRPC_HTTP3_TRACE("on_write called");

  ngtcp2_path_storage ps;
  ngtcp2_pkt_info pi;

  ngtcp2_path_storage_zero(&ps);

  // Write loop
  for (;;) {
    int64_t stream_id = -1;
    int fin = 0;
    std::array<nghttp3_vec, 16> vec;
    ngtcp2_vec raw_vec{};
    nghttp3_ssize sveccnt = 0;
    bool using_http_stream = false;

    // Get data from HTTP/3 layer if available
    if (httpconn_ && ngtcp2_conn_get_max_data_left(conn_)) {
      sveccnt = nghttp3_conn_writev_stream(httpconn_, &stream_id, &fin,
                                           vec.data(), vec.size());
      if (sveccnt < 0) {
        NPRPC_LOG_ERROR("[HTTP/3][E] nghttp3_conn_writev_stream: {}",
                        nghttp3_strerror(static_cast<int>(sveccnt)));
        ngtcp2_ccerr_set_application_error(
            &last_error_,
            nghttp3_err_infer_quic_app_error_code(static_cast<int>(sveccnt)),
            nullptr, 0);
        return handle_error();
      }

      using_http_stream = sveccnt > 0 || stream_id != -1 || fin != 0;
    }

    Http3Stream* raw_stream = nullptr;
    if (!using_http_stream) {
      raw_stream = next_raw_writable_stream();
      if (raw_stream) {
        stream_id = raw_stream->stream_id;
        auto& chunk = raw_stream->raw_write_queue.front();
        raw_vec.base = chunk.data_ptr();
        raw_vec.len = chunk.size();
        sveccnt = 1;
      }
    }

    ngtcp2_ssize ndatalen;
    uint32_t flags =
        NGTCP2_WRITE_STREAM_FLAG_MORE | NGTCP2_WRITE_STREAM_FLAG_PADDING;
    if (fin) {
      flags |= NGTCP2_WRITE_STREAM_FLAG_FIN;
    }

    NPRPC_HTTP3_TRACE("on_write: calling ngtcp2_conn_writev_stream, "
                      "stream_id={}, sveccnt={}",
                      stream_id, sveccnt);

    auto ts = timestamp_ns();
    auto nwrite = ngtcp2_conn_writev_stream(
        conn_, &ps.path, &pi, send_buf_.data(), MAX_UDP_PAYLOAD_SIZE, &ndatalen,
      flags, stream_id,
      using_http_stream ? reinterpret_cast<const ngtcp2_vec*>(vec.data())
                : &raw_vec,
        static_cast<size_t>(sveccnt), ts);

    NPRPC_HTTP3_TRACE("on_write: ngtcp2_conn_writev_stream returned "
                      "nwrite={}, ndatalen={}",
                      nwrite, ndatalen);

    if (nwrite < 0) {
      switch (nwrite) {
      case NGTCP2_ERR_STREAM_DATA_BLOCKED:
        if (using_http_stream && httpconn_) {
          nghttp3_conn_block_stream(httpconn_, stream_id);
        } else if (raw_stream) {
          schedule_raw_writable_stream(raw_stream);
          break;
        }
        continue;
      case NGTCP2_ERR_STREAM_SHUT_WR:
        if (using_http_stream && httpconn_) {
          nghttp3_conn_shutdown_stream_write(httpconn_, stream_id);
        } else if (raw_stream) {
          raw_stream->raw_write_queue.clear();
        }
        continue;
      case NGTCP2_ERR_WRITE_MORE:
        if (using_http_stream && httpconn_ && ndatalen >= 0) {
          auto rv = nghttp3_conn_add_write_offset(
              httpconn_, stream_id, static_cast<size_t>(ndatalen));
          if (rv != 0) {
            NPRPC_HTTP3_TRACE("nghttp3_conn_add_write_offset: {}",
                              nghttp3_strerror(rv));
            ngtcp2_ccerr_set_application_error(
                &last_error_, nghttp3_err_infer_quic_app_error_code(rv),
                nullptr, 0);
            return handle_error();
          }
        } else if (!using_http_stream && raw_stream) {
          if (ndatalen > 0) {
            raw_stream->raw_write_queue.front().consume(
                static_cast<size_t>(ndatalen));

            while (!raw_stream->raw_write_queue.empty() &&
                   raw_stream->raw_write_queue.front().size() == 0) {
              raw_stream->raw_write_queue.pop_front();
            }
          }

          if (!raw_stream->raw_write_queue.empty()) {
            schedule_raw_writable_stream(raw_stream);
          }
        }
        continue;
      }

      NPRPC_HTTP3_ERROR("ngtcp2_conn_writev_stream: {}",
                        ngtcp2_strerror(static_cast<int>(nwrite)));
      ngtcp2_ccerr_set_liberr(&last_error_, static_cast<int>(nwrite), nullptr,
                              0);
      return handle_error();
    }

    if (using_http_stream && ndatalen >= 0 && httpconn_) {
      auto rv = nghttp3_conn_add_write_offset(httpconn_, stream_id,
                                              static_cast<size_t>(ndatalen));
      if (rv != 0) {
        NPRPC_HTTP3_ERROR("nghttp3_conn_add_write_offset: {}",
                          nghttp3_strerror(rv));
        ngtcp2_ccerr_set_application_error(
            &last_error_, nghttp3_err_infer_quic_app_error_code(rv), nullptr,
            0);
        return handle_error();
      }
    } else if (!using_http_stream && raw_stream) {
      if (ndatalen > 0) {
        raw_stream->raw_write_queue.front().consume(static_cast<size_t>(ndatalen));

        while (!raw_stream->raw_write_queue.empty() &&
               raw_stream->raw_write_queue.front().size() == 0) {
          raw_stream->raw_write_queue.pop_front();
        }
      }

      if (!raw_stream->raw_write_queue.empty()) {
        schedule_raw_writable_stream(raw_stream);
      }
    }

    if (nwrite == 0) {
      NPRPC_HTTP3_TRACE("on_write: no more packets to write");
      break; // Nothing more to write
    }

    // Send the packet
    // Check what type of packet we're sending
#if NPRPC_ENABLE_HTTP3_TRACE
    uint8_t first_byte = send_buf_[0];
    const char* pkt_type = "unknown";
    if (first_byte & 0x80) { // Long header
      uint8_t type = (first_byte >> 4) & 0x03;
      switch (type) {
      case 0:
        pkt_type = "Initial";
        break;
      case 1:
        pkt_type = "0-RTT";
        break;
      case 2:
        pkt_type = "Handshake";
        break;
      case 3:
        pkt_type = "Retry";
        break;
      }
    } else {
      pkt_type = "Short (1-RTT)";
    }
    NPRPC_HTTP3_TRACE("on_write: sending {} bytes, type={}", nwrite, pkt_type);
#endif
    server_->send_packet(remote_ep_, send_buf_.data(),
                         static_cast<size_t>(nwrite));
  }

  ngtcp2_conn_update_pkt_tx_time(conn_, timestamp_ns());
  schedule_timer();

  return 0;
}

void Http3Connection::enqueue_packet(flat_buffer&& data,
                                     ngtcp2_pkt_info pi)
{
  boost::asio::post(
      strand_, [self = shared_from_this(), packet = std::move(data), pi]() {
        if (self->closed_) {
          return;
        }

        if (self->on_read(packet.data_ptr(), packet.size(), &pi) == 0) {
          self->on_write();
        }
      });
}

int Http3Connection::handle_expiry()
{
  auto now = timestamp_ns();
  int rv = ngtcp2_conn_handle_expiry(conn_, now);
  if (rv != 0) {
    NPRPC_HTTP3_ERROR("ngtcp2_conn_handle_expiry: {}", ngtcp2_strerror(rv));
    ngtcp2_ccerr_set_liberr(&last_error_, rv, nullptr, 0);
    return handle_error();
  }
  return 0;
}

void Http3Connection::schedule_timer()
{
  auto expiry = ngtcp2_conn_get_expiry(conn_);
  auto now = timestamp_ns();

  if (expiry <= now) {
    // Already expired, handle immediately
    boost::asio::post(strand_, [self = shared_from_this()]() {
      if (!self->closed_) {
        self->handle_expiry();
        self->on_write();
      }
    });
    return;
  }

  auto timeout = std::chrono::nanoseconds(expiry - now);
  timer_.expires_after(timeout);
  timer_.async_wait(boost::asio::bind_executor(
      strand_, [self = shared_from_this()](boost::system::error_code ec) {
        if (ec || self->closed_) {
          return;
        }
        self->handle_expiry();
        self->on_write();
      }));
}

void Http3Connection::signal_write()
{
  boost::asio::post(strand_, [self = shared_from_this()]() {
    if (!self->closed_) {
      self->on_write();
    }
  });
}

int Http3Connection::setup_httpconn()
{
  if (httpconn_) {
    return 0;
  }

  // Need at least 3 unidirectional streams for HTTP/3
  if (ngtcp2_conn_get_streams_uni_left(conn_) < 3) {
    NPRPC_HTTP3_ERROR("Peer does not allow 3 unidirectional streams");
    return -1;
  }

  nghttp3_callbacks callbacks{};
  callbacks.acked_stream_data = http_acked_stream_data_cb;
  callbacks.recv_data = http_recv_data_cb;
  callbacks.deferred_consume = http_deferred_consume_cb;
  callbacks.begin_headers = http_begin_headers_cb;
  callbacks.recv_header = http_recv_header_cb;
  callbacks.end_headers = http_end_headers_cb;
  callbacks.end_stream = http_end_stream_cb;
  callbacks.stop_sending = http_stop_sending_cb;
  callbacks.reset_stream = http_reset_stream_cb;

  nghttp3_settings settings;
  nghttp3_settings_default(&settings);
  settings.qpack_max_dtable_capacity = 4096;
  settings.qpack_blocked_streams = 100;
  settings.enable_connect_protocol = 1;
  settings.h3_datagram = 1;

  NPRPC_HTTP3_TRACE("[HTTP/3] Advertising SETTINGS: extended_connect=1 h3_datagram=1 wt_enabled=1");

  auto mem = nghttp3_mem_default();

  int rv =
      nghttp3_conn_server_new(&httpconn_, &callbacks, &settings, mem, this);
  if (rv != 0) {
    NPRPC_HTTP3_ERROR("nghttp3_conn_server_new: {}", nghttp3_strerror(rv));
    return -1;
  }

  auto params = ngtcp2_conn_get_local_transport_params(conn_);
  nghttp3_conn_set_max_client_streams_bidi(httpconn_,
                                           params->initial_max_streams_bidi);

  // Open control stream
  int64_t ctrl_stream_id;
  rv = ngtcp2_conn_open_uni_stream(conn_, &ctrl_stream_id, nullptr);
  if (rv != 0) {
    NPRPC_HTTP3_ERROR("Failed to open control stream: {}", ngtcp2_strerror(rv));
    return -1;
  }

  rv = nghttp3_conn_bind_control_stream(httpconn_, ctrl_stream_id);
  if (rv != 0) {
    NPRPC_HTTP3_ERROR("nghttp3_conn_bind_control_stream: {}", nghttp3_strerror(rv));
    return -1;
  }

  // Open QPACK streams
  int64_t qpack_enc_stream_id, qpack_dec_stream_id;

  rv = ngtcp2_conn_open_uni_stream(conn_, &qpack_enc_stream_id, nullptr);
  if (rv != 0) {
    NPRPC_HTTP3_ERROR("Failed to open QPACK encoder stream: {}", ngtcp2_strerror(rv));
    return -1;
  }

  rv = ngtcp2_conn_open_uni_stream(conn_, &qpack_dec_stream_id, nullptr);
  if (rv != 0) {
    NPRPC_HTTP3_ERROR("Failed to open QPACK decoder stream: {}", ngtcp2_strerror(rv));
    return -1;
  }

  rv = nghttp3_conn_bind_qpack_streams(httpconn_, qpack_enc_stream_id,
                                       qpack_dec_stream_id);
  if (rv != 0) {
    NPRPC_HTTP3_ERROR("nghttp3_conn_bind_qpack_streams: {}", nghttp3_strerror(rv));
    return -1;
  }

  NPRPC_HTTP3_TRACE("HTTP/3 connection setup complete");

  // Flush the control/QPACK streams promptly so the peer can receive SETTINGS
  // and continue with WebTransport session establishment.
  signal_write();

  return 0;
}

Http3Stream* Http3Connection::find_stream(int64_t stream_id)
{
  auto it = streams_.find(stream_id);
  return it != streams_.end() ? it->second.get() : nullptr;
}

Http3Stream* Http3Connection::create_stream(int64_t stream_id)
{
  auto stream = std::make_unique<Http3Stream>();
  stream->stream_id = stream_id;
  auto* ptr = stream.get();
  streams_[stream_id] = std::move(stream);
  return ptr;
}

void Http3Connection::remove_stream(int64_t stream_id)
{
  webtransport_session_ids_.erase(stream_id);
  webtransport_control_sessions_.erase(stream_id);
  streams_.erase(stream_id);
}

void Http3Connection::schedule_raw_writable_stream(Http3Stream* stream)
{
  if (!stream || stream->raw_write_queue.empty() || stream->raw_write_scheduled) {
    return;
  }

  const auto priority = static_cast<size_t>(raw_write_priority(stream));
  raw_writable_streams_[priority].push_back(stream->stream_id);
  stream->raw_write_scheduled = true;
}

RawWritePriority Http3Connection::raw_write_priority(const Http3Stream* stream)
{
  if (!stream) {
    return RawWritePriority::Default;
  }

  switch (stream->webtransport_child_binding) {
  case WebTransportChildBinding::Control:
    return RawWritePriority::Control;
  case WebTransportChildBinding::Native:
    return RawWritePriority::Native;
  case WebTransportChildBinding::Unbound:
  default:
    return RawWritePriority::Default;
  }
}

Http3Stream* Http3Connection::next_raw_writable_stream()
{
  for (auto& queue : raw_writable_streams_) {
    while (!queue.empty()) {
      const auto stream_id = queue.front();
      queue.pop_front();

      auto* stream = find_stream(stream_id);
      if (!stream) {
        continue;
      }

      stream->raw_write_scheduled = false;
      if (stream->raw_write_queue.empty()) {
        continue;
      }

      return stream;
    }
  }

  return nullptr;
}

int Http3Connection::recv_stream_data(uint32_t flags,
                                      int64_t stream_id,
                                      const uint8_t* data,
                                      size_t datalen)
{
  auto* stream = find_stream(stream_id);
  if (!stream) {
    stream = create_stream(stream_id);
  }

  const bool should_probe_webtransport =
      stream->webtransport_child_stream ||
      (!stream->http_stream && ngtcp2_is_bidi_stream(stream_id) &&
       !ngtcp2_conn_is_local_stream(conn_, stream_id) &&
       !webtransport_session_ids_.empty());

  if (should_probe_webtransport) {
    if (handle_webtransport_stream_data(stream, flags, data, datalen) != 0) {
      return -1;
    }
    if (stream->webtransport_child_stream) {
      ngtcp2_conn_extend_max_stream_offset(conn_, stream_id,
                                           static_cast<uint64_t>(datalen));
      ngtcp2_conn_extend_max_offset(conn_, static_cast<uint64_t>(datalen));
      return 0;
    }
  }

  if (!httpconn_) {
    return 0;
  }

  auto nconsumed =
      nghttp3_conn_read_stream2(httpconn_, stream_id, data, datalen,
                                (flags & NGTCP2_STREAM_DATA_FLAG_FIN) != 0,
                                ngtcp2_conn_get_timestamp(conn_));
  if (nconsumed < 0) {
    NPRPC_LOG_ERROR("[HTTP/3][E] nghttp3_conn_read_stream: {}",
                    nghttp3_strerror(static_cast<int>(nconsumed)));
    ngtcp2_ccerr_set_application_error(
        &last_error_,
        nghttp3_err_infer_quic_app_error_code(static_cast<int>(nconsumed)),
        nullptr, 0);
    return -1;
  }

  ngtcp2_conn_extend_max_stream_offset(conn_, stream_id,
                                       static_cast<uint64_t>(nconsumed));
  ngtcp2_conn_extend_max_offset(conn_, static_cast<uint64_t>(nconsumed));

  return 0;
}

int Http3Connection::acked_stream_data_offset(int64_t stream_id,
                                              uint64_t datalen)
{
  if (!httpconn_) {
    return 0;
  }

  int rv = nghttp3_conn_add_ack_offset(httpconn_, stream_id, datalen);
  if (rv != 0) {
    NPRPC_HTTP3_ERROR("nghttp3_conn_add_ack_offset: {}", nghttp3_strerror(rv));
    return -1;
  }

  return 0;
}

void Http3Connection::extend_max_remote_streams_bidi(uint64_t max_streams)
{
  if (httpconn_) {
    nghttp3_conn_set_max_client_streams_bidi(httpconn_, max_streams);
  }
}

void Http3Connection::extend_max_stream_data(int64_t stream_id,
                                             uint64_t max_data)
{
  auto* stream = find_stream(stream_id);
  if (stream && !stream->raw_write_queue.empty()) {
    signal_write();
  }

  if (httpconn_) {
    nghttp3_conn_unblock_stream(httpconn_, stream_id);
  }
}

// HTTP/3 header handling
void Http3Connection::http_begin_headers(int64_t stream_id)
{
  auto* stream = find_stream(stream_id);
  if (stream) {
    stream->http_stream = true;
    nghttp3_conn_set_stream_user_data(httpconn_, stream_id, stream);
  }
}

void Http3Connection::http_recv_header(Http3Stream* stream,
                                       int32_t token,
                                       nghttp3_rcbuf* name,
                                       nghttp3_rcbuf* value)
{
  auto v = nghttp3_rcbuf_get_buf(value);
  auto n = nghttp3_rcbuf_get_buf(name);

  // Store all headers for SSR
  std::string header_name(reinterpret_cast<const char*>(n.base), n.len);
  std::string header_value(reinterpret_cast<const char*>(v.base), v.len);
  stream->headers[header_name] = header_value;

  switch (token) {
  case NGHTTP3_QPACK_TOKEN__PATH:
    stream->path = std::move(header_value);
    break;
  case NGHTTP3_QPACK_TOKEN__METHOD:
    stream->method = std::move(header_value);
    break;
  case NGHTTP3_QPACK_TOKEN__AUTHORITY:
    stream->authority = std::move(header_value);
    break;
  default:
    if (header_name == ":scheme") {
      stream->scheme = header_value;
    } else if (header_name == ":protocol") {
      stream->protocol = header_value;
    }
    break;
  case NGHTTP3_QPACK_TOKEN_CONTENT_TYPE:
    stream->content_type = std::move(header_value);
    break;
  case NGHTTP3_QPACK_TOKEN_CONTENT_LENGTH:
    stream->content_length = std::stoull(header_value);
    break;
  case NGHTTP3_QPACK_TOKEN_ACCEPT:
    stream->accept = std::move(header_value);
    break;
  }
}

int Http3Connection::http_end_headers(Http3Stream* stream)
{
  NPRPC_HTTP3_TRACE("Request: {} {}", stream->method, stream->path);

  // Extended CONNECT requests do not terminate the request stream with FIN.
  // WebTransport expects the 200 response immediately after headers.
  if (stream->method == "CONNECT" && stream->path == k_webtransport_path) {
    return start_response(stream);
  }

  return 0;
}

int Http3Connection::http_end_stream(Http3Stream* stream)
{
  return start_response(stream);
}

void Http3Connection::http_stream_close(int64_t stream_id,
                                        uint64_t app_error_code)
{
  auto* stream = find_stream(stream_id);
  if (stream) {
    NPRPC_HTTP3_DEBUG(
        "conn={} stream_close stream_id={} path='{}' app_error_code={} response_len={} response_offset={} webtransport_child={} webtransport_session={} wt_session_id={} wt_binding={}",
        debug_id(), stream_id, stream->path, app_error_code, stream->response_len,
        stream->response_offset, stream->webtransport_child_stream,
        stream->webtransport_session, stream->webtransport_session_id,
        static_cast<int>(stream->webtransport_child_binding));
  } else {
    NPRPC_HTTP3_DEBUG(
        "conn={} stream_close stream_id={} app_error_code={} (no stream state)",
        debug_id(), stream_id, app_error_code);
  }

  if (stream) {
    auto session_it = webtransport_control_sessions_.find(stream->webtransport_session_id);
    if (stream->webtransport_session && session_it != webtransport_control_sessions_.end()) {
      session_it->second->shutdown();
      webtransport_control_sessions_.erase(session_it);
    } else if (session_it != webtransport_control_sessions_.end()) {
      session_it->second->on_transport_stream_closed(stream_id);
    }
  }

  if (!ngtcp2_is_bidi_stream(stream_id)) {
    return;
  }

  if (!ngtcp2_conn_is_local_stream(conn_, stream_id)) {
    ngtcp2_conn_extend_max_streams_bidi(conn_, 1);
  }

  remove_stream(stream_id);
}

int Http3Connection::start_response(Http3Stream* stream)
{
  if (stream->response_started) {
    return 0;
  }

  stream->response_started = true;

  if (stream->method == "CONNECT" && stream->path == k_webtransport_path &&
      (stream->protocol == "webtransport-h3" ||
       stream->protocol == "webtransport")) {
    NPRPC_HTTP3_TRACE("Received WebTransport CONNECT stream_id={} authority='{}' scheme='{}' origin='{}'",
                      stream->stream_id, stream->authority, stream->scheme, stream->headers["origin"]);
    return handle_webtransport_connect(stream);
  }

  // Handle the HTTP request
  if (stream->method == "GET" || stream->method == "HEAD") {
#ifdef NPRPC_SSR_ENABLED
    // Check if this request should be handled by SSR
    if (g_cfg.ssr_enabled &&
        nprpc::impl::should_ssr(stream->method, stream->path, stream->accept)) {
      NPRPC_HTTP3_TRACE("Forwarding to SSR: {} {}", stream->method,
                        stream->path);

      // Build full URL
      std::string url =
          std::string("https://") + stream->authority + stream->path;

      // Filter out HTTP/2 pseudo-headers (start with ':') for Web API
      // compatibility
      std::map<std::string, std::string> filtered_headers;
      for (const auto& [key, value] : stream->headers) {
        if (!key.empty() && key[0] != ':') {
          filtered_headers[key] = value;
        }
      }

      // Forward to SSR (synchronous call)
      auto ssr_response =
          nprpc::impl::forward_to_ssr(stream->method, url, filtered_headers,
                                      "", // No body for GET/HEAD
                                      remote_ep_.address().to_string());

      if (ssr_response) {
        NPRPC_HTTP3_TRACE("SSR response: {} ({} bytes)",
                          ssr_response->status_code, ssr_response->body.size());

        // Determine content type from SSR response headers
        std::string content_type = "text/html; charset=utf-8";
        for (const auto& [key, value] : ssr_response->headers) {
          if (key == "content-type" || key == "Content-Type") {
            content_type = value;
            break;
          }
        }

        return send_dynamic_response(stream, ssr_response->status_code,
                                     content_type,
                                     std::move(ssr_response->body));
      } else {
        NPRPC_HTTP3_ERROR("SSR failed, falling back to static file");
        // Fall through to static file serving
      }
    }
#endif

    // Serve static file
    std::string request_path = stream->path;

    // Check if path is a directory or handle index.html
    if (request_path == "/" || request_path.empty()) {
      request_path = "/index.html";
    }

    auto file_path = resolve_http_doc_root_path(g_cfg.http_root_dir,
                                                request_path);
    if (!file_path) {
      NPRPC_HTTP3_ERROR("Rejected static path outside root: {} (root={})",
                        request_path, g_cfg.http_root_dir);
      return send_static_response(stream, 404, "text/html",
                                  "<!DOCTYPE html><html><body><h1>404 "
                                  "Not Found</h1></body></html>");
    }

    NPRPC_HTTP3_TRACE("Serving file: {} (root={}, path={})", file_path->string(),
                      g_cfg.http_root_dir, request_path);

    // Get file from cache (zero-copy)
    auto cached_file = get_file_cache().get(*file_path);
    if (!cached_file) {
      // 404 Not Found
      NPRPC_HTTP3_ERROR("File not found: {} (path={})", file_path->string(), request_path);
      return send_static_response(stream, 404, "text/html",
                                  "<!DOCTYPE html><html><body><h1>404 "
                                  "Not Found</h1></body></html>");
    }

    NPRPC_HTTP3_TRACE("File size: {} (from cache)", cached_file->size());

    // For HEAD requests, we still need headers but no body
    if (stream->method == "HEAD") {
      // Create empty body response with correct content-length from cache
      return send_static_response(stream, 200, cached_file->content_type(), {});
    }

    // Use zero-copy response
    return send_cached_response(stream, 200, std::move(cached_file));
  } else if (stream->method == "OPTIONS") {
    if (is_rpc_http_target(stream->path)) {
      return send_cors_preflight(stream);
    }
    return send_static_response(stream, 405, "text/html",
                                "<!DOCTYPE html><html><body><h1>405 Method Not "
                                "Allowed</h1></body></html>");
  } else if (stream->method == "POST") { // Handle POST request (e.g., RPC)
    // Handle RPC requests (POST to /rpc)
    if (is_rpc_http_target(stream->path)) {
      NPRPC_HTTP3_TRACE("Handling RPC request");
      return handle_rpc_request(stream);
    }

#ifdef NPRPC_SSR_ENABLED
    // Check if this is a SvelteKit form action (POST with ?/ in path)
    if (g_cfg.ssr_enabled &&
        nprpc::impl::should_ssr(stream->method, stream->path, stream->accept)) {
      NPRPC_HTTP3_TRACE("Forwarding POST to SSR: {}", stream->path);

      // Build full URL
      std::string url =
          std::string("https://") + stream->authority + stream->path;

      // Filter out HTTP/2 pseudo-headers
      std::map<std::string, std::string> filtered_headers;
      for (const auto& [key, value] : stream->headers) {
        if (!key.empty() && key[0] != ':') {
          filtered_headers[key] = value;
        }
      }

      // Get request body as string
        std::string body_str(
          reinterpret_cast<const char*>(stream->request_body.data_ptr()),
          stream->request_body.size());

      // Forward to SSR
      auto ssr_response = nprpc::impl::forward_to_ssr(
          stream->method, url, filtered_headers, body_str,
          remote_ep_.address().to_string());

      if (ssr_response) {
        NPRPC_HTTP3_TRACE("SSR POST response: {} ({} bytes)",
                          ssr_response->status_code, ssr_response->body.size());

        std::string content_type = "text/html; charset=utf-8";
        for (const auto& [key, value] : ssr_response->headers) {
          if (key == "content-type" || key == "Content-Type") {
            content_type = value;
            break;
          }
        }

        return send_dynamic_response(stream, ssr_response->status_code,
                                     content_type,
                                     std::move(ssr_response->body));
      }
      // Fall through to default response on error
    }
#endif

    // Default: return 200 OK for other POST requests
    return send_static_response(stream, 200, "text/plain", "OK");
  } else {
    // Method not allowed
    return send_static_response(stream, 405, "text/html",
                                "<!DOCTYPE html><html><body><h1>405 Method Not "
                                "Allowed</h1></body></html>");
  }
}

int Http3Connection::send_cached_response(Http3Stream* stream,
                                          unsigned int status_code,
                                          CachedFileGuard cached_file)
{
  NPRPC_HTTP3_TRACE(
      "Sending cached response: {} Content-Type: {} Body length: {}",
      status_code, cached_file->content_type(), cached_file->size());

  // Store cached file guard in stream - this keeps the file pinned
  // The guard's acquire() was called when it was created, and release()
  // will be called when the stream is destroyed after transfer completes
  stream->cached_file = std::move(cached_file);

  // Zero-copy: point directly to cached file data
  stream->response_data = stream->cached_file->data();
  stream->response_len = stream->cached_file->size();
  stream->response_offset = 0;
    auto& headers = make_response_headers(stream, status_code,
                      stream->cached_file->content_type(),
                      stream->response_len, false);

  nghttp3_data_reader dr{
      .read_data = http_read_data_cb,
  };

    log_http3_response_submit("cached", stream, status_code,
                stream->cached_file->content_type(),
                            stream->response_len);

  int rv = nghttp3_conn_submit_response(httpconn_, stream->stream_id,
                      headers.headers.data(),
                      headers.headers.size(), &dr);
  if (rv != 0) {
    NPRPC_HTTP3_ERROR("nghttp3_conn_submit_response: {}", nghttp3_strerror(rv));
    return -1;
  }

  return 0;
}

//==============================================================================
// RPC Handling
//==============================================================================
int Http3Connection::handle_rpc_request(Http3Stream* stream)
{
  if (stream->request_body.size() == 0)
    return send_static_response(stream, 400, "text/plain",
                                "Empty request body");

  try {
    flat_buffer response_body;

    if (!process_http_rpc(server_->io_context(), std::move(stream->request_body),
                          response_body))
      return send_static_response(stream, 500, "text/plain",
                                  "RPC processing failed");

    return send_dynamic_response(stream, 200, "application/octet-stream",
                                 std::move(response_body));
  } catch (const std::exception& e) {
    NPRPC_LOG_ERROR("[HTTP/3][E] RPC exception: {}", e.what());
    return send_dynamic_response(stream, 500, "text/plain", e.what());
  }
}

PreparedResponseHeaders&
Http3Connection::make_response_headers(Http3Stream* stream,
                                       unsigned int status_code,
                                       std::string_view content_type,
                                       size_t content_length,
                                       bool include_cors,
                                       bool preflight)
{
  auto& prepared = stream->response_headers;
  prepared.headers.clear();
  prepared.status = std::to_string(status_code);
  prepared.content_length = std::to_string(content_length);
  prepared.content_type = std::string(content_type);
  prepared.allow_origin.clear();
  prepared.headers.reserve(include_cors ? 9 : 4);

  auto add = [&](const char* name,
                 size_t namelen,
                 const uint8_t* value,
                 size_t valuelen,
                 uint8_t flags) {
    prepared.headers.push_back({
        .name = reinterpret_cast<uint8_t*>(const_cast<char*>(name)),
        .value = const_cast<uint8_t*>(value),
        .namelen = namelen,
        .valuelen = valuelen,
        .flags = flags,
    });
  };

  auto add_sv = [&](const char* name,
                    size_t namelen,
                    std::string_view value,
                    uint8_t flags) {
    add(name, namelen, reinterpret_cast<const uint8_t*>(value.data()),
        value.size(), flags);
  };

  add_sv(":status", 7, prepared.status,
         NGHTTP3_NV_FLAG_NO_COPY_NAME | NGHTTP3_NV_FLAG_NO_COPY_VALUE);
  add("server", 6,
      reinterpret_cast<const uint8_t*>("nprpc/nghttp3"), 13,
      NGHTTP3_NV_FLAG_NO_COPY_NAME | NGHTTP3_NV_FLAG_NO_COPY_VALUE);

  if (!preflight) {
    add_sv("content-type", 12, prepared.content_type,
           NGHTTP3_NV_FLAG_NO_COPY_NAME | NGHTTP3_NV_FLAG_NO_COPY_VALUE);
    add_sv("content-length", 14, prepared.content_length,
           NGHTTP3_NV_FLAG_NO_COPY_NAME | NGHTTP3_NV_FLAG_NO_COPY_VALUE);
  }

  if (include_cors) {
    const auto origin_it = stream->headers.find("origin");
    const auto allowed_origin =
      origin_it != stream->headers.end()
        ? get_allowed_http_origin(origin_it->second)
        : std::optional<std::string_view>{};

    if (allowed_origin) {
      prepared.allow_origin = std::string(*allowed_origin);
      add_sv("access-control-allow-origin", 27, prepared.allow_origin,
         NGHTTP3_NV_FLAG_NO_COPY_NAME | NGHTTP3_NV_FLAG_NO_COPY_VALUE);
      add("access-control-allow-credentials", 32,
          reinterpret_cast<const uint8_t*>("true"), 4,
          NGHTTP3_NV_FLAG_NO_COPY_NAME | NGHTTP3_NV_FLAG_NO_COPY_VALUE);
      add("vary", 4, reinterpret_cast<const uint8_t*>("Origin"), 6,
        NGHTTP3_NV_FLAG_NO_COPY_NAME | NGHTTP3_NV_FLAG_NO_COPY_VALUE);
    }

    if (allowed_origin || preflight) {
      add("access-control-allow-methods", 28,
        reinterpret_cast<const uint8_t*>("POST, OPTIONS"), 13,
        NGHTTP3_NV_FLAG_NO_COPY_NAME | NGHTTP3_NV_FLAG_NO_COPY_VALUE);
      add("access-control-allow-headers", 28,
        reinterpret_cast<const uint8_t*>("Content-Type"), 12,
        NGHTTP3_NV_FLAG_NO_COPY_NAME | NGHTTP3_NV_FLAG_NO_COPY_VALUE);
    }

    if (preflight && allowed_origin) {
      add("access-control-max-age", 22,
          reinterpret_cast<const uint8_t*>("86400"), 5,
          NGHTTP3_NV_FLAG_NO_COPY_NAME | NGHTTP3_NV_FLAG_NO_COPY_VALUE);
    }
  }

  return prepared;
}

int Http3Connection::send_cors_preflight(Http3Stream* stream)
{
  const auto origin_it = stream->headers.find("origin");
  const auto allowed_origin =
      origin_it != stream->headers.end()
          ? get_allowed_http_origin(origin_it->second)
          : std::optional<std::string_view>{};
  if (!allowed_origin) {
    return send_static_response(stream, 403, "text/plain", "CORS origin denied");
  }

  auto& headers = make_response_headers(stream, 204, "", 0, true, true);
  nghttp3_data_reader dr{.read_data = http_read_data_cb};
  log_http3_response_submit("cors_preflight", stream, 204, "", 0);
  int rv = nghttp3_conn_submit_response(httpconn_, stream->stream_id,
                                        headers.headers.data(),
                                        headers.headers.size(), &dr);
  if (rv != 0) {
    NPRPC_HTTP3_ERROR("nghttp3_conn_submit_response(OPTIONS): {}", nghttp3_strerror(rv));
    return -1;
  }

  return 0;
}

int Http3Connection::send_static_response(Http3Stream* stream,
                                          unsigned int status_code,
                                          std::string_view content_type,
                                          std::string_view body)
{
  NPRPC_HTTP3_TRACE("Sending response: {} Content-Type: {} Body length: {}",
                    status_code, content_type, body.size());

  // Store response data in stream to keep it alive
  stream->response_data = reinterpret_cast<const uint8_t*>(body.data());
  stream->response_len = body.size();
  stream->response_offset = 0;

    const bool include_cors = stream->path == "/rpc" ||
                stream->path.rfind("/rpc/", 0) == 0;
  auto& headers = make_response_headers(stream, status_code, content_type,
                                        body.size(), include_cors);

  nghttp3_data_reader dr{
      .read_data = http_read_data_cb,
  };

  log_http3_response_submit("static", stream, status_code, content_type,
                            body.size());

  int rv = nghttp3_conn_submit_response(httpconn_, stream->stream_id,
                                        headers.headers.data(),
                                        headers.headers.size(), &dr);
  if (rv != 0) {
    NPRPC_HTTP3_ERROR("nghttp3_conn_submit_response: {}", nghttp3_strerror(rv));
    return -1;
  }

  return 0;
}

int Http3Connection::send_webtransport_connect_response(Http3Stream* stream)
{
  NPRPC_HTTP3_TRACE("Accepting WebTransport CONNECT stream_id={}", stream->stream_id);

  auto& headers = stream->response_headers;
  headers.headers.clear();
  headers.status = "200";
  headers.content_length.clear();
  headers.content_type.clear();
  headers.allow_origin.clear();
  headers.headers.reserve(5);
  headers.headers.push_back({
      .name = reinterpret_cast<uint8_t*>(const_cast<char*>(":status")),
      .value = reinterpret_cast<uint8_t*>(headers.status.data()),
      .namelen = 7,
      .valuelen = headers.status.size(),
      .flags = NGHTTP3_NV_FLAG_NO_COPY_NAME | NGHTTP3_NV_FLAG_NO_COPY_VALUE,
  });
  headers.headers.push_back({
      .name = reinterpret_cast<uint8_t*>(const_cast<char*>("server")),
      .value = reinterpret_cast<uint8_t*>(const_cast<char*>("nprpc/nghttp3")),
      .namelen = 6,
      .valuelen = 13,
      .flags = NGHTTP3_NV_FLAG_NO_COPY_NAME | NGHTTP3_NV_FLAG_NO_COPY_VALUE,
  });

      const auto origin_it = stream->headers.find("origin");
      const auto allowed_origin =
        origin_it != stream->headers.end()
          ? get_allowed_http_origin(origin_it->second)
          : std::optional<std::string_view>{};
      if (allowed_origin) {
      headers.allow_origin = std::string(*allowed_origin);
      headers.headers.push_back({
        .name = reinterpret_cast<uint8_t*>(const_cast<char*>("access-control-allow-origin")),
        .value = reinterpret_cast<uint8_t*>(headers.allow_origin.data()),
        .namelen = 27,
        .valuelen = headers.allow_origin.size(),
        .flags = NGHTTP3_NV_FLAG_NO_COPY_NAME | NGHTTP3_NV_FLAG_NO_COPY_VALUE,
      });
      headers.headers.push_back({
        .name = reinterpret_cast<uint8_t*>(const_cast<char*>("access-control-allow-credentials")),
        .value = reinterpret_cast<uint8_t*>(const_cast<char*>("true")),
        .namelen = 32,
        .valuelen = 4,
        .flags = NGHTTP3_NV_FLAG_NO_COPY_NAME | NGHTTP3_NV_FLAG_NO_COPY_VALUE,
      });
      headers.headers.push_back({
        .name = reinterpret_cast<uint8_t*>(const_cast<char*>("vary")),
        .value = reinterpret_cast<uint8_t*>(const_cast<char*>("Origin")),
        .namelen = 4,
        .valuelen = 6,
        .flags = NGHTTP3_NV_FLAG_NO_COPY_NAME | NGHTTP3_NV_FLAG_NO_COPY_VALUE,
      });
      }

  nghttp3_data_reader dr{.read_data = http_read_data_cb};
  log_http3_response_submit("webtransport_connect", stream, 200, "", 0);
  int rv = nghttp3_conn_submit_response(httpconn_, stream->stream_id,
                                        headers.headers.data(),
                                        headers.headers.size(), &dr);
  if (rv != 0) {
    NPRPC_HTTP3_ERROR("nghttp3_conn_submit_response(CONNECT): {}", nghttp3_strerror(rv));
    return -1;
  }

  return 0;
}

int Http3Connection::handle_webtransport_connect(Http3Stream* stream)
{
  if (stream->scheme != "https" || stream->authority.empty()) {
    NPRPC_HTTP3_ERROR("Rejecting WebTransport CONNECT stream_id={} scheme='{}' authority='{}'",
                      stream->stream_id, stream->scheme, stream->authority);
    return send_static_response(stream, 400, "text/plain",
                                "Invalid WebTransport CONNECT request");
  }

  const auto origin_it = stream->headers.find("origin");
  const auto origin =
      origin_it != stream->headers.end() ? std::string_view{origin_it->second}
                                         : std::string_view{};
  if (!is_allowed_browser_origin(origin, stream->scheme, stream->authority)) {
    NPRPC_HTTP3_ERROR(
        "Rejecting WebTransport CONNECT stream_id={} origin='{}' authority='{}'",
        stream->stream_id, origin, stream->authority);
    return send_static_response(stream, 403, "text/plain",
                                "WebTransport origin denied");
  }

  stream->webtransport_session = true;
  stream->webtransport_session_id = stream->stream_id;
  webtransport_session_ids_.insert(stream->stream_id);

  NPRPC_HTTP3_DEBUG(
      "conn={} wt_connect_accept stream_id={} authority='{}' scheme='{}' protocol='{}' origin='{}'",
      debug_id(), stream->stream_id, stream->authority, stream->scheme,
      stream->protocol,
      stream->headers.contains("origin") ? stream->headers.at("origin") : "");

  return send_webtransport_connect_response(stream);
}

bool Http3Connection::has_webtransport_session(int64_t session_stream_id) const
{
  return webtransport_session_ids_.contains(session_stream_id);
}

std::shared_ptr<WebTransportControlSession>
Http3Connection::get_or_create_webtransport_control_session(int64_t session_stream_id)
{
  auto it = webtransport_control_sessions_.find(session_stream_id);
  if (it != webtransport_control_sessions_.end()) {
    return it->second;
  }

  auto session =
      std::make_shared<WebTransportControlSession>(*this, session_stream_id,
                                                   remote_ep_);
  webtransport_control_sessions_.emplace(session_stream_id, session);
  return session;
}

int Http3Connection::handle_webtransport_stream_data(Http3Stream* stream,
                                                     uint32_t flags,
                                                     const uint8_t* data,
                                                     size_t datalen)
{
  if (!ngtcp2_is_bidi_stream(stream->stream_id) ||
      ngtcp2_conn_is_local_stream(conn_, stream->stream_id)) {
    return 0;
  }

  if (!stream->webtransport_child_stream) {
    if (webtransport_session_ids_.empty() || stream->http_stream) {
      return 0;
    }

    NPRPC_HTTP3_DEBUG(
        "conn={} wt_probe_begin transport_stream_id={} datalen={} fin={} buffered={} ",
        debug_id(), stream->stream_id, datalen,
        (flags & NGTCP2_STREAM_DATA_FLAG_FIN) != 0,
        stream->webtransport_probe_buffer.size());

    append_bytes(stream->webtransport_probe_buffer, data, datalen);

    auto signal = decode_quic_varint(stream->webtransport_probe_buffer.data_ptr(),
                                     stream->webtransport_probe_buffer.size());
    if (!signal) {
      if (flags & NGTCP2_STREAM_DATA_FLAG_FIN) {
        return -1;
      }
      return 0;
    }

    const auto [stream_type, stream_type_len] = *signal;
    if (stream_type != k_webtransport_bidi_stream_type) {
      NPRPC_HTTP3_DEBUG(
          "conn={} wt_probe_non_wt transport_stream_id={} stream_type={} buffered={}",
          debug_id(), stream->stream_id, stream_type,
          stream->webtransport_probe_buffer.size());
      return 0;
    }

    auto session_id = decode_quic_varint(
        stream->webtransport_probe_buffer.data_ptr() + stream_type_len,
        stream->webtransport_probe_buffer.size() - stream_type_len);
    if (!session_id) {
      if (flags & NGTCP2_STREAM_DATA_FLAG_FIN) {
        return -1;
      }
      return 0;
    }

    const auto [session_stream_id, session_id_len] = *session_id;
    const auto header_len = stream_type_len + session_id_len;
    if (!has_webtransport_session(static_cast<int64_t>(session_stream_id))) {
      NPRPC_HTTP3_DEBUG(
          "conn={} wt_probe_unknown_session transport_stream_id={} session_stream_id={} buffered={}",
          debug_id(), stream->stream_id, session_stream_id,
          stream->webtransport_probe_buffer.size());
      return 0;
    }

    stream->webtransport_child_stream = true;
    stream->webtransport_session_id = static_cast<int64_t>(session_stream_id);

    NPRPC_HTTP3_DEBUG(
        "conn={} wt_probe_child transport_stream_id={} session_stream_id={} header_len={} buffered={}",
        debug_id(), stream->stream_id, stream->webtransport_session_id,
        header_len, stream->webtransport_probe_buffer.size());

    flat_buffer remaining(stream->webtransport_probe_buffer.size() > header_len
                              ? stream->webtransport_probe_buffer.size() - header_len
                              : flat_buffer::default_initial_size());
    if (stream->webtransport_probe_buffer.size() > header_len) {
      append_bytes(remaining,
                   stream->webtransport_probe_buffer.data_ptr() + header_len,
                   stream->webtransport_probe_buffer.size() - header_len);
    }
    stream->webtransport_probe_buffer = std::move(remaining);
  } else if (stream->webtransport_child_binding == WebTransportChildBinding::Unbound) {
    NPRPC_HTTP3_DEBUG(
        "conn={} wt_probe_append transport_stream_id={} datalen={} fin={} buffered={}",
        debug_id(), stream->stream_id, datalen,
        (flags & NGTCP2_STREAM_DATA_FLAG_FIN) != 0,
        stream->webtransport_probe_buffer.size());
    append_bytes(stream->webtransport_probe_buffer, data, datalen);
  } else {
    NPRPC_HTTP3_DEBUG(
        "conn={} wt_child_payload transport_stream_id={} session_stream_id={} binding={} datalen={} fin={}",
        debug_id(), stream->stream_id, stream->webtransport_session_id,
        static_cast<int>(stream->webtransport_child_binding), datalen,
        (flags & NGTCP2_STREAM_DATA_FLAG_FIN) != 0);
    auto session =
        get_or_create_webtransport_control_session(stream->webtransport_session_id);
    session->process_bytes(data, datalen);
    return 0;
  }

  auto session =
      get_or_create_webtransport_control_session(stream->webtransport_session_id);

  if (stream->webtransport_child_binding == WebTransportChildBinding::Unbound) {
    if (stream->webtransport_probe_buffer.size() < 1) {
      if (flags & NGTCP2_STREAM_DATA_FLAG_FIN) {
        return -1;
      }
      return 0;
    }

    const auto* probe = stream->webtransport_probe_buffer.data_ptr();
    const auto bind_kind = probe[0];
    size_t binding_len = 0;

    switch (bind_kind) {
    case k_webtransport_bind_control:
      stream->webtransport_child_binding = WebTransportChildBinding::Control;
      session->bind_control_stream(stream->stream_id);
      binding_len = 1;
      NPRPC_HTTP3_DEBUG(
          "conn={} wt_bind_control transport_stream_id={} session_stream_id={} buffered={}",
          debug_id(), stream->stream_id, stream->webtransport_session_id,
          stream->webtransport_probe_buffer.size());
      break;
    case k_webtransport_bind_native_stream:
      if (stream->webtransport_probe_buffer.size() < 9) {
        if (flags & NGTCP2_STREAM_DATA_FLAG_FIN) {
          return -1;
        }
        return 0;
      }
      std::memcpy(&stream->webtransport_native_stream_id, probe + 1,
                  sizeof(stream->webtransport_native_stream_id));
      stream->webtransport_child_binding = WebTransportChildBinding::Native;
      session->bind_native_stream(stream->webtransport_native_stream_id,
                                  stream->stream_id);
      binding_len = 9;
      NPRPC_HTTP3_DEBUG(
          "conn={} wt_bind_native transport_stream_id={} session_stream_id={} native_stream_id={} buffered={}",
          debug_id(), stream->stream_id, stream->webtransport_session_id,
          stream->webtransport_native_stream_id,
          stream->webtransport_probe_buffer.size());
      break;
    default:
      NPRPC_LOG_ERROR("[HTTP/3][WT] Unknown child stream binding kind {}",
                      bind_kind);
      return -1;
    }

    if (stream->webtransport_probe_buffer.size() > binding_len) {
      session->process_bytes(stream->webtransport_probe_buffer.data_ptr() + binding_len,
                             stream->webtransport_probe_buffer.size() - binding_len);
    }
    stream->webtransport_probe_buffer.clear();
  }

  return 0;
}

void Http3Connection::queue_raw_stream_write(int64_t stream_id,
                                             flat_buffer&& data)
{
  boost::asio::post(
      strand_, [self = shared_from_this(), stream_id, data = std::move(data)]() mutable {
        auto* stream = self->find_stream(stream_id);
        if (!stream) {
          stream = self->create_stream(stream_id);
        }
        const bool was_empty = stream->raw_write_queue.empty();
        stream->raw_write_queue.emplace_back(std::move(data));
        if (was_empty) {
          self->schedule_raw_writable_stream(stream);
        }
        self->signal_write();
      });
}

int Http3Connection::send_dynamic_response(Http3Stream* stream,
                                           unsigned int status_code,
                                           std::string_view content_type,
                                           std::string&& body)
{
  flat_buffer buffer(body.empty() ? flat_buffer::default_initial_size()
                                  : body.size());
  append_bytes(buffer, reinterpret_cast<const uint8_t*>(body.data()), body.size());
  return send_dynamic_response(stream, status_code, content_type,
                               std::move(buffer));
}

int Http3Connection::send_dynamic_response(Http3Stream* stream,
                                           unsigned int status_code,
                                           std::string_view content_type,
                                           flat_buffer&& body)
{
  // Store response data in stream to keep it alive
  stream->dynamic_body = std::move(body);
  stream->response_content_type =
      std::string(content_type); // Store content-type for lifetime
    stream->response_data = stream->dynamic_body.data_ptr();
  stream->response_len = stream->dynamic_body.size();
  stream->response_offset = 0;

    const bool include_cors = stream->path == "/rpc" ||
                stream->path.rfind("/rpc/", 0) == 0;
  auto& headers = make_response_headers(stream, status_code,
                                        stream->response_content_type,
                                        stream->response_len, include_cors);

  nghttp3_data_reader dr{
      .read_data = http_read_data_cb,
  };

  log_http3_response_submit("dynamic", stream, status_code,
                            stream->response_content_type,
                            stream->response_len);

  int rv = nghttp3_conn_submit_response(httpconn_, stream->stream_id,
                                        headers.headers.data(),
                                        headers.headers.size(), &dr);
  if (rv != 0) {
    NPRPC_LOG_ERROR("[HTTP/3][E] nghttp3_conn_submit_response: {}", 
                    nghttp3_strerror(rv));
    return -1;
  }

  return 0;
}

int Http3Connection::handle_error()
{
  if (last_error_.type == NGTCP2_CCERR_TYPE_IDLE_CLOSE) {
    closed_ = true;
    return -1;
  }

  if (start_closing_period() != 0) {
    closed_ = true;
    return -1;
  }

  if (ngtcp2_conn_in_draining_period(conn_)) {
    return 0;
  }

  // Send connection close
  if (!conn_close_buf_.empty()) {
    server_->send_packet(remote_ep_, conn_close_buf_.data(),
                         conn_close_buf_.size());
  }

  return 0;
}

void Http3Connection::start_draining_period()
{
  auto timeout_ns = ngtcp2_conn_get_pto(conn_) * 3;
  auto timeout = std::chrono::nanoseconds(timeout_ns);

  timer_.expires_after(timeout);
  timer_.async_wait([self = shared_from_this()](boost::system::error_code ec) {
    if (!ec) {
      self->closed_ = true;
      self->server_->remove_connection(self);
    }
  });
}

int Http3Connection::start_closing_period()
{
  if (!conn_ || ngtcp2_conn_in_closing_period(conn_) ||
      ngtcp2_conn_in_draining_period(conn_)) {
    return 0;
  }

  conn_close_buf_.resize(NGTCP2_MAX_UDP_PAYLOAD_SIZE);

  ngtcp2_path_storage ps;
  ngtcp2_path_storage_zero(&ps);

  ngtcp2_pkt_info pi;
  auto n = ngtcp2_conn_write_connection_close(
      conn_, &ps.path, &pi, conn_close_buf_.data(), conn_close_buf_.size(),
      &last_error_, timestamp_ns());

  if (n < 0) {
    NPRPC_LOG_ERROR("[HTTP/3][E] ngtcp2_conn_write_connection_close: {}", 
                    ngtcp2_strerror(static_cast<int>(n)));
    return -1;
  }

  if (n == 0) {
    conn_close_buf_.clear();
    return 0;
  }

  conn_close_buf_.resize(static_cast<size_t>(n));

  // Start close wait timer
  auto timeout_ns = ngtcp2_conn_get_pto(conn_) * 3;
  auto timeout = std::chrono::nanoseconds(timeout_ns);

  timer_.expires_after(timeout);
  timer_.async_wait([self = shared_from_this()](boost::system::error_code ec) {
    if (!ec) {
      self->closed_ = true;
      self->server_->remove_connection(self);
    }
  });

  return 0;
}

//==============================================================================
// Debugging
//==============================================================================
void Http3Connection::print_ssl_state(std::string_view prefix)
{
#if NPRPC_NGTCP2_ENABLE_LOGGING 
  // Check SSL state
  auto ssl = ngtcp2_crypto_ossl_ctx_get_ssl(ossl_ctx_);
  if (ssl) {
    NPRPC_HTTP3_TRACE("SSL [{}]: state: {}", prefix,
                      SSL_state_string_long(ssl));
    int ssl_err = SSL_get_error(ssl, 0);
    if (ssl_err != SSL_ERROR_NONE) {
      NPRPC_HTTP3_TRACE("SSL [{}]: error: {}", prefix,
                        ERR_error_string(ssl_err, nullptr));
    }
    // Check pending errors
    unsigned long err;
    while ((err = ERR_get_error()) != 0) {
      NPRPC_HTTP3_TRACE("SSL [{}]: pending error: {}", prefix,
                        ERR_error_string(err, nullptr));
    }
  }
#endif
}

//==============================================================================
// Static ngtcp2 callbacks
//==============================================================================

int Http3Connection::on_handshake_completed(ngtcp2_conn* conn, void* user_data)
{
  auto h = static_cast<Http3Connection*>(user_data);

  NPRPC_HTTP3_TRACE("Handshake completed with {}",
                    h->remote_ep_.address().to_string());

  return 0;
}

int Http3Connection::on_recv_stream_data(ngtcp2_conn* conn,
                                         uint32_t flags,
                                         int64_t stream_id,
                                         uint64_t offset,
                                         const uint8_t* data,
                                         size_t datalen,
                                         void* user_data,
                                         void* stream_user_data)
{
  auto h = static_cast<Http3Connection*>(user_data);
  if (h->recv_stream_data(flags, stream_id, data, datalen) != 0) {
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }

  return 0;
}

int Http3Connection::on_acked_stream_data_offset(ngtcp2_conn* conn,
                                                 int64_t stream_id,
                                                 uint64_t offset,
                                                 uint64_t datalen,
                                                 void* user_data,
                                                 void* stream_user_data)
{
  auto h = static_cast<Http3Connection*>(user_data);
  if (h->acked_stream_data_offset(stream_id, datalen) != 0) {
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }
  return 0;
}

int Http3Connection::on_stream_open(ngtcp2_conn* conn,
                                    int64_t stream_id,
                                    void* user_data)
{
  auto h = static_cast<Http3Connection*>(user_data);

  if (!ngtcp2_is_bidi_stream(stream_id)) {
    return 0;
  }

  h->create_stream(stream_id);
  return 0;
}

int Http3Connection::on_stream_close(ngtcp2_conn* conn,
                                     uint32_t flags,
                                     int64_t stream_id,
                                     uint64_t app_error_code,
                                     void* user_data,
                                     void* stream_user_data)
{
  auto h = static_cast<Http3Connection*>(user_data);
  auto* stream = h->find_stream(stream_id);

  if (!(flags & NGTCP2_STREAM_CLOSE_FLAG_APP_ERROR_CODE_SET)) {
    app_error_code = NGHTTP3_H3_NO_ERROR;
  }

  if (h->httpconn_ && (!stream || !stream->webtransport_child_stream)) {
    int rv = nghttp3_conn_close_stream(h->httpconn_, stream_id, app_error_code);
    if (rv != 0 && rv != NGHTTP3_ERR_STREAM_NOT_FOUND) {
      NPRPC_HTTP3_ERROR("nghttp3_conn_close_stream: {}", nghttp3_strerror(rv));
      ngtcp2_ccerr_set_application_error(
          &h->last_error_, nghttp3_err_infer_quic_app_error_code(rv), nullptr,
          0);
      return NGTCP2_ERR_CALLBACK_FAILURE;
    }
  }

  h->http_stream_close(stream_id, app_error_code);
  return 0;
}

int Http3Connection::on_extend_max_remote_streams_bidi(ngtcp2_conn* conn,
                                                       uint64_t max_streams,
                                                       void* user_data)
{
  auto h = static_cast<Http3Connection*>(user_data);
  h->extend_max_remote_streams_bidi(max_streams);
  return 0;
}

int Http3Connection::on_extend_max_stream_data(ngtcp2_conn* conn,
                                               int64_t stream_id,
                                               uint64_t max_data,
                                               void* user_data,
                                               void* stream_user_data)
{
  auto h = static_cast<Http3Connection*>(user_data);
  h->extend_max_stream_data(stream_id, max_data);
  return 0;
}

int Http3Connection::on_recv_tx_key(ngtcp2_conn* conn,
                                    ngtcp2_encryption_level level,
                                    void* user_data)
{
  NPRPC_HTTP3_TRACE("on_recv_tx_key called, level={}", static_cast<int>(level));

  if (level != NGTCP2_ENCRYPTION_LEVEL_1RTT) {
    return 0;
  }

  auto h = static_cast<Http3Connection*>(user_data);
  if (h->setup_httpconn() != 0) {
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }

  return 0;
}

int Http3Connection::on_get_new_connection_id(ngtcp2_conn* conn,
                                              ngtcp2_cid* cid,
                                              uint8_t* token,
                                              size_t cidlen,
                                              void* user_data)
{
  random_bytes(cid->data, cidlen);
  cid->datalen = cidlen;

  if (ngtcp2_crypto_generate_stateless_reset_token(
          token, g_static_secret.data(), g_static_secret.size(), cid) != 0) {
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }

  auto h = static_cast<Http3Connection*>(user_data);
  h->server_->associate_cid(cid, h->shared_from_this());

  return 0;
}

int Http3Connection::on_remove_connection_id(ngtcp2_conn* conn,
                                             const ngtcp2_cid* cid,
                                             void* user_data)
{
  auto h = static_cast<Http3Connection*>(user_data);
  h->server_->dissociate_cid(cid);
  return 0;
}

void Http3Connection::on_rand(uint8_t* dest,
                              size_t destlen,
                              const ngtcp2_rand_ctx* rand_ctx)
{
  random_bytes(dest, destlen);
}

//==============================================================================
// Static nghttp3 callbacks
//==============================================================================

int Http3Connection::http_acked_stream_data_cb(nghttp3_conn* conn,
                                               int64_t stream_id,
                                               uint64_t datalen,
                                               void* user_data,
                                               void* stream_user_data)
{
  // Stream data was acknowledged - we could free resources here
  return 0;
}

int Http3Connection::http_recv_data_cb(nghttp3_conn* conn,
                                       int64_t stream_id,
                                       const uint8_t* data,
                                       size_t datalen,
                                       void* user_data,
                                       void* stream_user_data)
{
  auto h = static_cast<Http3Connection*>(user_data);
  auto stream = static_cast<Http3Stream*>(stream_user_data);

  if (stream) {
    append_bytes(stream->request_body, data, datalen);
  }

  // Extend flow control
  ngtcp2_conn_extend_max_stream_offset(h->conn_, stream_id, datalen);
  ngtcp2_conn_extend_max_offset(h->conn_, datalen);

  return 0;
}

int Http3Connection::http_deferred_consume_cb(nghttp3_conn* conn,
                                              int64_t stream_id,
                                              size_t nconsumed,
                                              void* user_data,
                                              void* stream_user_data)
{
  auto h = static_cast<Http3Connection*>(user_data);
  ngtcp2_conn_extend_max_stream_offset(h->conn_, stream_id, nconsumed);
  ngtcp2_conn_extend_max_offset(h->conn_, nconsumed);
  return 0;
}

int Http3Connection::http_begin_headers_cb(nghttp3_conn* conn,
                                           int64_t stream_id,
                                           void* user_data,
                                           void* stream_user_data)
{
  auto h = static_cast<Http3Connection*>(user_data);
  h->http_begin_headers(stream_id);
  return 0;
}

int Http3Connection::http_recv_header_cb(nghttp3_conn* conn,
                                         int64_t stream_id,
                                         int32_t token,
                                         nghttp3_rcbuf* name,
                                         nghttp3_rcbuf* value,
                                         uint8_t flags,
                                         void* user_data,
                                         void* stream_user_data)
{
  auto h = static_cast<Http3Connection*>(user_data);
  auto stream = static_cast<Http3Stream*>(stream_user_data);

  if (stream) {
    h->http_recv_header(stream, token, name, value);
  }

  return 0;
}

int Http3Connection::http_end_headers_cb(nghttp3_conn* conn,
                                         int64_t stream_id,
                                         int fin,
                                         void* user_data,
                                         void* stream_user_data)
{
  auto h = static_cast<Http3Connection*>(user_data);
  auto stream = static_cast<Http3Stream*>(stream_user_data);

  if (stream && h->http_end_headers(stream) != 0) {
    return NGHTTP3_ERR_CALLBACK_FAILURE;
  }

  return 0;
}

int Http3Connection::http_end_stream_cb(nghttp3_conn* conn,
                                        int64_t stream_id,
                                        void* user_data,
                                        void* stream_user_data)
{
  auto h = static_cast<Http3Connection*>(user_data);
  auto stream = static_cast<Http3Stream*>(stream_user_data);

  try {
    if (stream && h->http_end_stream(stream) != 0) {
      return NGHTTP3_ERR_CALLBACK_FAILURE;
    }
  } catch (const std::exception& e) {
    NPRPC_HTTP3_ERROR("Exception in http_end_stream_cb: {}", e.what());
    return NGHTTP3_ERR_CALLBACK_FAILURE;
  }

  return 0;
}

int Http3Connection::http_stop_sending_cb(nghttp3_conn* conn,
                                          int64_t stream_id,
                                          uint64_t app_error_code,
                                          void* user_data,
                                          void* stream_user_data)
{
  auto h = static_cast<Http3Connection*>(user_data);
  auto* stream = h->find_stream(stream_id);
  NPRPC_HTTP3_DEBUG(
      "conn={} stop_sending stream_id={} path='{}' app_error_code={} wt_session={} wt_child={} wt_binding={}",
      h->debug_id(), stream_id, stream ? stream->path : "", app_error_code,
      stream ? stream->webtransport_session : false,
      stream ? stream->webtransport_child_stream : false,
      stream ? static_cast<int>(stream->webtransport_child_binding) : -1);
  ngtcp2_conn_shutdown_stream_read(h->conn_, 0, stream_id, app_error_code);
  return 0;
}

int Http3Connection::http_reset_stream_cb(nghttp3_conn* conn,
                                          int64_t stream_id,
                                          uint64_t app_error_code,
                                          void* user_data,
                                          void* stream_user_data)
{
  auto h = static_cast<Http3Connection*>(user_data);
  auto* stream = h->find_stream(stream_id);
  NPRPC_HTTP3_DEBUG(
      "conn={} reset_stream stream_id={} path='{}' app_error_code={} wt_session={} wt_child={} wt_binding={}",
      h->debug_id(), stream_id, stream ? stream->path : "", app_error_code,
      stream ? stream->webtransport_session : false,
      stream ? stream->webtransport_child_stream : false,
      stream ? static_cast<int>(stream->webtransport_child_binding) : -1);
  ngtcp2_conn_shutdown_stream_write(h->conn_, 0, stream_id, app_error_code);
  return 0;
}

nghttp3_ssize Http3Connection::http_read_data_cb(nghttp3_conn* conn,
                                                 int64_t stream_id,
                                                 nghttp3_vec* vec,
                                                 size_t veccnt,
                                                 uint32_t* pflags,
                                                 void* user_data,
                                                 void* stream_user_data)
{
  auto stream = static_cast<Http3Stream*>(stream_user_data);

  if (stream && stream->webtransport_session && !stream->response_data) {
    NPRPC_HTTP3_DEBUG(
        "read_data stream_id={} path='{}' webtransport_connect_eof no_end_stream response_len={} response_offset={}",
        stream_id, stream->path, stream->response_len, stream->response_offset);
    *pflags |= NGHTTP3_DATA_FLAG_EOF | NGHTTP3_DATA_FLAG_NO_END_STREAM;
    return 0;
  }

  if (!stream || !stream->response_data) {
    NPRPC_HTTP3_DEBUG("read_data stream_id={} empty_response eof", stream_id);
    *pflags |= NGHTTP3_DATA_FLAG_EOF;
    return 0;
  }

  size_t remaining = stream->response_len - stream->response_offset;

  if (remaining == 0) {
    NPRPC_HTTP3_DEBUG(
        "read_data stream_id={} path='{}' eof response_len={} response_offset={}",
        stream_id, stream->path, stream->response_len, stream->response_offset);
    *pflags |= NGHTTP3_DATA_FLAG_EOF;
    return 0;
  }

  const auto current_offset = stream->response_offset;
  vec[0].base =
      const_cast<uint8_t*>(stream->response_data + stream->response_offset);
  vec[0].len = remaining;
  stream->response_offset += remaining;

  NPRPC_HTTP3_DEBUG(
      "read_data stream_id={} path='{}' chunk_len={} offset={} next_offset={} total={} eof=true",
      stream_id, stream->path, remaining, current_offset,
      stream->response_offset, stream->response_len);

  *pflags |= NGHTTP3_DATA_FLAG_EOF;

  return 1;
}

//==============================================================================
// Http3Server Implementation
//==============================================================================

Http3Server::Http3Server(boost::asio::io_context& ioc,
                         const std::string& cert_file,
                         const std::string& key_file,
                         uint16_t port)
    : ioc_(ioc)
    , cert_file_(cert_file)
    , key_file_(key_file)
    , port_(port)
    , socket_(ioc)
{
}

Http3Server::~Http3Server() { stop(); }

bool Http3Server::start()
{
  // Initialize static secret for tokens
  init_static_secret();

  // Initialize ngtcp2 crypto
  if (ngtcp2_crypto_ossl_init() != 0) {
    NPRPC_HTTP3_ERROR("Failed to initialize ngtcp2 crypto");
    return false;
  }

  // Create SSL context
  ssl_ctx_ = SSL_CTX_new(TLS_server_method());
  if (!ssl_ctx_) {
    NPRPC_HTTP3_ERROR("Failed to create SSL context: {}", ERR_error_string(ERR_get_error(), nullptr));
    return false;
  }

  // Set TLS 1.3 minimum (required for QUIC)
  SSL_CTX_set_min_proto_version(ssl_ctx_, TLS1_3_VERSION);
  SSL_CTX_set_max_proto_version(ssl_ctx_, TLS1_3_VERSION);

  // Enable early data
  SSL_CTX_set_max_early_data(ssl_ctx_, UINT32_MAX);

  // SSL options
  SSL_CTX_set_options(
      ssl_ctx_, (SSL_OP_ALL & ~SSL_OP_DONT_INSERT_EMPTY_FRAGMENTS) |
                    SSL_OP_SINGLE_ECDH_USE | SSL_OP_CIPHER_SERVER_PREFERENCE |
                    SSL_OP_NO_ANTI_REPLAY);

  // Set ciphersuites for TLS 1.3
  if (SSL_CTX_set_ciphersuites(ssl_ctx_, crypto_default_ciphers()) != 1) {
    NPRPC_HTTP3_TRACE("Failed to set ciphersuites");
    SSL_CTX_free(ssl_ctx_);
    ssl_ctx_ = nullptr;
    return false;
  }

  // Set groups for key exchange
  if (SSL_CTX_set1_groups_list(ssl_ctx_, crypto_default_groups()) != 1) {
    NPRPC_HTTP3_TRACE("Failed to set groups");
    SSL_CTX_free(ssl_ctx_);
    ssl_ctx_ = nullptr;
    return false;
  }

  SSL_CTX_set_mode(ssl_ctx_, SSL_MODE_RELEASE_BUFFERS);

  // Set ALPN callback for HTTP/3
  SSL_CTX_set_alpn_select_cb(
      ssl_ctx_,
      [](SSL* ssl, const unsigned char** out, unsigned char* outlen,
         const unsigned char* in, unsigned int inlen, void* arg) -> int {
        NPRPC_HTTP3_TRACE("ALPN callback called, inlen={}", inlen);

#if NPRPC_ENABLE_HTTP3_TRACE
        // Print received ALPN protos
        for (unsigned int i = 0; i < inlen;) {
          uint8_t len = in[i];
          if (i + 1 + len > inlen)
            break;
          std::string proto(reinterpret_cast<const char*>(in + i + 1), len);
          NPRPC_HTTP3_TRACE("ALPN proto offered: '{}'", proto);
          i += 1 + len;
        }
#endif

        // Look for h3 ALPN
        for (unsigned int i = 0; i < inlen;) {
          uint8_t len = in[i];
          if (i + 1 + len > inlen) {
            break;
          }
          if (len == 2 && in[i + 1] == 'h' && in[i + 2] == '3') {
            *out = in + i + 1;
            *outlen = len;
            NPRPC_HTTP3_TRACE("ALPN selected: h3");
            return SSL_TLSEXT_ERR_OK;
          }
          i += 1 + len;
        }
        NPRPC_HTTP3_TRACE("ALPN selection failed - no h3 found");
        return SSL_TLSEXT_ERR_ALERT_FATAL;
      },
      nullptr);

  // Load certificate and key
  if (SSL_CTX_use_certificate_chain_file(ssl_ctx_, cert_file_.c_str()) != 1) {
    NPRPC_HTTP3_ERROR("Failed to load certificate: {}",
                      ERR_error_string(ERR_get_error(), nullptr));
    SSL_CTX_free(ssl_ctx_);
    ssl_ctx_ = nullptr;
    return false;
  }

  if (SSL_CTX_use_PrivateKey_file(ssl_ctx_, key_file_.c_str(),
                                  SSL_FILETYPE_PEM) != 1) {
    NPRPC_HTTP3_ERROR("Failed to load private key: {}",
                      ERR_error_string(ERR_get_error(), nullptr));
    SSL_CTX_free(ssl_ctx_);
    ssl_ctx_ = nullptr;
    return false;
  }

  if (SSL_CTX_check_private_key(ssl_ctx_) != 1) {
    NPRPC_HTTP3_ERROR("Certificate and private key mismatch");
    SSL_CTX_free(ssl_ctx_);
    ssl_ctx_ = nullptr;
    return false;
  }

  // Set session ID context
  static const unsigned char sid_ctx[] = "nprpc http3 server";
  SSL_CTX_set_session_id_context(ssl_ctx_, sid_ctx, sizeof(sid_ctx) - 1);

  // Open UDP socket
  boost::system::error_code ec;
  socket_.open(boost::asio::ip::udp::v6(), ec);
  if (ec) {
    NPRPC_HTTP3_ERROR("Failed to open socket: {}", ec.message());
    SSL_CTX_free(ssl_ctx_);
    ssl_ctx_ = nullptr;
    return false;
  }

  // Allow IPv4 connections too (dual-stack)
  socket_.set_option(boost::asio::ip::v6_only(false), ec);
  socket_.set_option(boost::asio::socket_base::reuse_address(true), ec);

  // Increase receive buffer size
  socket_.set_option(
      boost::asio::socket_base::receive_buffer_size(2 * 1024 * 1024), ec);
  socket_.set_option(
      boost::asio::socket_base::send_buffer_size(2 * 1024 * 1024), ec);

  socket_.bind(
      boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v6(), port_), ec);
  if (ec) {
    NPRPC_LOG_ERROR("Failed to bind socket: {}", ec.message());
    SSL_CTX_free(ssl_ctx_);
    ssl_ctx_ = nullptr;
    return false;
  }

  // Get local endpoint
  local_ep_ = socket_.local_endpoint();

  running_ = true;

  NPRPC_HTTP3_TRACE("Server listening on port {} (nghttp3/ngtcp2 backend)",
                    port_);

  // Start receiving
  do_receive();

  return true;
}

void Http3Server::stop()
{
  running_ = false;

  boost::system::error_code ec;
  socket_.close(ec);

  {
    std::lock_guard lock(mutex_);
    connections_.clear();
  }

  if (ssl_ctx_) {
    SSL_CTX_free(ssl_ctx_);
    ssl_ctx_ = nullptr;
  }
}

int Http3Server::send_packet(const boost::asio::ip::udp::endpoint& remote_ep,
                             const uint8_t* data,
                             size_t len)
{
  boost::system::error_code ec;
  socket_.send_to(boost::asio::buffer(data, len), remote_ep, 0, ec);
  if (ec) {
    NPRPC_HTTP3_TRACE("send_to failed: {}", ec.message());
    return -1;
  }
  return 0;
}

void Http3Server::associate_cid(const ngtcp2_cid* cid,
                                std::shared_ptr<Http3Connection> conn)
{
  std::string key = cid_to_string(cid);
  std::lock_guard lock(mutex_);
  connections_[key] = conn;
}

void Http3Server::dissociate_cid(const ngtcp2_cid* cid)
{
  std::string key = cid_to_string(cid);
  std::lock_guard lock(mutex_);
  connections_.erase(key);
}

void Http3Server::remove_connection(std::shared_ptr<Http3Connection> conn)
{
  std::lock_guard lock(mutex_);
  // Remove all CIDs associated with this connection
  for (auto it = connections_.begin(); it != connections_.end();) {
    if (it->second == conn) {
      it = connections_.erase(it);
    } else {
      ++it;
    }
  }
}

void Http3Server::do_receive()
{
  if (!running_) {
    return;
  }

  socket_.async_receive_from(
      boost::asio::buffer(recv_buf_), remote_ep_,
      [this](boost::system::error_code ec, std::size_t bytes_recvd) {
        if (!ec && bytes_recvd > 0) {
          handle_packet(remote_ep_, recv_buf_.data(), bytes_recvd);
        }

        if (running_ && (!ec || ec == boost::asio::error::message_size)) {
          do_receive();
        }
      });
}

void Http3Server::handle_packet(const boost::asio::ip::udp::endpoint& remote_ep,
                                const uint8_t* data,
                                size_t len)
{
  // Parse the packet header to get DCID
  ngtcp2_version_cid vc;
  int rv = ngtcp2_pkt_decode_version_cid(&vc, data, len, SCID_LEN);
  if (rv != 0) {
    if (rv == NGTCP2_ERR_VERSION_NEGOTIATION) {
      send_version_negotiation(remote_ep, vc);
    }
    return;
  }

  ngtcp2_cid dcid;
  dcid.datalen = vc.dcidlen;
  memcpy(dcid.data, vc.dcid, vc.dcidlen);

  // Find existing connection
  auto conn = find_connection(&dcid);

  if (conn) {
    // Existing connection - feed data
    ngtcp2_pkt_info pi{};
    conn->enqueue_packet(copy_bytes_to_buffer(data, len), pi);
    return;
  }

  // New connection - decode packet header
  ngtcp2_pkt_hd hd;
  rv = ngtcp2_pkt_decode_hd_long(&hd, data, len);
  if (rv < 0) {
    // Try short header
    if (ngtcp2_pkt_decode_hd_short(&hd, data, len, SCID_LEN) < 0) {
      // Send stateless reset for unknown short header packets
      send_stateless_reset(remote_ep, vc.dcid, vc.dcidlen);
      return;
    }
    return;
  }

  // Only handle Initial packets for new connections
  if (hd.type != NGTCP2_PKT_INITIAL) {
    return;
  }

  // Validate QUIC version
  if (!ngtcp2_is_supported_version(hd.version)) {
    send_version_negotiation(remote_ep, vc);
    return;
  }

  ngtcp2_cid scid;
  scid.datalen = vc.scidlen;
  memcpy(scid.data, vc.scid, vc.scidlen);

  // Create new connection
  // Note: client_scid is the client's Source CID (from hd.scid/vc.scid)
  //       client_dcid is what client sent as Destination CID (from
  //       hd.dcid/vc.dcid)
  conn = std::make_shared<Http3Connection>(
      this, socket_, local_ep_, remote_ep, &scid, &dcid,
      nullptr, // client_scid=scid, client_dcid=dcid, no retry token initially
      hd.token, hd.tokenlen, hd.version, ssl_ctx_);

  if (!conn->init()) {
    NPRPC_HTTP3_ERROR("Failed to initialize connection");
    return;
  }

  // Associate both DCID and SCID with the connection
  associate_cid(&dcid, conn);
  associate_cid(&conn->scid(), conn);

  NPRPC_HTTP3_TRACE("New connection from {}", remote_ep.address().to_string());

  // Feed the initial packet
  ngtcp2_pkt_info pi{};
  conn->enqueue_packet(copy_bytes_to_buffer(data, len), pi);
}

int Http3Server::send_version_negotiation(
    const boost::asio::ip::udp::endpoint& remote_ep,
    const ngtcp2_version_cid& vc)
{
  std::array<uint8_t, NGTCP2_MAX_UDP_PAYLOAD_SIZE> buf;

  std::array<uint32_t, 2> sv = {
      NGTCP2_PROTO_VER_V1,
      NGTCP2_PROTO_VER_V2,
  };

  auto nwrite = ngtcp2_pkt_write_version_negotiation(
      buf.data(), buf.size(), std::rand() % 256, vc.scid, vc.scidlen, vc.dcid,
      vc.dcidlen, sv.data(), sv.size());

  if (nwrite < 0) {
    NPRPC_HTTP3_TRACE("ngtcp2_pkt_write_version_negotiation: {}",
                      ngtcp2_strerror(static_cast<int>(nwrite)));
    return -1;
  }

  return send_packet(remote_ep, buf.data(), static_cast<size_t>(nwrite));
}

int Http3Server::send_stateless_reset(
    const boost::asio::ip::udp::endpoint& remote_ep,
    const uint8_t* dcid,
    size_t dcidlen)
{
  std::array<uint8_t, NGTCP2_MAX_UDP_PAYLOAD_SIZE> buf;

  ngtcp2_cid cid;
  cid.datalen = dcidlen;
  memcpy(cid.data, dcid, dcidlen);

  std::array<uint8_t, NGTCP2_STATELESS_RESET_TOKENLEN> token;
  if (ngtcp2_crypto_generate_stateless_reset_token(
          token.data(), g_static_secret.data(), g_static_secret.size(), &cid) !=
      0) {
    return -1;
  }

  // Generate random padding
  size_t randlen = std::rand() % 64 + 40;
  std::vector<uint8_t> rand_data(randlen);
  random_bytes(rand_data.data(), randlen);

  auto nwrite = ngtcp2_pkt_write_stateless_reset(
      buf.data(), buf.size(), token.data(), rand_data.data(), randlen);

  if (nwrite < 0) {
    return -1;
  }

  return send_packet(remote_ep, buf.data(), static_cast<size_t>(nwrite));
}

std::shared_ptr<Http3Connection>
Http3Server::find_connection(const ngtcp2_cid* dcid)
{
  std::string key = cid_to_string(dcid);

  std::lock_guard lock(mutex_);
  auto it = connections_.find(key);
  return it != connections_.end() ? it->second : nullptr;
}

//==============================================================================
// Global HTTP/3 Server Instance
//==============================================================================

static std::unique_ptr<Http3Server> g_http3_server;

NPRPC_API void init_http3_server(boost::asio::io_context& ioc)
{
  if (!g_cfg.http3_enabled)
    return;

  NPRPC_HTTP3_TRACE("Initializing on port {} (nghttp3 backend)",
                    g_cfg.listen_http_port);

  g_http3_server = std::make_unique<Http3Server>(
      ioc, g_cfg.http_cert_file, g_cfg.http_key_file, g_cfg.listen_http_port);

  if (!g_http3_server->start()) {
    NPRPC_HTTP3_ERROR("Failed to start server");
    g_http3_server.reset();
  }
}

NPRPC_API void stop_http3_server()
{
  if (g_http3_server) {
    g_http3_server->stop();
    g_http3_server.reset();
  }
}

} // namespace nprpc::impl

#endif // NPRPC_HTTP3_ENABLED && NPRPC_HTTP3_BACKEND_NGHTTP3

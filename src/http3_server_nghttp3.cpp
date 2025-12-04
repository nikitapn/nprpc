// Copyright (c) 2025 nikitapnn1@gmail.com
// SPDX-License-Identifier: MIT

#include <nprpc/impl/http3_server.hpp>

// This file implements HTTP/3 using ngtcp2 + nghttp3 backend
#if defined(NPRPC_HTTP3_ENABLED) && defined(NPRPC_HTTP3_BACKEND_NGHTTP3)

#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/impl/http_rpc_session.hpp>
#include <nprpc/common.hpp>

#include <ngtcp2/ngtcp2.h>
#include <ngtcp2/ngtcp2_crypto.h>
#include <ngtcp2/ngtcp2_crypto_ossl.h>
#include <nghttp3/nghttp3.h>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/rand.h>

#include <boost/asio.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/steady_timer.hpp>

#include <iostream>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <vector>
#include <cstring>
#include <format>
#include <random>
#include <chrono>
#include <array>
#include <deque>
#include <ranges>

#include "debug.hpp"

namespace nprpc::impl {

// Forward declaration - implemented in http_server.cpp
extern beast::string_view mime_type(beast::string_view path);
extern std::string path_cat(beast::string_view base, beast::string_view path);

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

} // anonymous namespace

//==============================================================================
// Utility functions
//==============================================================================

namespace {

uint64_t timestamp_ns() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()).count();
}

void random_bytes(uint8_t* data, size_t len) {
    RAND_bytes(data, static_cast<int>(len));
}

void init_static_secret() {
    if (!g_static_secret_initialized) {
        random_bytes(g_static_secret.data(), g_static_secret.size());
        g_static_secret_initialized = true;
    }
}

std::string cid_to_string(const ngtcp2_cid* cid) {
    return std::string(reinterpret_cast<const char*>(cid->data), cid->datalen);
}

const char *crypto_default_ciphers() {
  return "TLS_AES_128_GCM_SHA256:TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256";
}

const char *crypto_default_groups() {
  return "X25519:P-256:P-384:P-521";
}

void log_printf(void *user_data, const char *fmt, ...) {
  va_list ap;
  std::array<char, 4096> buf;

  va_start(ap, fmt);
  auto n = vsnprintf(buf.data(), buf.size(), fmt, ap);
  va_end(ap);

  if (static_cast<size_t>(n) >= buf.size()) {
    n = buf.size() - 1;
  }

  buf[static_cast<size_t>(n++)] = '\n';

  while (write(fileno(stderr), buf.data(), static_cast<size_t>(n)) == -1 &&
         errno == EINTR)
    ;
}

} // anonymous namespace

//==============================================================================
// Http3Stream - Tracks state for a single HTTP/3 request stream
//==============================================================================

struct Http3Stream {
    int64_t stream_id = -1;
    std::string method;
    std::string path;
    std::string authority;
    std::string content_type;
    size_t content_length = 0;
    std::vector<uint8_t> request_body;

    // Response data - kept alive for async sending
    std::vector<uint8_t> response_body;
    const uint8_t* response_data = nullptr;
    size_t response_len = 0;
    size_t response_offset = 0;  // How much has been sent
    bool headers_sent = false;
    bool body_complete = false;
    bool fin_sent = false;

    // HTTP/3 response status and headers (kept alive for nghttp3)
    std::string status_str;
    std::string content_type_str;
    std::string content_length_str;
};

//==============================================================================
// Http3Connection - A single QUIC connection with HTTP/3
//==============================================================================

class Http3Server;

class Http3Connection : public std::enable_shared_from_this<Http3Connection> {
public:
    Http3Connection(Http3Server* server,
                    boost::asio::ip::udp::socket& socket,
                    const boost::asio::ip::udp::endpoint& local_ep,
                    const boost::asio::ip::udp::endpoint& remote_ep,
                    const ngtcp2_cid* client_scid,   // Client's Source CID (from hd.scid)
                    const ngtcp2_cid* client_dcid,   // Client's Destination CID (from hd.dcid)
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

    const ngtcp2_cid& scid() const { return scid_; }
    const boost::asio::ip::udp::endpoint& remote_endpoint() const { return remote_ep_; }
    ngtcp2_conn* conn() { return conn_; }

    bool is_closed() const { return closed_; }
    bool is_draining() const { 
        return conn_ && (ngtcp2_conn_in_draining_period(conn_) || 
                        ngtcp2_conn_in_closing_period(conn_)); 
    }

    // Called by server when write is possible
    void signal_write();

    // ngtcp2 crypto connection reference
    ngtcp2_crypto_conn_ref* conn_ref() { return &conn_ref_; }

private:
    // Stream management
    Http3Stream* find_stream(int64_t stream_id);
    Http3Stream* create_stream(int64_t stream_id);
    void remove_stream(int64_t stream_id);

    // HTTP/3 handling
    int setup_httpconn();
    int recv_stream_data(uint32_t flags, int64_t stream_id,
                         const uint8_t* data, size_t datalen);
    int acked_stream_data_offset(int64_t stream_id, uint64_t datalen);
    void extend_max_remote_streams_bidi(uint64_t max_streams);
    void extend_max_stream_data(int64_t stream_id, uint64_t max_data);

    // HTTP callbacks
    void http_begin_headers(int64_t stream_id);
    void http_recv_header(Http3Stream* stream, int32_t token, 
                          nghttp3_rcbuf* name, nghttp3_rcbuf* value);
    int http_end_headers(Http3Stream* stream);
    int http_end_stream(Http3Stream* stream);
    void http_stream_close(int64_t stream_id, uint64_t app_error_code);

    // Response handling
    int start_response(Http3Stream* stream);
    int send_response(Http3Stream* stream, unsigned int status_code,
                      const std::string& content_type,
                      const std::vector<uint8_t>& body);

    // Error handling
    int handle_error();
    void start_draining_period();
    int start_closing_period();

    // Debug
    void print_ssl_state(std::string_view prefix);

    // Static callbacks for ngtcp2
    static int on_handshake_completed(ngtcp2_conn* conn, void* user_data);
    static int on_recv_stream_data(ngtcp2_conn* conn, uint32_t flags, 
                                   int64_t stream_id, uint64_t offset,
                                   const uint8_t* data, size_t datalen,
                                   void* user_data, void* stream_user_data);
    static int on_acked_stream_data_offset(ngtcp2_conn* conn, int64_t stream_id,
                                           uint64_t offset, uint64_t datalen,
                                           void* user_data, void* stream_user_data);
    static int on_stream_open(ngtcp2_conn* conn, int64_t stream_id, void* user_data);
    static int on_stream_close(ngtcp2_conn* conn, uint32_t flags, int64_t stream_id,
                               uint64_t app_error_code, void* user_data,
                               void* stream_user_data);
    static int on_extend_max_remote_streams_bidi(ngtcp2_conn* conn, 
                                                 uint64_t max_streams, void* user_data);
    static int on_extend_max_stream_data(ngtcp2_conn* conn, int64_t stream_id,
                                         uint64_t max_data, void* user_data,
                                         void* stream_user_data);
    static int on_recv_tx_key(ngtcp2_conn* conn, ngtcp2_encryption_level level,
                              void* user_data);
    static int on_get_new_connection_id(ngtcp2_conn* conn, ngtcp2_cid* cid,
                                        uint8_t* token, size_t cidlen, void* user_data);
    static int on_remove_connection_id(ngtcp2_conn* conn, const ngtcp2_cid* cid,
                                       void* user_data);
    static void on_rand(uint8_t* dest, size_t destlen, const ngtcp2_rand_ctx* rand_ctx);
    
    static int on_recv_client_initial(ngtcp2_conn *conn, const ngtcp2_cid *dcid,
                                       void *user_data) {
        NPRPC_HTTP3_TRACE("on_recv_client_initial: dcid len={}", dcid->datalen);
        int rv = ngtcp2_crypto_recv_client_initial_cb(conn, dcid, user_data);
        NPRPC_HTTP3_TRACE("on_recv_client_initial: returned {}", rv);
        return rv;
    }
    
    static int on_recv_crypto_data(ngtcp2_conn *conn, ngtcp2_encryption_level crypto_level,
                                   uint64_t offset, const uint8_t *data, size_t datalen,
                                   void *user_data) {
        NPRPC_HTTP3_TRACE("on_recv_crypto_data: level={}, offset={}, len={}", (int)crypto_level, offset, datalen);
        
        // Get the ossl_ctx to check state before and after
        auto ossl_ctx = static_cast<ngtcp2_crypto_ossl_ctx*>(ngtcp2_conn_get_tls_native_handle(conn));
        auto ssl = ngtcp2_crypto_ossl_ctx_get_ssl(ossl_ctx);
        NPRPC_HTTP3_TRACE("on_recv_crypto_data: SSL state before: {}", SSL_state_string_long(ssl));
        
        int rv = ngtcp2_crypto_recv_crypto_data_cb(conn, crypto_level, offset, data, datalen, user_data);
        
        NPRPC_HTTP3_TRACE("on_recv_crypto_data: SSL state after: {}", SSL_state_string_long(ssl));
        NPRPC_HTTP3_TRACE("on_recv_crypto_data: returned {}", rv);
        
        return rv;
    }

    // Static callbacks for nghttp3  
    static int http_acked_stream_data_cb(nghttp3_conn* conn, int64_t stream_id,
                                         uint64_t datalen, void* user_data,
                                         void* stream_user_data);
    static int http_recv_data_cb(nghttp3_conn* conn, int64_t stream_id,
                                 const uint8_t* data, size_t datalen,
                                 void* user_data, void* stream_user_data);
    static int http_deferred_consume_cb(nghttp3_conn* conn, int64_t stream_id,
                                        size_t nconsumed, void* user_data,
                                        void* stream_user_data);
    static int http_begin_headers_cb(nghttp3_conn* conn, int64_t stream_id,
                                     void* user_data, void* stream_user_data);
    static int http_recv_header_cb(nghttp3_conn* conn, int64_t stream_id,
                                   int32_t token, nghttp3_rcbuf* name,
                                   nghttp3_rcbuf* value, uint8_t flags,
                                   void* user_data, void* stream_user_data);
    static int http_end_headers_cb(nghttp3_conn* conn, int64_t stream_id, int fin,
                                   void* user_data, void* stream_user_data);
    static int http_end_stream_cb(nghttp3_conn* conn, int64_t stream_id,
                                  void* user_data, void* stream_user_data);
    static int http_stop_sending_cb(nghttp3_conn* conn, int64_t stream_id,
                                    uint64_t app_error_code, void* user_data,
                                    void* stream_user_data);
    static int http_reset_stream_cb(nghttp3_conn* conn, int64_t stream_id,
                                    uint64_t app_error_code, void* user_data,
                                    void* stream_user_data);
    static nghttp3_ssize http_read_data_cb(nghttp3_conn* conn, int64_t stream_id,
                                           nghttp3_vec* vec, size_t veccnt,
                                           uint32_t* pflags, void* user_data,
                                           void* stream_user_data);

    Http3Server* server_;
    boost::asio::ip::udp::socket& socket_;
    boost::asio::ip::udp::endpoint local_ep_;
    boost::asio::ip::udp::endpoint remote_ep_;
    boost::asio::steady_timer timer_;

    ngtcp2_conn* conn_ = nullptr;
    ngtcp2_crypto_conn_ref conn_ref_;
    nghttp3_conn* httpconn_ = nullptr;
    ngtcp2_crypto_ossl_ctx* ossl_ctx_ = nullptr;
    SSL_CTX* ssl_ctx_ = nullptr;

    ngtcp2_cid scid_;
    ngtcp2_cid dcid_;
    ngtcp2_cid ocid_;
    ngtcp2_cid original_client_dcid_;  // What client sent as DCID in Initial packet
    std::vector<uint8_t> initial_token_;
    uint32_t version_;

    std::unordered_map<int64_t, std::unique_ptr<Http3Stream>> streams_;

    // Send buffer
    std::vector<uint8_t> send_buf_;

    // Connection close buffer
    std::vector<uint8_t> conn_close_buf_;

    ngtcp2_ccerr last_error_{};
    bool closed_ = false;
    bool timer_active_ = false;
};

//==============================================================================
// Http3Server - Main HTTP/3 server using ngtcp2 + nghttp3
//==============================================================================

class Http3Server {
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
                    const uint8_t* data, size_t len);

    // Connection management
    void associate_cid(const ngtcp2_cid* cid, std::shared_ptr<Http3Connection> conn);
    void dissociate_cid(const ngtcp2_cid* cid);
    void remove_connection(std::shared_ptr<Http3Connection> conn);

private:
    void do_receive();
    void handle_packet(const boost::asio::ip::udp::endpoint& remote_ep,
                       const uint8_t* data, size_t len);
    int handle_new_connection(const boost::asio::ip::udp::endpoint& remote_ep,
                              const uint8_t* data, size_t len,
                              const ngtcp2_version_cid& vc);
    int send_version_negotiation(const boost::asio::ip::udp::endpoint& remote_ep,
                                 const ngtcp2_version_cid& vc);
    int send_stateless_reset(const boost::asio::ip::udp::endpoint& remote_ep,
                             const uint8_t* dcid, size_t dcidlen);
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
    std::unordered_map<std::string, std::shared_ptr<Http3Connection>> connections_;

    bool running_ = false;
};

//==============================================================================
// Http3Connection Implementation
//==============================================================================

Http3Connection::Http3Connection(Http3Server* server,
                                 boost::asio::ip::udp::socket& socket,
                                 const boost::asio::ip::udp::endpoint& local_ep,
                                 const boost::asio::ip::udp::endpoint& remote_ep,
                                 const ngtcp2_cid* client_scid,   // Client's Source CID (from hd.scid)
                                 const ngtcp2_cid* client_dcid,   // Client's Destination CID (from hd.dcid)
                                 const ngtcp2_cid* ocid,
                                 const uint8_t* token,
                                 size_t tokenlen,
                                 uint32_t version,
                                 SSL_CTX* ssl_ctx)
    : server_(server)
    , socket_(socket)
    , local_ep_(local_ep)
    , remote_ep_(remote_ep)
    , timer_(server->io_context())
    , ssl_ctx_(ssl_ctx)
    , version_(version)
{
    // dcid_ stores the client's source CID - this is what ngtcp2_conn_server_new expects
    // as its 'dcid' parameter (the CID that appears in client Initial as Source CID)
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

Http3Connection::~Http3Connection() {
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

bool Http3Connection::init() {
    NPRPC_HTTP3_TRACE("Http3Connection::init() starting...");

    // Create OpenSSL QUIC context
    if (ngtcp2_crypto_ossl_ctx_new(&ossl_ctx_, nullptr) != 0) {
        std::cerr << "[HTTP/3] Failed to create OpenSSL QUIC context" << std::endl;
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
    settings.log_printf = log_printf;
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
    params.initial_max_streams_uni = 3;  // Control + QPACK enc + QPACK dec
    params.max_idle_timeout = 30 * NGTCP2_SECONDS;
    params.stateless_reset_token_present = 1;
    params.active_connection_id_limit = 7;
    params.grease_quic_bit = 1;

    // Set original DCID - this is what the client sent as Destination CID in their Initial
    if (ocid_.datalen > 0) {
        // Retry case: ocid_ is the original DCID before retry
        params.original_dcid = ocid_;
        // retry_scid is what the client received in Retry (the original_client_dcid)
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
        std::cerr << "[HTTP/3] Failed to generate stateless reset token" << std::endl;
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
        .local = {
            .addr = reinterpret_cast<sockaddr*>(&local_ss),
            .addrlen = static_cast<ngtcp2_socklen>(local_ep_.size()),
        },
        .remote = {
            .addr = reinterpret_cast<sockaddr*>(&remote_ss),
            .addrlen = static_cast<ngtcp2_socklen>(remote_ep_.size()),
        },
    };

    // Create server connection
    int rv = ngtcp2_conn_server_new(&conn_, &dcid_, &scid_, &path, version_,
                                    &callbacks, &settings, &params, nullptr, this);
    if (rv != 0) {
        std::cerr << "[HTTP/3] ngtcp2_conn_server_new failed: " 
                  << ngtcp2_strerror(rv) << std::endl;
        return false;
    }

    // See example in /home/nikita/projects/nprpc/third_party/ngtcp2/examples/tls_server_session_ossl.cc

    // Create SSL object
    SSL* ssl = SSL_new(ssl_ctx_);
    if (!ssl) {
        std::cerr << "[HTTP/3] SSL_new failed: " 
                  << ERR_error_string(ERR_get_error(), nullptr) << std::endl;
        return false;
    }

    ngtcp2_crypto_ossl_ctx_set_ssl(ossl_ctx_, ssl);

    // Configure SSL for QUIC server FIRST - this sets up the QUIC TLS callbacks
    if (ngtcp2_crypto_ossl_configure_server_session(ssl) != 0) {
        std::cerr << "[HTTP/3] Failed to configure server SSL session" << std::endl;
        SSL_free(ssl);
        return false;
    }

    // Set app data AFTER configuring for QUIC (reference order)
    SSL_set_app_data(ssl, &conn_ref_);
    SSL_set_accept_state(ssl);
    SSL_set_quic_tls_early_data_enabled(ssl, 1);

    ngtcp2_conn_set_tls_native_handle(conn_, ossl_ctx_);

    print_ssl_state("After SSL setup");

    NPRPC_HTTP3_TRACE("Connection initialized for {}", remote_ep_.address().to_string());

    return true;
}

int Http3Connection::on_read(const uint8_t* data, size_t len, const ngtcp2_pkt_info* pi) {
    if (closed_) {
        return -1;
    }
  
    print_ssl_state("on_read start");

    // If we're closing, just send connection close
    if (ngtcp2_conn_in_closing_period(conn_)) {
        NPRPC_HTTP3_TRACE("Connection is in closing period, ignoring received packet");
        // TODO: Send connection close
        return 0;
    }

    if (ngtcp2_conn_in_draining_period(conn_)) {
        NPRPC_HTTP3_TRACE("Connection is in draining period, ignoring received packet");
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
        .local = {
            .addr = reinterpret_cast<sockaddr*>(&local_ss),
            .addrlen = static_cast<ngtcp2_socklen>(local_ep_.size()),
        },
        .remote = {
            .addr = reinterpret_cast<sockaddr*>(&remote_ss),
            .addrlen = static_cast<ngtcp2_socklen>(remote_ep_.size()),
        },
    };

    NPRPC_HTTP3_TRACE("Reading packet, len={}", len);

    int rv = ngtcp2_conn_read_pkt(conn_, &path, pi, data, len, timestamp_ns());
    if (rv != 0) {
        std::cerr << "[HTTP/3] ngtcp2_conn_read_pkt: " << ngtcp2_strerror(rv) << std::endl;

        // Print OpenSSL errors
        unsigned long err;
        while ((err = ERR_get_error()) != 0) {
            std::cerr << "[HTTP/3] OpenSSL error: " << ERR_error_string(err, nullptr) << std::endl;
        }

        switch (rv) {
        case NGTCP2_ERR_DRAINING:
            start_draining_period();
            return 0;
        case NGTCP2_ERR_DROP_CONN:
            closed_ = true;
            return -1;
        case NGTCP2_ERR_CRYPTO:
            if (!last_error_.error_code) {
                auto alert = ngtcp2_conn_get_tls_alert(conn_);
                std::cerr << "[HTTP/3] TLS alert: " << (int)alert << std::endl;
                ngtcp2_ccerr_set_tls_alert(&last_error_, alert, nullptr, 0);
            }
            break;
        default:
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

int Http3Connection::on_write() {
    if (closed_) {
        return -1;
    }

    if (ngtcp2_conn_in_closing_period(conn_) || ngtcp2_conn_in_draining_period(conn_)) {
        return 0;
    }

    NPRPC_HTTP3_TRACE("on_write called");

    auto ts = timestamp_ns();
    ngtcp2_path_storage ps;
    ngtcp2_pkt_info pi;

    ngtcp2_path_storage_zero(&ps);

    // Write loop
    for (;;) {
        int64_t stream_id = -1;
        int fin = 0;
        std::array<nghttp3_vec, 16> vec;
        nghttp3_ssize sveccnt = 0;

        // Get data from HTTP/3 layer if available
        if (httpconn_ && ngtcp2_conn_get_max_data_left(conn_)) {
            sveccnt = nghttp3_conn_writev_stream(httpconn_, &stream_id, &fin,
                                                 vec.data(), vec.size());
            if (sveccnt < 0) {
                std::cerr << "[HTTP/3] nghttp3_conn_writev_stream: " 
                          << nghttp3_strerror(static_cast<int>(sveccnt)) << std::endl;
                ngtcp2_ccerr_set_application_error(
                    &last_error_,
                    nghttp3_err_infer_quic_app_error_code(static_cast<int>(sveccnt)),
                    nullptr, 0);
                return handle_error();
            }
        }

        ngtcp2_ssize ndatalen;
        uint32_t flags = NGTCP2_WRITE_STREAM_FLAG_MORE |
                         NGTCP2_WRITE_STREAM_FLAG_PADDING;
        if (fin) {
            flags |= NGTCP2_WRITE_STREAM_FLAG_FIN;
        }

        NPRPC_HTTP3_TRACE("on_write: calling ngtcp2_conn_writev_stream, stream_id={}, sveccnt={}", 
                          stream_id, sveccnt);

        auto nwrite = ngtcp2_conn_writev_stream(
            conn_, &ps.path, &pi, send_buf_.data(), MAX_UDP_PAYLOAD_SIZE,
            &ndatalen, flags, stream_id,
            reinterpret_cast<const ngtcp2_vec*>(vec.data()),
            static_cast<size_t>(sveccnt), ts);
        
        NPRPC_HTTP3_TRACE("on_write: ngtcp2_conn_writev_stream returned nwrite={}, ndatalen={}", 
                          nwrite, ndatalen);

        if (nwrite < 0) {
            switch (nwrite) {
            case NGTCP2_ERR_STREAM_DATA_BLOCKED:
                if (httpconn_) {
                    nghttp3_conn_block_stream(httpconn_, stream_id);
                }
                continue;
            case NGTCP2_ERR_STREAM_SHUT_WR:
                if (httpconn_) {
                    nghttp3_conn_shutdown_stream_write(httpconn_, stream_id);
                }
                continue;
            case NGTCP2_ERR_WRITE_MORE:
                if (httpconn_ && ndatalen >= 0) {
                    auto rv = nghttp3_conn_add_write_offset(
                        httpconn_, stream_id, static_cast<size_t>(ndatalen));
                    if (rv != 0) {
                        std::cerr << "[HTTP/3] nghttp3_conn_add_write_offset: "
                                  << nghttp3_strerror(rv) << std::endl;
                        ngtcp2_ccerr_set_application_error(
                            &last_error_, nghttp3_err_infer_quic_app_error_code(rv),
                            nullptr, 0);
                        return handle_error();
                    }
                }
                continue;
            }

            std::cerr << "[HTTP/3] ngtcp2_conn_writev_stream: " 
                      << ngtcp2_strerror(static_cast<int>(nwrite)) << std::endl;
            ngtcp2_ccerr_set_liberr(&last_error_, static_cast<int>(nwrite), nullptr, 0);
            return handle_error();
        }

        if (ndatalen >= 0 && httpconn_) {
            auto rv = nghttp3_conn_add_write_offset(
                httpconn_, stream_id, static_cast<size_t>(ndatalen));
            if (rv != 0) {
                std::cerr << "[HTTP/3] nghttp3_conn_add_write_offset: "
                          << nghttp3_strerror(rv) << std::endl;
                ngtcp2_ccerr_set_application_error(
                    &last_error_, nghttp3_err_infer_quic_app_error_code(rv),
                    nullptr, 0);
                return handle_error();
            }
        }

        if (nwrite == 0) {
            NPRPC_HTTP3_TRACE("on_write: no more packets to write");
            break;  // Nothing more to write
        }

        // Send the packet
        // Check what type of packet we're sending
#if NPRPC_ENABLE_HTTP3_TRACE
        uint8_t first_byte = send_buf_[0];
        const char* pkt_type = "unknown";
        if (first_byte & 0x80) {  // Long header
            uint8_t type = (first_byte >> 4) & 0x03;
            switch (type) {
            case 0: pkt_type = "Initial"; break;
            case 1: pkt_type = "0-RTT"; break;
            case 2: pkt_type = "Handshake"; break;
            case 3: pkt_type = "Retry"; break;
            }
        } else {
            pkt_type = "Short (1-RTT)";
        }
        NPRPC_HTTP3_TRACE("on_write: sending {} bytes, type={}", nwrite, pkt_type);
#endif
        server_->send_packet(remote_ep_, send_buf_.data(), static_cast<size_t>(nwrite));
    }

    ngtcp2_conn_update_pkt_tx_time(conn_, ts);
    schedule_timer();

    return 0;
}

int Http3Connection::handle_expiry() {
    auto now = timestamp_ns();
    int rv = ngtcp2_conn_handle_expiry(conn_, now);
    if (rv != 0) {
        std::cerr << "[HTTP/3] ngtcp2_conn_handle_expiry: " << ngtcp2_strerror(rv) << std::endl;
        ngtcp2_ccerr_set_liberr(&last_error_, rv, nullptr, 0);
        return handle_error();
    }
    return 0;
}

void Http3Connection::schedule_timer() {
    auto expiry = ngtcp2_conn_get_expiry(conn_);
    auto now = timestamp_ns();

    if (expiry <= now) {
        // Already expired, handle immediately
        boost::asio::post(server_->io_context(), [self = shared_from_this()]() {
            if (!self->closed_) {
                self->handle_expiry();
                self->on_write();
            }
        });
        return;
    }

    auto timeout = std::chrono::nanoseconds(expiry - now);
    timer_.expires_after(timeout);
    timer_.async_wait([self = shared_from_this()](boost::system::error_code ec) {
        if (ec || self->closed_) {
            return;
        }
        self->handle_expiry();
        self->on_write();
    });
}

void Http3Connection::signal_write() {
    boost::asio::post(server_->io_context(), [self = shared_from_this()]() {
        if (!self->closed_) {
            self->on_write();
        }
    });
}

int Http3Connection::setup_httpconn() {
    if (httpconn_) {
        return 0;
    }

    // Need at least 3 unidirectional streams for HTTP/3
    if (ngtcp2_conn_get_streams_uni_left(conn_) < 3) {
        std::cerr << "[HTTP/3] Peer does not allow 3 unidirectional streams" << std::endl;
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

    auto mem = nghttp3_mem_default();

    int rv = nghttp3_conn_server_new(&httpconn_, &callbacks, &settings, mem, this);
    if (rv != 0) {
        std::cerr << "[HTTP/3] nghttp3_conn_server_new: " << nghttp3_strerror(rv) << std::endl;
        return -1;
    }

    auto params = ngtcp2_conn_get_local_transport_params(conn_);
    nghttp3_conn_set_max_client_streams_bidi(httpconn_, params->initial_max_streams_bidi);

    // Open control stream
    int64_t ctrl_stream_id;
    rv = ngtcp2_conn_open_uni_stream(conn_, &ctrl_stream_id, nullptr);
    if (rv != 0) {
        std::cerr << "[HTTP/3] Failed to open control stream: " << ngtcp2_strerror(rv) << std::endl;
        return -1;
    }

    rv = nghttp3_conn_bind_control_stream(httpconn_, ctrl_stream_id);
    if (rv != 0) {
        std::cerr << "[HTTP/3] nghttp3_conn_bind_control_stream: " << nghttp3_strerror(rv) << std::endl;
        return -1;
    }

    // Open QPACK streams
    int64_t qpack_enc_stream_id, qpack_dec_stream_id;

    rv = ngtcp2_conn_open_uni_stream(conn_, &qpack_enc_stream_id, nullptr);
    if (rv != 0) {
        std::cerr << "[HTTP/3] Failed to open QPACK encoder stream: " << ngtcp2_strerror(rv) << std::endl;
        return -1;
    }

    rv = ngtcp2_conn_open_uni_stream(conn_, &qpack_dec_stream_id, nullptr);
    if (rv != 0) {
        std::cerr << "[HTTP/3] Failed to open QPACK decoder stream: " << ngtcp2_strerror(rv) << std::endl;
        return -1;
    }

    rv = nghttp3_conn_bind_qpack_streams(httpconn_, qpack_enc_stream_id, qpack_dec_stream_id);
    if (rv != 0) {
        std::cerr << "[HTTP/3] nghttp3_conn_bind_qpack_streams: " << nghttp3_strerror(rv) << std::endl;
        return -1;
    }

    NPRPC_HTTP3_TRACE("HTTP/3 connection setup complete");

    return 0;
}

Http3Stream* Http3Connection::find_stream(int64_t stream_id) {
    auto it = streams_.find(stream_id);
    return it != streams_.end() ? it->second.get() : nullptr;
}

Http3Stream* Http3Connection::create_stream(int64_t stream_id) {
    auto stream = std::make_unique<Http3Stream>();
    stream->stream_id = stream_id;
    auto* ptr = stream.get();
    streams_[stream_id] = std::move(stream);
    return ptr;
}

void Http3Connection::remove_stream(int64_t stream_id) {
    streams_.erase(stream_id);
}

int Http3Connection::recv_stream_data(uint32_t flags, int64_t stream_id,
                                      const uint8_t* data, size_t datalen) {
    if (!httpconn_) {
        return 0;
    }

    auto nconsumed = nghttp3_conn_read_stream2(
        httpconn_, stream_id, data, datalen,
        (flags & NGTCP2_STREAM_DATA_FLAG_FIN) != 0,
        ngtcp2_conn_get_timestamp(conn_));
    if (nconsumed < 0) {
        std::cerr << "[HTTP/3] nghttp3_conn_read_stream: " 
                  << nghttp3_strerror(static_cast<int>(nconsumed)) << std::endl;
        ngtcp2_ccerr_set_application_error(
            &last_error_,
            nghttp3_err_infer_quic_app_error_code(static_cast<int>(nconsumed)),
            nullptr, 0);
        return -1;
    }

    ngtcp2_conn_extend_max_stream_offset(conn_, stream_id, static_cast<uint64_t>(nconsumed));
    ngtcp2_conn_extend_max_offset(conn_, static_cast<uint64_t>(nconsumed));

    return 0;
}

int Http3Connection::acked_stream_data_offset(int64_t stream_id, uint64_t datalen) {
    if (!httpconn_) {
        return 0;
    }

    int rv = nghttp3_conn_add_ack_offset(httpconn_, stream_id, datalen);
    if (rv != 0) {
        std::cerr << "[HTTP/3] nghttp3_conn_add_ack_offset: " << nghttp3_strerror(rv) << std::endl;
        return -1;
    }

    return 0;
}

void Http3Connection::extend_max_remote_streams_bidi(uint64_t max_streams) {
    if (httpconn_) {
        nghttp3_conn_set_max_client_streams_bidi(httpconn_, max_streams);
    }
}

void Http3Connection::extend_max_stream_data(int64_t stream_id, uint64_t max_data) {
    if (httpconn_) {
        nghttp3_conn_unblock_stream(httpconn_, stream_id);
    }
}

// HTTP/3 header handling
void Http3Connection::http_begin_headers(int64_t stream_id) {
    auto* stream = find_stream(stream_id);
    if (!stream) {
        // Stream might not exist yet if on_stream_open wasn't called
        // This can happen for client-initiated streams
        stream = create_stream(stream_id);
    }
    if (stream) {
        nghttp3_conn_set_stream_user_data(httpconn_, stream_id, stream);
    }
}

void Http3Connection::http_recv_header(Http3Stream* stream, int32_t token,
                                       nghttp3_rcbuf* name, nghttp3_rcbuf* value) {
    auto v = nghttp3_rcbuf_get_buf(value);

    switch (token) {
    case NGHTTP3_QPACK_TOKEN__PATH:
        stream->path = std::string(reinterpret_cast<const char*>(v.base), v.len);
        break;
    case NGHTTP3_QPACK_TOKEN__METHOD:
        stream->method = std::string(reinterpret_cast<const char*>(v.base), v.len);
        break;
    case NGHTTP3_QPACK_TOKEN__AUTHORITY:
        stream->authority = std::string(reinterpret_cast<const char*>(v.base), v.len);
        break;
    case NGHTTP3_QPACK_TOKEN_CONTENT_TYPE:
        stream->content_type = std::string(reinterpret_cast<const char*>(v.base), v.len);
        break;
    case NGHTTP3_QPACK_TOKEN_CONTENT_LENGTH:
        stream->content_length = std::stoull(std::string(
            reinterpret_cast<const char*>(v.base), v.len));
        break;
    }
}

int Http3Connection::http_end_headers(Http3Stream* stream) {
    NPRPC_HTTP3_TRACE("Request: {} {}", stream->method, stream->path);
    return 0;
}

int Http3Connection::http_end_stream(Http3Stream* stream) {
    return start_response(stream);
}

void Http3Connection::http_stream_close(int64_t stream_id, uint64_t app_error_code) {
    if (!ngtcp2_is_bidi_stream(stream_id)) {
        return;
    }

    if (!ngtcp2_conn_is_local_stream(conn_, stream_id)) {
        ngtcp2_conn_extend_max_streams_bidi(conn_, 1);
    }

    remove_stream(stream_id);
}

int Http3Connection::start_response(Http3Stream* stream) {
    // Handle the HTTP request
    if (stream->method == "GET" || stream->method == "HEAD") {
        // Serve static file
        std::string request_path = stream->path;
        
        // Check if path is a directory or handle index.html
        if (request_path == "/" || request_path.empty()) {
            request_path = "/index.html";
        }
        
        auto file_path = path_cat(g_cfg.http_root_dir, request_path);
        
        NPRPC_HTTP3_TRACE("Serving file: {} (root={}, path={})", 
                          file_path, g_cfg.http_root_dir, request_path);

        std::ifstream file(file_path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            // 404 Not Found
            NPRPC_HTTP3_TRACE("File not found: {}", file_path);
            std::string body = "<!DOCTYPE html><html><body><h1>404 Not Found</h1></body></html>";
            return send_response(stream, 404, "text/html", 
                std::vector<uint8_t>(body.begin(), body.end()));
        }

        auto size = file.tellg();
        NPRPC_HTTP3_TRACE("File size: {}", (long long)size);
        
        if (size < 0) {
            // tellg() failed - might be a directory
            std::string body = "<!DOCTYPE html><html><body><h1>403 Forbidden</h1></body></html>";
            return send_response(stream, 403, "text/html",
                std::vector<uint8_t>(body.begin(), body.end()));
        }
        
        if (size > 100 * 1024 * 1024) {  // Limit to 100MB
            std::string body = "<!DOCTYPE html><html><body><h1>500 File Too Large</h1></body></html>";
            return send_response(stream, 500, "text/html",
                std::vector<uint8_t>(body.begin(), body.end()));
        }
        file.seekg(0, std::ios::beg);

        std::vector<uint8_t> body;
        if (stream->method != "HEAD") {
            body.resize(static_cast<size_t>(size));
            file.read(reinterpret_cast<char*>(body.data()), size);
        }

        auto content_type = std::string(mime_type(request_path));
        return send_response(stream, 200, content_type, body);
    }
    else if (stream->method == "POST") {
        // Handle POST request (e.g., RPC)
        // For now, return 200 OK
        std::string body = "OK";
        return send_response(stream, 200, "text/plain",
            std::vector<uint8_t>(body.begin(), body.end()));
    }
    else {
        // Method not allowed
        std::string body = "<!DOCTYPE html><html><body><h1>405 Method Not Allowed</h1></body></html>";
        return send_response(stream, 405, "text/html",
            std::vector<uint8_t>(body.begin(), body.end()));
    }
}

int Http3Connection::send_response(Http3Stream* stream, unsigned int status_code,
                                   const std::string& content_type,
                                   const std::vector<uint8_t>& body) {
    // Store response data in stream to keep it alive
    stream->response_body = body;
    stream->response_data = stream->response_body.data();
    stream->response_len = stream->response_body.size();
    stream->response_offset = 0;

    // Build headers
    stream->status_str = std::to_string(status_code);
    stream->content_type_str = content_type;
    stream->content_length_str = std::to_string(body.size());

    std::array<nghttp3_nv, 4> nva;
    nva[0] = {
        .name = reinterpret_cast<uint8_t*>(const_cast<char*>(":status")),
        .value = reinterpret_cast<uint8_t*>(stream->status_str.data()),
        .namelen = 7,
        .valuelen = stream->status_str.size(),
        .flags = NGHTTP3_NV_FLAG_NONE,
    };
    nva[1] = {
        .name = reinterpret_cast<uint8_t*>(const_cast<char*>("server")),
        .value = reinterpret_cast<uint8_t*>(const_cast<char*>("nprpc/nghttp3")),
        .namelen = 6,
        .valuelen = 13,
        .flags = NGHTTP3_NV_FLAG_NONE,
    };
    nva[2] = {
        .name = reinterpret_cast<uint8_t*>(const_cast<char*>("content-type")),
        .value = reinterpret_cast<uint8_t*>(stream->content_type_str.data()),
        .namelen = 12,
        .valuelen = stream->content_type_str.size(),
        .flags = NGHTTP3_NV_FLAG_NONE,
    };
    nva[3] = {
        .name = reinterpret_cast<uint8_t*>(const_cast<char*>("content-length")),
        .value = reinterpret_cast<uint8_t*>(stream->content_length_str.data()),
        .namelen = 14,
        .valuelen = stream->content_length_str.size(),
        .flags = NGHTTP3_NV_FLAG_NONE,
    };

    nghttp3_data_reader dr{
        .read_data = http_read_data_cb,
    };

    int rv = nghttp3_conn_submit_response(httpconn_, stream->stream_id,
                                          nva.data(), nva.size(), &dr);
    if (rv != 0) {
        std::cerr << "[HTTP/3] nghttp3_conn_submit_response: " << nghttp3_strerror(rv) << std::endl;
        return -1;
    }

    return 0;
}

int Http3Connection::handle_error() {
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
        server_->send_packet(remote_ep_, conn_close_buf_.data(), conn_close_buf_.size());
    }

    return 0;
}

void Http3Connection::start_draining_period() {
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

int Http3Connection::start_closing_period() {
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
        std::cerr << "[HTTP/3] ngtcp2_conn_write_connection_close: " 
                  << ngtcp2_strerror(static_cast<int>(n)) << std::endl;
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
void Http3Connection::print_ssl_state(std::string_view prefix) {
    // Check SSL state
    auto ssl = ngtcp2_crypto_ossl_ctx_get_ssl(ossl_ctx_);
    if (ssl) {
        NPRPC_HTTP3_TRACE("SSL [{}]: state: {}", prefix, SSL_state_string_long(ssl));
        int ssl_err = SSL_get_error(ssl, 0);
        if (ssl_err != SSL_ERROR_NONE) {
            NPRPC_HTTP3_TRACE("SSL [{}]: error: {}", prefix, ERR_error_string(ssl_err, nullptr));
        }
        // Check pending errors
        unsigned long err;
        while ((err = ERR_get_error()) != 0) {
            NPRPC_HTTP3_TRACE("SSL [{}]: pending error: {}", prefix, ERR_error_string(err, nullptr));
        }
    }
}

//==============================================================================
// Static ngtcp2 callbacks
//==============================================================================

int Http3Connection::on_handshake_completed(ngtcp2_conn* conn, void* user_data) {
    auto h = static_cast<Http3Connection*>(user_data);

    NPRPC_HTTP3_TRACE("Handshake completed with {}", h->remote_ep_.address().to_string());

    return 0;
}

int Http3Connection::on_recv_stream_data(ngtcp2_conn* conn, uint32_t flags,
                                         int64_t stream_id, uint64_t offset,
                                         const uint8_t* data, size_t datalen,
                                         void* user_data, void* stream_user_data) {
    auto h = static_cast<Http3Connection*>(user_data);
    if (h->recv_stream_data(flags, stream_id, data, datalen) != 0) {
        return NGTCP2_ERR_CALLBACK_FAILURE;
    }

    return 0;
}

int Http3Connection::on_acked_stream_data_offset(ngtcp2_conn* conn, int64_t stream_id,
                                                 uint64_t offset, uint64_t datalen,
                                                 void* user_data, void* stream_user_data) {
    auto h = static_cast<Http3Connection*>(user_data);
    if (h->acked_stream_data_offset(stream_id, datalen) != 0) {
        return NGTCP2_ERR_CALLBACK_FAILURE;
    }
    return 0;
}

int Http3Connection::on_stream_open(ngtcp2_conn* conn, int64_t stream_id, void* user_data) {
    auto h = static_cast<Http3Connection*>(user_data);

    if (!ngtcp2_is_bidi_stream(stream_id)) {
        return 0;
    }

    h->create_stream(stream_id);
    return 0;
}

int Http3Connection::on_stream_close(ngtcp2_conn* conn, uint32_t flags,
                                     int64_t stream_id, uint64_t app_error_code,
                                     void* user_data, void* stream_user_data) {
    auto h = static_cast<Http3Connection*>(user_data);

    if (!(flags & NGTCP2_STREAM_CLOSE_FLAG_APP_ERROR_CODE_SET)) {
        app_error_code = NGHTTP3_H3_NO_ERROR;
    }

    if (h->httpconn_) {
        int rv = nghttp3_conn_close_stream(h->httpconn_, stream_id, app_error_code);
        if (rv != 0 && rv != NGHTTP3_ERR_STREAM_NOT_FOUND) {
            std::cerr << "[HTTP/3] nghttp3_conn_close_stream: " << nghttp3_strerror(rv) << std::endl;
            ngtcp2_ccerr_set_application_error(
                &h->last_error_, nghttp3_err_infer_quic_app_error_code(rv),
                nullptr, 0);
            return NGTCP2_ERR_CALLBACK_FAILURE;
        }
    }

    h->http_stream_close(stream_id, app_error_code);
    return 0;
}

int Http3Connection::on_extend_max_remote_streams_bidi(ngtcp2_conn* conn,
                                                       uint64_t max_streams,
                                                       void* user_data) {
    auto h = static_cast<Http3Connection*>(user_data);
    h->extend_max_remote_streams_bidi(max_streams);
    return 0;
}

int Http3Connection::on_extend_max_stream_data(ngtcp2_conn* conn, int64_t stream_id,
                                               uint64_t max_data, void* user_data,
                                               void* stream_user_data) {
    auto h = static_cast<Http3Connection*>(user_data);
    h->extend_max_stream_data(stream_id, max_data);
    return 0;
}

int Http3Connection::on_recv_tx_key(ngtcp2_conn* conn, ngtcp2_encryption_level level,
                                    void* user_data) {
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

int Http3Connection::on_get_new_connection_id(ngtcp2_conn* conn, ngtcp2_cid* cid,
                                              uint8_t* token, size_t cidlen,
                                              void* user_data) {
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

int Http3Connection::on_remove_connection_id(ngtcp2_conn* conn, const ngtcp2_cid* cid,
                                             void* user_data) {
    auto h = static_cast<Http3Connection*>(user_data);
    h->server_->dissociate_cid(cid);
    return 0;
}

void Http3Connection::on_rand(uint8_t* dest, size_t destlen, 
                              const ngtcp2_rand_ctx* rand_ctx) {
    random_bytes(dest, destlen);
}

//==============================================================================
// Static nghttp3 callbacks  
//==============================================================================

int Http3Connection::http_acked_stream_data_cb(nghttp3_conn* conn, int64_t stream_id,
                                               uint64_t datalen, void* user_data,
                                               void* stream_user_data) {
    // Stream data was acknowledged - we could free resources here
    return 0;
}

int Http3Connection::http_recv_data_cb(nghttp3_conn* conn, int64_t stream_id,
                                       const uint8_t* data, size_t datalen,
                                       void* user_data, void* stream_user_data) {
    auto h = static_cast<Http3Connection*>(user_data);
    auto stream = static_cast<Http3Stream*>(stream_user_data);

    if (stream) {
        stream->request_body.insert(stream->request_body.end(), data, data + datalen);
    }

    // Extend flow control
    ngtcp2_conn_extend_max_stream_offset(h->conn_, stream_id, datalen);
    ngtcp2_conn_extend_max_offset(h->conn_, datalen);

    return 0;
}

int Http3Connection::http_deferred_consume_cb(nghttp3_conn* conn, int64_t stream_id,
                                              size_t nconsumed, void* user_data,
                                              void* stream_user_data) {
    auto h = static_cast<Http3Connection*>(user_data);
    ngtcp2_conn_extend_max_stream_offset(h->conn_, stream_id, nconsumed);
    ngtcp2_conn_extend_max_offset(h->conn_, nconsumed);
    return 0;
}

int Http3Connection::http_begin_headers_cb(nghttp3_conn* conn, int64_t stream_id,
                                           void* user_data, void* stream_user_data) {
    auto h = static_cast<Http3Connection*>(user_data);
    h->http_begin_headers(stream_id);
    return 0;
}

int Http3Connection::http_recv_header_cb(nghttp3_conn* conn, int64_t stream_id,
                                         int32_t token, nghttp3_rcbuf* name,
                                         nghttp3_rcbuf* value, uint8_t flags,
                                         void* user_data, void* stream_user_data) {
    auto h = static_cast<Http3Connection*>(user_data);
    auto stream = static_cast<Http3Stream*>(stream_user_data);

    if (stream) {
        h->http_recv_header(stream, token, name, value);
    }

    return 0;
}

int Http3Connection::http_end_headers_cb(nghttp3_conn* conn, int64_t stream_id, int fin,
                                         void* user_data, void* stream_user_data) {
    auto h = static_cast<Http3Connection*>(user_data);
    auto stream = static_cast<Http3Stream*>(stream_user_data);

    if (stream && h->http_end_headers(stream) != 0) {
        return NGHTTP3_ERR_CALLBACK_FAILURE;
    }

    return 0;
}

int Http3Connection::http_end_stream_cb(nghttp3_conn* conn, int64_t stream_id,
                                        void* user_data, void* stream_user_data) {
    auto h = static_cast<Http3Connection*>(user_data);
    auto stream = static_cast<Http3Stream*>(stream_user_data);

    try {
        if (stream && h->http_end_stream(stream) != 0) {
            return NGHTTP3_ERR_CALLBACK_FAILURE;
        }
    } catch (const std::exception& e) {
        std::cerr << "[HTTP/3] Exception in http_end_stream_cb: " << e.what() << std::endl;
        return NGHTTP3_ERR_CALLBACK_FAILURE;
    }

    return 0;
}

int Http3Connection::http_stop_sending_cb(nghttp3_conn* conn, int64_t stream_id,
                                          uint64_t app_error_code, void* user_data,
                                          void* stream_user_data) {
    auto h = static_cast<Http3Connection*>(user_data);
    ngtcp2_conn_shutdown_stream_read(h->conn_, 0, stream_id, app_error_code);
    return 0;
}

int Http3Connection::http_reset_stream_cb(nghttp3_conn* conn, int64_t stream_id,
                                          uint64_t app_error_code, void* user_data,
                                          void* stream_user_data) {
    auto h = static_cast<Http3Connection*>(user_data);
    ngtcp2_conn_shutdown_stream_write(h->conn_, 0, stream_id, app_error_code);
    return 0;
}

nghttp3_ssize Http3Connection::http_read_data_cb(nghttp3_conn* conn, int64_t stream_id,
                                                 nghttp3_vec* vec, size_t veccnt,
                                                 uint32_t* pflags, void* user_data,
                                                 void* stream_user_data) {
    auto stream = static_cast<Http3Stream*>(stream_user_data);

    if (!stream || !stream->response_data) {
        *pflags |= NGHTTP3_DATA_FLAG_EOF;
        return 0;
    }

    size_t remaining = stream->response_len - stream->response_offset;

    if (remaining == 0) {
        *pflags |= NGHTTP3_DATA_FLAG_EOF;
        return 0;
    }

    vec[0].base = const_cast<uint8_t*>(stream->response_data + stream->response_offset);
    vec[0].len = remaining;
    stream->response_offset += remaining;

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

Http3Server::~Http3Server() {
    stop();
}

bool Http3Server::start() {
    // Initialize static secret for tokens
    init_static_secret();

    // Initialize ngtcp2 crypto
    if (ngtcp2_crypto_ossl_init() != 0) {
        std::cerr << "[HTTP/3] Failed to initialize ngtcp2 crypto" << std::endl;
        return false;
    }

    // Create SSL context
    ssl_ctx_ = SSL_CTX_new(TLS_server_method());
    if (!ssl_ctx_) {
        std::cerr << "[HTTP/3] Failed to create SSL context: " 
                  << ERR_error_string(ERR_get_error(), nullptr) << std::endl;
        return false;
    }

    // Set TLS 1.3 minimum (required for QUIC)
    SSL_CTX_set_min_proto_version(ssl_ctx_, TLS1_3_VERSION);
    SSL_CTX_set_max_proto_version(ssl_ctx_, TLS1_3_VERSION);

    // Enable early data
    SSL_CTX_set_max_early_data(ssl_ctx_, UINT32_MAX);

    // SSL options
    SSL_CTX_set_options(ssl_ctx_, 
        (SSL_OP_ALL & ~SSL_OP_DONT_INSERT_EMPTY_FRAGMENTS) |
        SSL_OP_SINGLE_ECDH_USE |
        SSL_OP_CIPHER_SERVER_PREFERENCE |
        SSL_OP_NO_ANTI_REPLAY);

    // Set ciphersuites for TLS 1.3
    if (SSL_CTX_set_ciphersuites(ssl_ctx_, crypto_default_ciphers()) != 1) {
        std::cerr << "[HTTP/3] Failed to set ciphersuites" << std::endl;
        SSL_CTX_free(ssl_ctx_);
        ssl_ctx_ = nullptr;
        return false;
    }

    // Set groups for key exchange
    if (SSL_CTX_set1_groups_list(ssl_ctx_, crypto_default_groups()) != 1) {
        std::cerr << "[HTTP/3] Failed to set groups" << std::endl;
        SSL_CTX_free(ssl_ctx_);
        ssl_ctx_ = nullptr;
        return false;
    }

    SSL_CTX_set_mode(ssl_ctx_, SSL_MODE_RELEASE_BUFFERS);

    // Set ALPN callback for HTTP/3
    SSL_CTX_set_alpn_select_cb(ssl_ctx_, [](SSL* ssl, const unsigned char** out,
                                            unsigned char* outlen,
                                            const unsigned char* in, unsigned int inlen,
                                            void* arg) -> int {
        std::cerr << "[TRACE][HTTP/3] ALPN callback called, inlen=" << inlen << std::endl;
        
        // Print received ALPN protos
        for (unsigned int i = 0; i < inlen;) {
            uint8_t len = in[i];
            if (i + 1 + len > inlen) break;
            std::string proto(reinterpret_cast<const char*>(in + i + 1), len);
            std::cerr << "[TRACE][HTTP/3] ALPN proto offered: '" << proto << "'" << std::endl;
            i += 1 + len;
        }
        
        // Look for h3 ALPN
        for (unsigned int i = 0; i < inlen;) {
            uint8_t len = in[i];
            if (i + 1 + len > inlen) {
                break;
            }
            if (len == 2 && in[i + 1] == 'h' && in[i + 2] == '3') {
                *out = in + i + 1;
                *outlen = len;
                std::cerr << "[TRACE][HTTP/3] ALPN selected: h3" << std::endl;
                return SSL_TLSEXT_ERR_OK;
            }
            i += 1 + len;
        }
        std::cerr << "[TRACE][HTTP/3] ALPN selection failed - no h3 found" << std::endl;
        return SSL_TLSEXT_ERR_ALERT_FATAL;
    }, nullptr);

    // Load certificate and key
    if (SSL_CTX_use_certificate_chain_file(ssl_ctx_, cert_file_.c_str()) != 1) {
        std::cerr << "[HTTP/3] Failed to load certificate: " 
                  << ERR_error_string(ERR_get_error(), nullptr) << std::endl;
        SSL_CTX_free(ssl_ctx_);
        ssl_ctx_ = nullptr;
        return false;
    }

    if (SSL_CTX_use_PrivateKey_file(ssl_ctx_, key_file_.c_str(), SSL_FILETYPE_PEM) != 1) {
        std::cerr << "[HTTP/3] Failed to load private key: " 
                  << ERR_error_string(ERR_get_error(), nullptr) << std::endl;
        SSL_CTX_free(ssl_ctx_);
        ssl_ctx_ = nullptr;
        return false;
    }

    if (SSL_CTX_check_private_key(ssl_ctx_) != 1) {
        std::cerr << "[HTTP/3] Certificate and private key mismatch" << std::endl;
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
        std::cerr << "[HTTP/3] Failed to open socket: " << ec.message() << std::endl;
        SSL_CTX_free(ssl_ctx_);
        ssl_ctx_ = nullptr;
        return false;
    }

    // Allow IPv4 connections too (dual-stack)
    socket_.set_option(boost::asio::ip::v6_only(false), ec);
    socket_.set_option(boost::asio::socket_base::reuse_address(true), ec);

    // Increase receive buffer size
    socket_.set_option(boost::asio::socket_base::receive_buffer_size(2 * 1024 * 1024), ec);
    socket_.set_option(boost::asio::socket_base::send_buffer_size(2 * 1024 * 1024), ec);

    socket_.bind(boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v6(), port_), ec);
    if (ec) {
        std::cerr << "[HTTP/3] Failed to bind socket: " << ec.message() << std::endl;
        SSL_CTX_free(ssl_ctx_);
        ssl_ctx_ = nullptr;
        return false;
    }

    // Get local endpoint
    local_ep_ = socket_.local_endpoint();

    running_ = true;

    NPRPC_HTTP3_TRACE("Server listening on port {} (nghttp3/ngtcp2 backend)", port_);

    // Start receiving
    do_receive();

    return true;
}

void Http3Server::stop() {
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
                             const uint8_t* data, size_t len) {
    boost::system::error_code ec;
    socket_.send_to(boost::asio::buffer(data, len), remote_ep, 0, ec);
    if (ec) {
        if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
            std::cerr << "[HTTP/3] send_to failed: " << ec.message() << std::endl;
        }
        return -1;
    }
    return 0;
}

void Http3Server::associate_cid(const ngtcp2_cid* cid, std::shared_ptr<Http3Connection> conn) {
    std::string key = cid_to_string(cid);
    std::lock_guard lock(mutex_);
    connections_[key] = conn;
}

void Http3Server::dissociate_cid(const ngtcp2_cid* cid) {
    std::string key = cid_to_string(cid);
    std::lock_guard lock(mutex_);
    connections_.erase(key);
}

void Http3Server::remove_connection(std::shared_ptr<Http3Connection> conn) {
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

void Http3Server::do_receive() {
    if (!running_) {
        return;
    }

    socket_.async_receive_from(
        boost::asio::buffer(recv_buf_),
        remote_ep_,
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
                                const uint8_t* data, size_t len) {
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
        if (conn->on_read(data, len, &pi) == 0) {
            conn->on_write();
        }
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
    //       client_dcid is what client sent as Destination CID (from hd.dcid/vc.dcid)
    conn = std::make_shared<Http3Connection>(
        this, socket_, local_ep_, remote_ep,
        &scid, &dcid, nullptr,  // client_scid=scid, client_dcid=dcid, no retry token initially
        hd.token, hd.tokenlen,
        hd.version, ssl_ctx_);

    if (!conn->init()) {
        std::cerr << "[HTTP/3] Failed to initialize connection" << std::endl;
        return;
    }

    // Associate both DCID and SCID with the connection
    associate_cid(&dcid, conn);
    associate_cid(&conn->scid(), conn);

    NPRPC_HTTP3_TRACE("New connection from {}", remote_ep.address().to_string());

    // Feed the initial packet
    ngtcp2_pkt_info pi{};
    if (conn->on_read(data, len, &pi) == 0) {
        conn->on_write();
    }
}

int Http3Server::send_version_negotiation(const boost::asio::ip::udp::endpoint& remote_ep,
                                          const ngtcp2_version_cid& vc) {
    std::array<uint8_t, NGTCP2_MAX_UDP_PAYLOAD_SIZE> buf;

    std::array<uint32_t, 2> sv = {
        NGTCP2_PROTO_VER_V1,
        NGTCP2_PROTO_VER_V2,
    };

    auto nwrite = ngtcp2_pkt_write_version_negotiation(
        buf.data(), buf.size(),
        std::rand() % 256, vc.scid, vc.scidlen, vc.dcid, vc.dcidlen,
        sv.data(), sv.size());

    if (nwrite < 0) {
        std::cerr << "[HTTP/3] ngtcp2_pkt_write_version_negotiation: "
                  << ngtcp2_strerror(static_cast<int>(nwrite)) << std::endl;
        return -1;
    }

    return send_packet(remote_ep, buf.data(), static_cast<size_t>(nwrite));
}

int Http3Server::send_stateless_reset(const boost::asio::ip::udp::endpoint& remote_ep,
                                      const uint8_t* dcid, size_t dcidlen) {
    std::array<uint8_t, NGTCP2_MAX_UDP_PAYLOAD_SIZE> buf;

    ngtcp2_cid cid;
    cid.datalen = dcidlen;
    memcpy(cid.data, dcid, dcidlen);

    std::array<uint8_t, NGTCP2_STATELESS_RESET_TOKENLEN> token;
    if (ngtcp2_crypto_generate_stateless_reset_token(
            token.data(), g_static_secret.data(), g_static_secret.size(), &cid) != 0) {
        return -1;
    }

    // Generate random padding
    size_t randlen = std::rand() % 64 + 40;
    std::vector<uint8_t> rand_data(randlen);
    random_bytes(rand_data.data(), randlen);

    auto nwrite = ngtcp2_pkt_write_stateless_reset(
        buf.data(), buf.size(), token.data(),
        rand_data.data(), randlen);

    if (nwrite < 0) {
        return -1;
    }

    return send_packet(remote_ep, buf.data(), static_cast<size_t>(nwrite));
}

std::shared_ptr<Http3Connection> Http3Server::find_connection(const ngtcp2_cid* dcid) {
    std::string key = cid_to_string(dcid);

    std::lock_guard lock(mutex_);
    auto it = connections_.find(key);
    return it != connections_.end() ? it->second : nullptr;
}

//==============================================================================
// Global HTTP/3 Server Instance
//==============================================================================

static std::unique_ptr<Http3Server> g_http3_server;

NPRPC_API void init_http3_server(boost::asio::io_context& ioc) {
    if (!g_cfg.http3_enabled || g_cfg.listen_http_port == 0) {
        return;
    }

    NPRPC_HTTP3_TRACE("Initializing on port {} (nghttp3 backend)", g_cfg.listen_http_port);

    if (g_cfg.http3_cert_file.empty() || g_cfg.http3_key_file.empty()) {
        std::cerr << "[HTTP/3] Certificate and key files required" << std::endl;
        return;
    }

    g_http3_server = std::make_unique<Http3Server>(
        ioc,
        g_cfg.http3_cert_file,
        g_cfg.http3_key_file,
        g_cfg.listen_http_port);

    if (!g_http3_server->start()) {
        std::cerr << "[HTTP/3] Failed to start server" << std::endl;
        g_http3_server.reset();
    }
}

NPRPC_API void stop_http3_server() {
    if (g_http3_server) {
        g_http3_server->stop();
        g_http3_server.reset();
    }
}

} // namespace nprpc::impl

#endif // NPRPC_HTTP3_ENABLED && NPRPC_HTTP3_BACKEND_NGHTTP3

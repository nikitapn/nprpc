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

namespace nprpc::impl {

// Forward declaration - implemented in http_server.cpp
extern beast::string_view mime_type(beast::string_view path);
extern std::string path_cat(beast::string_view base, beast::string_view path);

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
    
    // Response data
    std::vector<uint8_t> response_body;
    size_t response_offset = 0;  // How much has been sent
    bool headers_sent = false;
    bool body_complete = false;
    
    // HTTP/3 response status and headers (kept alive for nghttp3)
    std::string status_str;
    std::string content_type_str;
    std::string content_length_str;
};

//==============================================================================
// Http3Connection - A single QUIC connection with HTTP/3
//
// TODO: Implement the full QUIC connection handling with ngtcp2
// This requires:
// - TLS 1.3 handshake with QUIC transport parameters
// - Proper callback setup for all ngtcp2 events
// - Stream management
// - Flow control
// - See ngtcp2/examples/server.cc for reference
//==============================================================================

class Http3Server;

class Http3Connection : public std::enable_shared_from_this<Http3Connection> {
public:
    Http3Connection(Http3Server* server,
                    boost::asio::ip::udp::socket& socket,
                    const boost::asio::ip::udp::endpoint& remote_ep,
                    const ngtcp2_cid* dcid,
                    const ngtcp2_cid* scid,
                    uint32_t version);
    ~Http3Connection();
    
    bool init(SSL_CTX* ssl_ctx);
    void on_read(const uint8_t* data, size_t len);
    int on_write();
    void handle_expiry();
    
    const ngtcp2_cid& scid() const { return scid_; }
    const boost::asio::ip::udp::endpoint& remote_endpoint() const { return remote_ep_; }
    
private:
    Http3Stream* find_stream(int64_t stream_id);
    Http3Stream* create_stream(int64_t stream_id);
    void remove_stream(int64_t stream_id);
    
    Http3Server* server_;
    boost::asio::ip::udp::socket& socket_;
    boost::asio::ip::udp::endpoint remote_ep_;
    
    ngtcp2_conn* conn_ = nullptr;
    nghttp3_conn* httpconn_ = nullptr;
    SSL* ssl_ = nullptr;
    
    ngtcp2_cid scid_;
    ngtcp2_cid dcid_;
    uint32_t version_;
    
    std::unordered_map<int64_t, std::unique_ptr<Http3Stream>> streams_;
    
    // Send buffer
    std::vector<uint8_t> send_buf_;
    static constexpr size_t MAX_UDP_PAYLOAD = 1350;
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
    
private:
    void do_receive();
    void handle_packet(const boost::asio::ip::udp::endpoint& remote_ep,
                      const uint8_t* data, size_t len);
    
    std::shared_ptr<Http3Connection> find_connection(const ngtcp2_cid* dcid);
    void remove_connection(const ngtcp2_cid* scid);
    
    boost::asio::io_context& ioc_;
    std::string cert_file_;
    std::string key_file_;
    uint16_t port_;
    
    boost::asio::ip::udp::socket socket_;
    boost::asio::ip::udp::endpoint remote_ep_;
    std::array<uint8_t, 65536> recv_buf_;
    
    SSL_CTX* ssl_ctx_ = nullptr;
    
    std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<Http3Connection>> connections_;
};

//==============================================================================
// Http3Connection Implementation
//==============================================================================

Http3Connection::Http3Connection(Http3Server* server,
                                 boost::asio::ip::udp::socket& socket,
                                 const boost::asio::ip::udp::endpoint& remote_ep,
                                 const ngtcp2_cid* dcid,
                                 const ngtcp2_cid* scid,
                                 uint32_t version)
    : server_(server)
    , socket_(socket)
    , remote_ep_(remote_ep)
    , version_(version)
{
    dcid_ = *dcid;
    
    // Generate our own source CID
    scid_.datalen = NGTCP2_MIN_INITIAL_DCIDLEN;
    random_bytes(scid_.data, scid_.datalen);
    
    send_buf_.resize(MAX_UDP_PAYLOAD);
}

Http3Connection::~Http3Connection() {
    if (httpconn_) {
        nghttp3_conn_del(httpconn_);
    }
    if (conn_) {
        ngtcp2_conn_del(conn_);
    }
    if (ssl_) {
        SSL_free(ssl_);
    }
}

bool Http3Connection::init(SSL_CTX* ssl_ctx) {
    // TODO: Full implementation needed
    // See ngtcp2/examples/server.cc for reference implementation
    // 
    // Key steps:
    // 1. Create SSL object and configure for QUIC
    // 2. Set up ngtcp2_callbacks with all required callbacks
    // 3. Configure ngtcp2_settings and ngtcp2_transport_params
    // 4. Create ngtcp2_conn_server_new
    // 5. Set up TLS native handle
    // 6. Handle incoming Initial packet
    
    std::cerr << "[HTTP/3 nghttp3] Connection init not fully implemented yet" << std::endl;
    return false;
}

void Http3Connection::on_read(const uint8_t* data, size_t len) {
    // TODO: Feed data to ngtcp2_conn_read_pkt
}

int Http3Connection::on_write() {
    // TODO: Write packets using ngtcp2_conn_writev_stream and ngtcp2_conn_write_pkt
    return 0;
}

void Http3Connection::handle_expiry() {
    // TODO: Handle connection timeout
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
    // Create SSL context
    ssl_ctx_ = SSL_CTX_new(TLS_server_method());
    if (!ssl_ctx_) {
        std::cerr << "[HTTP/3] Failed to create SSL context" << std::endl;
        return false;
    }
    
    // Set minimum TLS version to 1.3 (required for QUIC)
    SSL_CTX_set_min_proto_version(ssl_ctx_, TLS1_3_VERSION);
    SSL_CTX_set_max_proto_version(ssl_ctx_, TLS1_3_VERSION);
    
    // Configure for QUIC
    // Note: This requires OpenSSL with QUIC support or a patched version
    // For production, consider using quictls (OpenSSL fork with QUIC support)
    
    // Set ALPN to h3
    static const unsigned char alpn[] = "\x02h3";
    SSL_CTX_set_alpn_select_cb(ssl_ctx_, [](SSL* ssl, const unsigned char** out,
                                            unsigned char* outlen,
                                            const unsigned char* in, unsigned int inlen,
                                            void* arg) -> int {
        if (SSL_select_next_proto(const_cast<unsigned char**>(out), outlen,
                                  alpn, sizeof(alpn) - 1, in, inlen) != OPENSSL_NPN_NEGOTIATED) {
            return SSL_TLSEXT_ERR_NOACK;
        }
        return SSL_TLSEXT_ERR_OK;
    }, nullptr);
    
    // Load certificate and key
    if (SSL_CTX_use_certificate_chain_file(ssl_ctx_, cert_file_.c_str()) != 1) {
        std::cerr << "[HTTP/3] Failed to load certificate" << std::endl;
        SSL_CTX_free(ssl_ctx_);
        ssl_ctx_ = nullptr;
        return false;
    }
    
    if (SSL_CTX_use_PrivateKey_file(ssl_ctx_, key_file_.c_str(), SSL_FILETYPE_PEM) != 1) {
        std::cerr << "[HTTP/3] Failed to load private key" << std::endl;
        SSL_CTX_free(ssl_ctx_);
        ssl_ctx_ = nullptr;
        return false;
    }
    
    // Open UDP socket
    boost::system::error_code ec;
    socket_.open(boost::asio::ip::udp::v6(), ec);
    if (ec) {
        std::cerr << "[HTTP/3] Failed to open socket: " << ec.message() << std::endl;
        return false;
    }
    
    // Allow IPv4 connections too (dual-stack)
    socket_.set_option(boost::asio::ip::v6_only(false), ec);
    socket_.set_option(boost::asio::socket_base::reuse_address(true), ec);
    
    socket_.bind(boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v6(), port_), ec);
    if (ec) {
        std::cerr << "[HTTP/3] Failed to bind socket: " << ec.message() << std::endl;
        return false;
    }
    
    std::cout << "[HTTP/3] Server listening on port " << port_ 
              << " (nghttp3 backend - STUB)" << std::endl;
    std::cout << "[HTTP/3] WARNING: nghttp3 backend is not fully implemented yet" << std::endl;
    
    // Start receiving
    do_receive();
    
    return true;
}

void Http3Server::stop() {
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

void Http3Server::do_receive() {
    socket_.async_receive_from(
        boost::asio::buffer(recv_buf_),
        remote_ep_,
        [this](boost::system::error_code ec, std::size_t bytes_recvd) {
            if (!ec && bytes_recvd > 0) {
                handle_packet(remote_ep_, recv_buf_.data(), bytes_recvd);
            }
            
            if (!ec || ec == boost::asio::error::message_size) {
                do_receive();
            }
        });
}

void Http3Server::handle_packet(const boost::asio::ip::udp::endpoint& remote_ep,
                                const uint8_t* data, size_t len) {
    // Parse the packet header to get DCID
    ngtcp2_version_cid vc;
    int rv = ngtcp2_pkt_decode_version_cid(&vc, data, len, NGTCP2_MIN_INITIAL_DCIDLEN);
    if (rv != 0) {
        return;  // Invalid packet
    }
    
    ngtcp2_cid dcid;
    dcid.datalen = vc.dcidlen;
    memcpy(dcid.data, vc.dcid, vc.dcidlen);
    
    // Find existing connection
    auto conn = find_connection(&dcid);
    
    if (!conn) {
        // New connection - check packet type via header
        ngtcp2_pkt_hd hd;
        rv = ngtcp2_pkt_decode_hd_long(&hd, data, len);
        if (rv < 0 || hd.type != NGTCP2_PKT_INITIAL) {
            return;  // Not an Initial packet
        }
        
        ngtcp2_cid scid;
        scid.datalen = vc.scidlen;
        memcpy(scid.data, vc.scid, vc.scidlen);
        
        // Create new connection
        conn = std::make_shared<Http3Connection>(this, socket_, remote_ep,
                                                  &dcid, &scid, vc.version);
        if (!conn->init(ssl_ctx_)) {
            // Connection init failed - this is expected since it's a stub
            return;
        }
        
        // Store connection
        std::string key(reinterpret_cast<const char*>(conn->scid().data), conn->scid().datalen);
        {
            std::lock_guard lock(mutex_);
            connections_[key] = conn;
        }
        
        if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryCall) {
            std::cout << "[HTTP/3] New connection from " << remote_ep << std::endl;
        }
    }
    
    // Feed data to connection
    conn->on_read(data, len);
}

std::shared_ptr<Http3Connection> Http3Server::find_connection(const ngtcp2_cid* dcid) {
    std::string key(reinterpret_cast<const char*>(dcid->data), dcid->datalen);
    
    std::lock_guard lock(mutex_);
    auto it = connections_.find(key);
    return it != connections_.end() ? it->second : nullptr;
}

void Http3Server::remove_connection(const ngtcp2_cid* scid) {
    std::string key(reinterpret_cast<const char*>(scid->data), scid->datalen);
    
    std::lock_guard lock(mutex_);
    connections_.erase(key);
}

//==============================================================================
// Global HTTP/3 Server Instance
//==============================================================================

static std::unique_ptr<Http3Server> g_http3_server;

NPRPC_API void init_http3_server(boost::asio::io_context& ioc) {
    if (!g_cfg.http3_enabled || g_cfg.listen_http_port == 0) {
        return;
    }
    
    std::cout << "[HTTP/3] Initializing on port " << g_cfg.listen_http_port 
              << " (nghttp3 backend)" << std::endl;
    
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

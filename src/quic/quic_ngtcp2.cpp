// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT
//
// Raw QUIC transport using ngtcp2 + boringssl/openssl.
// Replaces the MsQuic-based implementation.
// Protocol: raw binary NPRPC frames over a single persistent bidi QUIC stream.
// Wire format identical to the TCP/WebSocket transport (4-byte LE total length).

#include <nprpc/impl/quic_transport.hpp>

#ifdef NPRPC_QUIC_ENABLED

#include <nprpc/asio_compat.hpp>
#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/impl/session.hpp>
#include <nprpc/impl/stream_manager.hpp>
#include <nprpc/impl/telemetry.hpp>
#include "../logging.hpp"

#include <openssl/ssl.h>
#include <ngtcp2/ngtcp2.h>
#include <ngtcp2/ngtcp2_crypto.h>
#if defined(OPENSSL_IS_BORINGSSL)
#  include <ngtcp2/ngtcp2_crypto_boringssl.h>
#else
#  include <ngtcp2/ngtcp2_crypto_ossl.h>
#endif

#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/x509v3.h>
#include <openssl/ssl.h>

#include <boost/asio.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/steady_timer.hpp>

#if !defined(_WIN32)
#  include <netinet/in.h>
#  include <netinet/udp.h>  // UDP_SEGMENT (Linux >= 4.18)
#  include <sys/socket.h>
#  include <sys/uio.h>      // iovec, sendmsg
#endif

#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------------------
// Tracing
// ---------------------------------------------------------------------------
#define NPRPC_ENABLE_QUIC_TRANSPORT_TRACE 0

#if NPRPC_ENABLE_QUIC_TRANSPORT_TRACE
#  define NPRPC_QUIC_TRACE(fmt, ...) \
     NPRPC_LOG_TRACE("[QUIC] " fmt __VA_OPT__(,) __VA_ARGS__)
#else
#  define NPRPC_QUIC_TRACE(fmt, ...) do {} while (0)
#endif

#define NPRPC_QUIC_ERROR(fmt, ...) \
  NPRPC_LOG_ERROR("[QUIC] " fmt __VA_OPT__(,) __VA_ARGS__)

namespace nprpc::impl {

static std::atomic<std::shared_ptr<const QuicClientTlsConfig>> client_tls_config{
    std::make_shared<const QuicClientTlsConfig>()};

static bool configure_client_credentials(SSL_CTX* ctx, const QuicClientTlsConfig& config)
{
  if (!ctx || config.certificate_file.empty() != config.private_key_file.empty()) return false;
  SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);
  if (config.ca_file.empty()) {
    if (SSL_CTX_set_default_verify_paths(ctx) != 1) return false;
  } else if (SSL_CTX_load_verify_locations(ctx, config.ca_file.c_str(), nullptr) != 1) {
    return false;
  }
  return config.certificate_file.empty() ||
      (SSL_CTX_use_certificate_chain_file(ctx, config.certificate_file.c_str()) == 1 &&
       SSL_CTX_use_PrivateKey_file(ctx, config.private_key_file.c_str(), SSL_FILETYPE_PEM) == 1 &&
       SSL_CTX_check_private_key(ctx) == 1);
}

void set_quic_client_tls(const QuicClientTlsConfig& config)
{
  std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> checked(
      SSL_CTX_new(TLS_client_method()), SSL_CTX_free);
  if (!configure_client_credentials(checked.get(), config))
    throw std::invalid_argument("Invalid QUIC client TLS credentials or trust bundle");
  client_tls_config.store(std::make_shared<const QuicClientTlsConfig>(config),
                          std::memory_order_release);
}

// ---------------------------------------------------------------------------
// Anonymous utilities
// ---------------------------------------------------------------------------
namespace {

// ALPN wire encoding: 1-byte length + "nprpc"
static constexpr uint8_t kNprpcAlpn[]  = "\x05nprpc";
static constexpr size_t  kNprpcAlpnLen = sizeof(kNprpcAlpn) - 1; // no null

// Connection ID length for server-generated CIDs
static constexpr size_t kScidLen = 18;

// Maximum single UDP payload (conservative to avoid fragmentation)
static constexpr size_t kMaxUdpPayload = 1350;

// Maximum packets to batch per do_write() call
static constexpr size_t kMaxBurst = 64;
static constexpr size_t kMaxWriteVecs = 32;

// Hard limit of the buffer passed to the kernel for UDP GSO:
// UDP total length is a 16-bit field, so the payload is at most 65535 bytes.
static constexpr size_t kGsoMaxBytes = 65535;

// Sanity limit on NPRPC frame size
static constexpr uint32_t kMaxNprpcFrame = 64u * 1024 * 1024; // 64 MB

// ---------------------------------------------------------------------------
// Monotonic timestamp (nanoseconds), same workaround as HTTP/3
// ---------------------------------------------------------------------------
static uint64_t timestamp_ns()
{
  auto ns = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());

  static std::atomic<uint64_t> last{0};
  auto prev = last.load(std::memory_order_relaxed);
  for (;;) {
    auto clamped = std::max(ns, prev);
    if (last.compare_exchange_weak(prev, clamped,
                                   std::memory_order_relaxed,
                                   std::memory_order_relaxed))
      return clamped;
  }
}

static void random_bytes(uint8_t* dst, size_t len)
{
  RAND_bytes(dst, static_cast<int>(len));
}

static void generate_cid(ngtcp2_cid* cid, size_t len)
{
  cid->datalen = len;
  random_bytes(cid->data, len);
}

static std::string cid_key(const ngtcp2_cid* cid)
{
  return {reinterpret_cast<const char*>(cid->data), cid->datalen};
}

#if !defined(OPENSSL_IS_BORINGSSL)
static const char* crypto_ciphers()
{
  return "TLS_AES_128_GCM_SHA256:TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256";
}
#endif

static const char* crypto_groups() { return "X25519:P-256:P-384:P-521"; }

// ---------------------------------------------------------------------------
// Inject / extract request_id from NPRPC flat buffer header
// ---------------------------------------------------------------------------
static void inject_request_id(flat_buffer& buf, uint32_t id)
{
  if (buf.size() >= sizeof(impl::Header)) {
    impl::flat::Header_Direct hdr(buf, 0);
    hdr.request_id() = id;
  }
}

static uint32_t extract_request_id(const flat_buffer& buf)
{
  if (buf.size() >= sizeof(impl::Header)) {
    const impl::flat::Header_Direct hdr(const_cast<flat_buffer&>(buf), 0);
    return hdr.request_id();
  }
  return 0;
}

// ---------------------------------------------------------------------------
// Convert Asio endpoint → sockaddr_storage
// ---------------------------------------------------------------------------
static ngtcp2_path make_path(const boost::asio::ip::udp::endpoint& local,
                              const boost::asio::ip::udp::endpoint& remote,
                              sockaddr_storage& lss, sockaddr_storage& rss)
{
  std::memset(&lss, 0, sizeof(lss));
  std::memset(&rss, 0, sizeof(rss));
  std::memcpy(&lss, local.data(),  local.size());
  std::memcpy(&rss, remote.data(), remote.size());
  return ngtcp2_path{
      .local  = {.addr = reinterpret_cast<sockaddr*>(&lss),
                 .addrlen = static_cast<ngtcp2_socklen>(local.size())},
      .remote = {.addr = reinterpret_cast<sockaddr*>(&rss),
                 .addrlen = static_cast<ngtcp2_socklen>(remote.size())},
  };
}

// ---------------------------------------------------------------------------
// GSO send: pack all QUIC packets in one contiguous buffer, then hand them
// to the kernel in a single sendmsg() with UDP_SEGMENT so the NIC/driver
// does the actual segmentation (true GSO, 1 syscall per do_write()).
// Falls back to a plain sendmsg on kernels without UDP_SEGMENT.
// ---------------------------------------------------------------------------
static void send_gso(int fd,
                     const boost::asio::ip::udp::endpoint& remote,
                     const uint8_t* data, size_t len, size_t gso_size)
{
  iovec iov{ const_cast<uint8_t*>(data), len };

  uint8_t ctrl[CMSG_SPACE(sizeof(uint16_t))]{};
  msghdr msg{};
  msg.msg_name    = const_cast<sockaddr*>(remote.data());
  msg.msg_namelen = static_cast<socklen_t>(remote.size());
  msg.msg_iov     = &iov;
  msg.msg_iovlen  = 1;

#if defined(UDP_SEGMENT)
  if (len > gso_size) {
    msg.msg_control    = ctrl;
    msg.msg_controllen = sizeof(ctrl);
    auto* cm           = CMSG_FIRSTHDR(&msg);
    cm->cmsg_level     = SOL_UDP;
    cm->cmsg_type      = UDP_SEGMENT;
    cm->cmsg_len       = CMSG_LEN(sizeof(uint16_t));
    uint16_t seg       = static_cast<uint16_t>(gso_size);
    std::memcpy(CMSG_DATA(cm), &seg, sizeof(seg));
    msg.msg_controllen = CMSG_SPACE(sizeof(uint16_t));
  }
#endif

  ssize_t nw;
  do { nw = ::sendmsg(fd, &msg, 0); } while (nw < 0 && errno == EINTR);
  if (nw < 0)
    NPRPC_QUIC_ERROR("send_gso failed errno={}", errno);
}

static size_t build_unsent_vecs(const std::deque<flat_buffer>& queue,
                                uint64_t acked_offset,
                                uint64_t sent_offset,
                                std::array<ngtcp2_vec, kMaxWriteVecs>& vecs)
{
  size_t vec_count = 0;
  uint64_t queue_offset = acked_offset;
  uint64_t target_offset = sent_offset;

  for (const auto& chunk : queue) {
    const auto chunk_size = static_cast<uint64_t>(chunk.size());
    if (target_offset >= queue_offset + chunk_size) {
      queue_offset += chunk_size;
      continue;
    }

    const auto start = static_cast<size_t>(target_offset > queue_offset
        ? target_offset - queue_offset
        : 0);
    if (start < chunk.size()) {
      vecs[vec_count++] = ngtcp2_vec{
          .base = const_cast<uint8_t*>(chunk.data_ptr() + start),
          .len = chunk.size() - start,
      };
      if (vec_count == vecs.size()) break;
    }

    queue_offset += chunk_size;
    target_offset = queue_offset;
  }

  return vec_count;
}

static void release_acked_prefix(std::deque<flat_buffer>& queue,
                                 uint64_t& acked_offset,
                                 uint64_t offset,
                                 uint64_t datalen)
{
  const auto ack_end = offset + datalen;
  if (ack_end <= acked_offset) return;

  uint64_t remaining = ack_end - acked_offset;
  while (remaining > 0 && !queue.empty()) {
    auto& front = queue.front();
    const auto consumed = std::min<uint64_t>(remaining, front.size());
    front.consume(static_cast<size_t>(consumed));
    acked_offset += consumed;
    remaining -= consumed;
    if (front.size() == 0) queue.pop_front();
  }

  if (remaining > 0) {
    acked_offset += remaining;
  }
}

static uint64_t queued_bytes(const std::deque<flat_buffer>& queue)
{
  uint64_t total = 0;
  for (const auto& chunk : queue) {
    total += static_cast<uint64_t>(chunk.size());
  }
  return total;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
class QuicNgtcp2Listener;
class QuicNgtcp2ServerSession;

// ===========================================================================
// QuicNgtcp2ServerConn — one per accepted QUIC connection (server side)
// ===========================================================================
class QuicNgtcp2ServerConn
    : public std::enable_shared_from_this<QuicNgtcp2ServerConn>
{
public:
  QuicNgtcp2ServerConn(QuicNgtcp2Listener*                       listener,
                       boost::asio::ip::udp::socket&             socket,
                       const boost::asio::ip::udp::endpoint&     local_ep,
                       const boost::asio::ip::udp::endpoint&     remote_ep,
                       const ngtcp2_cid*                         client_scid,
                       const ngtcp2_cid*                         client_dcid,
                       uint32_t                                  version,
                       std::shared_ptr<SSL_CTX>                   ssl_ctx);
  ~QuicNgtcp2ServerConn();

  bool init();
  void close();

  // Thread-safe: post a received UDP packet to the strand
  void enqueue_packet(flat_buffer pkt, ngtcp2_pkt_info pi);

  // Thread-safe: post a write to the RPC stream
  void enqueue_response(flat_buffer buf);

  const ngtcp2_cid& scid()       const noexcept { return scid_; }
  bool              is_closed()   const noexcept { return closed_; }
  auto              get_executor()               { return strand_; }
  ngtcp2_crypto_conn_ref* conn_ref() noexcept   { return &conn_ref_; }

private:
  // All run on strand_
  void do_read(const uint8_t* data, size_t len, const ngtcp2_pkt_info* pi);
  void do_write();
  void schedule_timer();
  void handle_expiry();
  void handle_error();
  void on_stream_opened(int64_t stream_id);
  void on_stream_data(int64_t stream_id, const uint8_t* data, size_t len);
  void on_stream_data_acked(int64_t stream_id, uint64_t offset, uint64_t datalen);
  void maybe_create_session();
  void remove_self();

  // ngtcp2 static callbacks
  static int  cb_recv_client_initial(ngtcp2_conn*, const ngtcp2_cid*, void*);
  static int  cb_recv_crypto_data(ngtcp2_conn*, ngtcp2_encryption_level,
                                   uint64_t, const uint8_t*, size_t, void*);
  static int  cb_handshake_completed(ngtcp2_conn*, void*);
  static int  cb_recv_stream_data(ngtcp2_conn*, uint32_t, int64_t, uint64_t,
                                   const uint8_t*, size_t, void*, void*);
  static int  cb_acked_stream_data_offset(ngtcp2_conn*, int64_t, uint64_t,
                                           uint64_t, void*, void*);
  static int  cb_stream_open(ngtcp2_conn*, int64_t, void*);
  static int  cb_stream_close(ngtcp2_conn*, uint32_t, int64_t, uint64_t, void*, void*);
  static int  cb_get_new_connection_id(ngtcp2_conn*, ngtcp2_cid*, uint8_t*, size_t, void*);
  static int  cb_remove_connection_id(ngtcp2_conn*, const ngtcp2_cid*, void*);
  static void cb_rand(uint8_t*, size_t, const ngtcp2_rand_ctx*);

  std::shared_ptr<QuicNgtcp2Listener>                    listener_;
  boost::asio::ip::udp::socket&                         socket_;
  boost::asio::ip::udp::endpoint                        local_ep_;
  boost::asio::ip::udp::endpoint                        remote_ep_;
  boost::asio::strand<boost::asio::io_context::executor_type> strand_;
  boost::asio::steady_timer                             timer_;

  ngtcp2_conn*            conn_      = nullptr;
  ngtcp2_crypto_conn_ref  conn_ref_{};
#if !defined(OPENSSL_IS_BORINGSSL)
  ngtcp2_crypto_ossl_ctx* ossl_ctx_  = nullptr;
#endif
  SSL*     ssl_     = nullptr;
  std::shared_ptr<SSL_CTX> ssl_ctx_;

  ngtcp2_cid scid_;             // our source CID
  ngtcp2_cid dcid_;             // client's source CID (our peer CID)
  ngtcp2_cid original_client_dcid_;
  uint32_t   version_;

  int64_t    rpc_stream_id_ = -1;
  flat_buffer rx_buf_{flat_buffer::default_initial_size()};
  std::deque<flat_buffer> write_queue_;
  uint64_t   tx_stream_acked_offset_ = 0;
  uint64_t   tx_stream_sent_offset_  = 0;

  bool closed_ = false;
  bool handshake_finished_ = false;
  std::unique_ptr<ActiveConnection> active_connection_;

  // Contiguous send buffer: kMaxBurst slots of kMaxUdpPayload each.
  // ngtcp2 writes one QUIC packet per slot; we flush the whole batch with
  // a single sendmmsg() call to amortize syscall overhead.
  std::array<uint8_t, kGsoMaxBytes> txbuf_;
  std::vector<uint8_t>            conn_close_buf_;
  ngtcp2_ccerr                    last_error_{};

  std::shared_ptr<QuicNgtcp2ServerSession> session_;
};

// ===========================================================================
// QuicNgtcp2ServerSession — Session for a QUIC server connection
// ===========================================================================
class QuicNgtcp2ServerSession
    : public Session,
      public std::enable_shared_from_this<QuicNgtcp2ServerSession>
{
public:
  explicit QuicNgtcp2ServerSession(std::shared_ptr<QuicNgtcp2ServerConn> conn)
      : Session(conn->get_executor()), conn_(conn)
  {
    ctx_.stream_manager->set_active_stream_counter(active_stream_counter(monitor::Transport::Quic));
  }

  void send_receive(flat_buffer&, uint32_t) override
  {
    assert(false && "server session cannot initiate RPC");
  }
  nprpc::Task<> send_receive_coro(flat_buffer&, uint32_t, std::stop_token) override
  {
    assert(false && "server session cannot initiate RPC");
    co_return;
  }
  void send_receive_async(
      flat_buffer&&,
      std::optional<std::function<void(const boost::system::error_code&, flat_buffer&)>>&&,
      uint32_t) override
  {
    assert(false && "server session cannot initiate RPC");
  }

  bool send_stream_message(flat_buffer&& buf) override
  {
    auto conn = conn_.lock();
    if (!conn) return false;
    conn->enqueue_response(std::move(buf));
    return true;
  }
  bool send_main_stream_message(flat_buffer&& buf) override
  {
    auto conn = conn_.lock();
    if (!conn) return false;
    conn->enqueue_response(std::move(buf));
    return true;
  }

  // Called from QuicNgtcp2ServerConn on its strand
  void dispatch_request(flat_buffer& frame)
  {
    const uint32_t rid = extract_request_id(frame);
    flat_buffer tx;
    if (measured_dispatch(monitor::Transport::Quic, frame, tx,
                         [&] { return handle_request(frame, tx); }) && tx.size() > 0) {
      inject_request_id(tx, rid);
      if (auto conn = conn_.lock()) conn->enqueue_response(std::move(tx));
    }
  }

  void timeout_action() override {}

private:
  std::weak_ptr<QuicNgtcp2ServerConn> conn_;
};

// ===========================================================================
// QuicNgtcp2Listener — single UDP socket; routes packets to connections
// ===========================================================================
class QuicNgtcp2Listener : public std::enable_shared_from_this<QuicNgtcp2Listener>
{
public:
  QuicNgtcp2Listener(boost::asio::io_context& ioc,
                     std::string cert_file,
                     std::string key_file,
                     uint16_t    port);
  ~QuicNgtcp2Listener();

  bool start();
  void stop();
  bool reload_certificates() { return setup_ssl_ctx(); }

  void associate_cid(const ngtcp2_cid* cid,
                     std::shared_ptr<QuicNgtcp2ServerConn> conn);
  void dissociate_cid(const ngtcp2_cid* cid);
  void remove_connection(std::shared_ptr<QuicNgtcp2ServerConn> conn);

  void send_to(const boost::asio::ip::udp::endpoint& ep,
               const uint8_t* data, size_t len)
  {
    boost::system::error_code ec;
    socket_.send_to(boost::asio::buffer(data, len), ep, 0, ec);
    if (ec) NPRPC_QUIC_ERROR("send_to: {}", ec.message());
  }

  boost::asio::io_context&          io_context()  { return ioc_; }
  boost::asio::ip::udp::socket&     socket()       { return socket_; }
  const boost::asio::ip::udp::endpoint& local_ep() const { return local_ep_; }

private:
  void do_receive();
  void handle_packet(const boost::asio::ip::udp::endpoint& from,
                     const uint8_t* data, size_t len);
  int  handle_new_connection(const boost::asio::ip::udp::endpoint& from,
                              const uint8_t* data, size_t len,
                              const ngtcp2_version_cid& vc);
  bool setup_ssl_ctx();
  std::shared_ptr<QuicNgtcp2ServerConn> find_connection(const ngtcp2_cid* dcid);

  boost::asio::io_context& ioc_;
  std::string              cert_file_;
  std::string              key_file_;
  uint16_t                 port_;

  boost::asio::ip::udp::socket   socket_;
  boost::asio::ip::udp::endpoint local_ep_;
  boost::asio::ip::udp::endpoint sender_ep_; // scratch for async_receive_from
  std::array<uint8_t, 65536>     recv_buf_;

  std::atomic<std::shared_ptr<SSL_CTX>> ssl_ctx_;

  std::mutex conns_mutex_;
  std::unordered_map<std::string, std::shared_ptr<QuicNgtcp2ServerConn>> conns_;
  std::atomic<bool> running_{false};
};

// ===========================================================================
// QuicNgtcp2ServerConn — implementation
// ===========================================================================

QuicNgtcp2ServerConn::QuicNgtcp2ServerConn(
    QuicNgtcp2Listener*                   listener,
    boost::asio::ip::udp::socket&         socket,
    const boost::asio::ip::udp::endpoint& local_ep,
    const boost::asio::ip::udp::endpoint& remote_ep,
    const ngtcp2_cid*                     client_scid,
    const ngtcp2_cid*                     client_dcid,
    uint32_t                              version,
    std::shared_ptr<SSL_CTX>               ssl_ctx)
    : listener_(listener->shared_from_this())
    , socket_(socket)
    , local_ep_(local_ep)
    , remote_ep_(remote_ep)
    , strand_(boost::asio::make_strand(listener->io_context()))
    , timer_(listener->io_context())
    , ssl_ctx_(std::move(ssl_ctx))
    , version_(version)
{
  dcid_               = *client_scid;
  original_client_dcid_ = *client_dcid;
  generate_cid(&scid_, kScidLen);

  conn_ref_.get_conn = [](ngtcp2_crypto_conn_ref* r) -> ngtcp2_conn* {
    return static_cast<QuicNgtcp2ServerConn*>(r->user_data)->conn_;
  };
  conn_ref_.user_data = this;
}

QuicNgtcp2ServerConn::~QuicNgtcp2ServerConn()
{
  timer_.cancel();
  if (conn_) ngtcp2_conn_del(conn_);
#if defined(OPENSSL_IS_BORINGSSL)
  if (ssl_) { SSL_set_app_data(ssl_, nullptr); SSL_free(ssl_); }
#else
  if (ossl_ctx_) {
    if (auto* s = ngtcp2_crypto_ossl_ctx_get_ssl(ossl_ctx_)) {
      SSL_set_app_data(s, nullptr);
      SSL_free(s);
    }
    ngtcp2_crypto_ossl_ctx_del(ossl_ctx_);
  }
#endif
}

bool QuicNgtcp2ServerConn::init()
{
#if !defined(OPENSSL_IS_BORINGSSL)
  if (ngtcp2_crypto_ossl_ctx_new(&ossl_ctx_, nullptr) != 0) {
    NPRPC_QUIC_ERROR("ngtcp2_crypto_ossl_ctx_new failed");
    return false;
  }
#endif

  ngtcp2_callbacks cbs{
      .recv_client_initial   = cb_recv_client_initial,
      .recv_crypto_data      = cb_recv_crypto_data,
      .handshake_completed   = cb_handshake_completed,
      .encrypt               = ngtcp2_crypto_encrypt_cb,
      .decrypt               = ngtcp2_crypto_decrypt_cb,
      .hp_mask               = ngtcp2_crypto_hp_mask_cb,
      .recv_stream_data      = cb_recv_stream_data,
      .acked_stream_data_offset = cb_acked_stream_data_offset,
      .stream_open           = cb_stream_open,
      .stream_close          = cb_stream_close,
      .rand                  = cb_rand,
      .get_new_connection_id = cb_get_new_connection_id,
      .remove_connection_id  = cb_remove_connection_id,
      .update_key            = ngtcp2_crypto_update_key_cb,
      .delete_crypto_aead_ctx = ngtcp2_crypto_delete_crypto_aead_ctx_cb,
      .delete_crypto_cipher_ctx = ngtcp2_crypto_delete_crypto_cipher_ctx_cb,
      .get_path_challenge_data  = ngtcp2_crypto_get_path_challenge_data_cb,
      .version_negotiation      = ngtcp2_crypto_version_negotiation_cb,
  };

  ngtcp2_settings settings;
  ngtcp2_settings_default(&settings);
  settings.initial_ts           = timestamp_ns();
  settings.log_printf           = nullptr;
  settings.cc_algo              = NGTCP2_CC_ALGO_CUBIC;
  settings.max_tx_udp_payload_size = kMaxUdpPayload;
  settings.initial_rtt          = 100 * NGTCP2_MICROSECONDS;
  settings.ack_thresh           = 1;

  ngtcp2_transport_params params;
  ngtcp2_transport_params_default(&params);
  params.initial_max_stream_data_bidi_local  = 16 * 1024 * 1024;
  params.initial_max_stream_data_bidi_remote = 16 * 1024 * 1024;
  params.initial_max_stream_data_uni         = 0;
  params.initial_max_data                    = 32 * 1024 * 1024;
  params.max_ack_delay                       = 1 * NGTCP2_MILLISECONDS;
  params.initial_max_streams_bidi            = 8;
  params.initial_max_streams_uni             = 0;
  params.max_idle_timeout                    = 30 * NGTCP2_SECONDS;
  params.active_connection_id_limit          = 7;
  params.original_dcid                       = original_client_dcid_;
  params.original_dcid_present               = 1;

  sockaddr_storage lss, rss;
  auto path = make_path(local_ep_, remote_ep_, lss, rss);

  int rv = ngtcp2_conn_server_new(&conn_, &dcid_, &scid_, &path, version_,
                                   &cbs, &settings, &params, nullptr, this);
  if (rv != 0) {
    NPRPC_QUIC_ERROR("ngtcp2_conn_server_new: {}", ngtcp2_strerror(rv));
    return false;
  }

  ssl_ = SSL_new(ssl_ctx_.get());
  if (!ssl_) { NPRPC_QUIC_ERROR("SSL_new failed"); return false; }

#if defined(OPENSSL_IS_BORINGSSL)
  SSL_set_app_data(ssl_, &conn_ref_);
  SSL_set_accept_state(ssl_);
  ngtcp2_conn_set_tls_native_handle(conn_, ssl_);
#else
  ngtcp2_crypto_ossl_ctx_set_ssl(ossl_ctx_, ssl_);
  if (ngtcp2_crypto_ossl_configure_server_session(ssl_) != 0) {
    NPRPC_QUIC_ERROR("ngtcp2_crypto_ossl_configure_server_session failed");
    return false;
  }
  SSL_set_app_data(ssl_, &conn_ref_);
  SSL_set_accept_state(ssl_);
  ngtcp2_conn_set_tls_native_handle(conn_, ossl_ctx_);
#endif

  return true;
}

void QuicNgtcp2ServerConn::enqueue_packet(flat_buffer pkt, ngtcp2_pkt_info pi)
{
  boost::asio::post(
      strand_,
      [self = shared_from_this(), p = std::move(pkt), pi]() mutable {
        if (self->closed_) return;
        self->do_read(p.data_ptr(), p.size(), &pi);
        self->do_write();
      });
}

void QuicNgtcp2ServerConn::enqueue_response(flat_buffer buf)
{
  boost::asio::post(
      strand_,
      [self = shared_from_this(), b = std::move(buf)]() mutable {
        if (self->closed_) return;
        self->write_queue_.emplace_back(std::move(b));
        self->do_write();
      });
}

void QuicNgtcp2ServerConn::do_read(const uint8_t* data, size_t len,
                                    const ngtcp2_pkt_info* pi)
{
  if (closed_) return;
  if (ngtcp2_conn_in_closing_period(conn_) ||
      ngtcp2_conn_in_draining_period(conn_))
    return;

  sockaddr_storage lss, rss;
  auto path = make_path(local_ep_, remote_ep_, lss, rss);

  int rv = ngtcp2_conn_read_pkt(conn_, &path, pi, data, len, timestamp_ns());
  if (rv != 0) {
    switch (rv) {
    case NGTCP2_ERR_DRAINING:
    case NGTCP2_ERR_DROP_CONN:
      closed_ = true;
      remove_self();
      return;
    default:
      NPRPC_QUIC_ERROR("ngtcp2_conn_read_pkt: {}", ngtcp2_strerror(rv));
      if (!last_error_.error_code)
        ngtcp2_ccerr_set_liberr(&last_error_, rv, nullptr, 0);
      handle_error();
      return;
    }
  }
  schedule_timer();
}

void QuicNgtcp2ServerConn::do_write()
{
  if (closed_) return;
  if (ngtcp2_conn_in_closing_period(conn_) ||
      ngtcp2_conn_in_draining_period(conn_))
    return;

  // ngtcp2_write_pkt callback: ngtcp2 calls this once per packet slot while
  // building the contiguous GSO buffer.  user_data = this.
  // WRITE_STREAM_FLAG_PADDING pads each packet to max_tx_udp_payload_size so
  // all segments in the GSO batch are the same size (required by the kernel).
  auto write_pkt_cb = [](ngtcp2_conn* conn, ngtcp2_path* path,
                          ngtcp2_pkt_info* pi, uint8_t* dest, size_t destlen,
                          ngtcp2_tstamp ts, void* user_data) -> ngtcp2_ssize {
    auto* self = static_cast<QuicNgtcp2ServerConn*>(user_data);
    const bool has_stream  = self->rpc_stream_id_ >= 0;
    const bool has_data    = !self->write_queue_.empty();
    const uint64_t fc_left = ngtcp2_conn_get_max_data_left(conn);
    // std::cerr << "[cb] has_stream=" << has_stream
    //           << " has_data=" << has_data
    //           << " fc_left=" << fc_left
    //           << " destlen=" << destlen << "\n";
    if (has_stream && has_data && fc_left > 0) {
      std::array<ngtcp2_vec, kMaxWriteVecs> vecs{};
      const auto vec_count = build_unsent_vecs(
          self->write_queue_,
          self->tx_stream_acked_offset_,
          self->tx_stream_sent_offset_,
          vecs);
      if (vec_count == 0) {
        return ngtcp2_conn_write_pkt(conn, path, pi, dest, destlen, ts);
      }
      ngtcp2_ssize ndatalen = 0;
      auto nwrite = ngtcp2_conn_writev_stream(
          conn, path, pi, dest, destlen, &ndatalen,
          NGTCP2_WRITE_STREAM_FLAG_PADDING,
          self->rpc_stream_id_, vecs.data(), vec_count, ts);
      // std::cerr << "[cb] writev_stream nwrite=" << nwrite << " ndatalen=" << ndatalen << "\n";
      if (nwrite == NGTCP2_ERR_STREAM_DATA_BLOCKED ||
          nwrite == NGTCP2_ERR_STREAM_SHUT_WR ||
          nwrite == NGTCP2_ERR_STREAM_NOT_FOUND) {
        // std::cerr << "[cb] stream blocked/shut/not_found, falling back\n";
        return ngtcp2_conn_write_pkt(conn, path, pi, dest, destlen, ts);
      }
      if (nwrite > 0 && ndatalen > 0) {
        self->tx_stream_sent_offset_ += static_cast<uint64_t>(ndatalen);
      }
      return nwrite;
    }
    return ngtcp2_conn_write_pkt(conn, path, pi, dest, destlen, ts);
  };

  ngtcp2_path_storage ps;
  ngtcp2_path_storage_zero(&ps);
  ngtcp2_pkt_info pi{};
  size_t gso_size = 0;

  const size_t buflen = std::min(txbuf_.size(),
                                  ngtcp2_conn_get_send_quantum2(conn_));

  auto nwrite = ngtcp2_conn_write_aggregate_pkt2(
      conn_, &ps.path, &pi,
      txbuf_.data(), buflen, &gso_size,
      write_pkt_cb, kMaxBurst, timestamp_ns());

  ngtcp2_conn_update_pkt_tx_time(conn_, timestamp_ns());
  schedule_timer();

  if (!write_queue_.empty() && (nwrite == 0 || (nwrite > 0 && gso_size < 256))) {
    ngtcp2_conn_info cinfo{};
    ngtcp2_conn_get_conn_info2(conn_, &cinfo);
    const auto queue_bytes_now = queued_bytes(write_queue_);
    const auto stream_end_offset = tx_stream_acked_offset_ + queue_bytes_now;
    const auto unacked_bytes = tx_stream_sent_offset_ > tx_stream_acked_offset_
                                   ? tx_stream_sent_offset_ - tx_stream_acked_offset_
                                   : 0;
    const auto unsent_bytes = stream_end_offset > tx_stream_sent_offset_
                                  ? stream_end_offset - tx_stream_sent_offset_
                                  : 0;
    const auto now = timestamp_ns();
    const auto expiry = ngtcp2_conn_get_expiry2(conn_);
    const auto wait_ns = expiry == UINT64_MAX ? UINT64_MAX
                                               : (expiry > now ? expiry - now : 0);
    // std::cerr << "[srv-tx] nwrite=" << nwrite
    //           << " gso=" << gso_size
    //           << " queue=" << write_queue_.size()
    //           << " queue_bytes=" << queue_bytes_now
    //           << " sent_off=" << tx_stream_sent_offset_
    //           << " acked_off=" << tx_stream_acked_offset_
    //           << " unacked_bytes=" << unacked_bytes
    //           << " unsent_bytes=" << unsent_bytes
    //           << " cwnd=" << cinfo.cwnd
    //           << " bif=" << cinfo.bytes_in_flight
    //           << " cwnd_left=" << ngtcp2_conn_get_cwnd_left2(conn_)
    //           << " send_quantum=" << ngtcp2_conn_get_send_quantum2(conn_)
    //           << " srtt_ns=" << cinfo.smoothed_rtt
    //           << " latest_rtt_ns=" << cinfo.latest_rtt
    //           << " wait_ns=" << wait_ns << "\n";
  }

  if (nwrite < 0) {
    NPRPC_QUIC_ERROR("write error: {}", ngtcp2_strerror(static_cast<int>(nwrite)));
    if (!last_error_.error_code)
      ngtcp2_ccerr_set_liberr(&last_error_, static_cast<int>(nwrite), nullptr, 0);
    handle_error();
    return;
  }

  if (nwrite == 0) return;

  send_gso(socket_.native_handle(), remote_ep_,
           txbuf_.data(), static_cast<size_t>(nwrite), gso_size);
}

void QuicNgtcp2ServerConn::schedule_timer()
{
  auto expiry = ngtcp2_conn_get_expiry(conn_);
  auto now    = timestamp_ns();

  if (expiry == UINT64_MAX) return;

  if (expiry <= now) {
    boost::asio::post(strand_, [self = shared_from_this()]() {
      if (!self->closed_) { self->handle_expiry(); self->do_write(); }
    });
    return;
  }

  timer_.expires_after(std::chrono::nanoseconds(expiry - now));
  timer_.async_wait(boost::asio::bind_executor(
      strand_,
      [self = shared_from_this()](boost::system::error_code ec) {
        if (ec || self->closed_) return;
        self->handle_expiry();
        self->do_write();
      }));
}

void QuicNgtcp2ServerConn::handle_expiry()
{
  int rv = ngtcp2_conn_handle_expiry(conn_, timestamp_ns());
  if (rv != 0) {
    NPRPC_QUIC_ERROR("ngtcp2_conn_handle_expiry: {}", ngtcp2_strerror(rv));
    ngtcp2_ccerr_set_liberr(&last_error_, rv, nullptr, 0);
    handle_error();
  }
}

void QuicNgtcp2ServerConn::handle_error()
{
  if (ngtcp2_conn_in_closing_period(conn_) ||
      ngtcp2_conn_in_draining_period(conn_))
    return;

  conn_close_buf_.resize(kMaxUdpPayload);
  ngtcp2_path_storage ps;
  ngtcp2_path_storage_zero(&ps);
  ngtcp2_pkt_info pi{};
  auto n = ngtcp2_conn_write_connection_close(
      conn_, &ps.path, &pi,
      conn_close_buf_.data(), conn_close_buf_.size(),
      &last_error_, timestamp_ns());
  if (n > 0) {
    conn_close_buf_.resize(static_cast<size_t>(n));
    listener_->send_to(remote_ep_, conn_close_buf_.data(), conn_close_buf_.size());
  }
  closed_ = true;
  remove_self();
}

void QuicNgtcp2ServerConn::remove_self()
{
  timer_.cancel();
  if (!handshake_finished_) {
    handshake_finished_ = true;
    record_handshake_failure(monitor::Transport::Quic);
  }
  active_connection_.reset();
  session_.reset();
  boost::asio::post(listener_->io_context(), [self = shared_from_this()]() {
    self->listener_->remove_connection(self);
  });
}

void QuicNgtcp2ServerConn::close()
{
  boost::asio::post(strand_, [self = shared_from_this()] {
    if (!self->closed_) {
      self->closed_ = true;
      self->remove_self();
    }
  });
}

void QuicNgtcp2ServerConn::on_stream_opened(int64_t stream_id)
{
  if (ngtcp2_is_bidi_stream(stream_id) && rpc_stream_id_ < 0) {
    rpc_stream_id_ = stream_id;
    NPRPC_QUIC_TRACE("RPC stream opened: {}", stream_id);
    maybe_create_session();
  }
}

void QuicNgtcp2ServerConn::maybe_create_session()
{
  if (session_) return;
  session_ = std::make_shared<QuicNgtcp2ServerSession>(shared_from_this());
  session_->bind_self(session_);
  session_->ctx().quic_client_authenticated = !g_cfg.quic_client_ca_file.empty() &&
      SSL_get_verify_result(ssl_) == X509_V_OK;
  session_->ctx().remote_endpoint = EndPoint(
      EndPointType::Quic,
      remote_ep_.address().to_string(),
      remote_ep_.port());
}

void QuicNgtcp2ServerConn::on_stream_data(int64_t stream_id,
                                           const uint8_t* data, size_t len)
{
  if (stream_id != rpc_stream_id_ || !session_) {
    ngtcp2_conn_extend_max_stream_offset(conn_, stream_id, len);
    ngtcp2_conn_extend_max_offset(conn_, len);
    return;
  }

  auto mb = rx_buf_.prepare(len);
  std::memcpy(mb.data(), data, len);
  rx_buf_.commit(len);

  ngtcp2_conn_extend_max_stream_offset(conn_, stream_id, len);
  ngtcp2_conn_extend_max_offset(conn_, len);

  // Dispatch complete NPRPC frames
  while (rx_buf_.size() >= sizeof(uint32_t)) {
    uint32_t frame_len = 0;
    std::memcpy(&frame_len, rx_buf_.data_ptr(), sizeof(frame_len));

    if (frame_len < sizeof(impl::flat::Header) || frame_len > kMaxNprpcFrame) {
      NPRPC_QUIC_ERROR("invalid frame length: {}", frame_len);
      handle_error();
      return;
    }
    if (rx_buf_.size() < frame_len) break;

    flat_buffer frame{frame_len};
    auto wmb = frame.prepare(frame_len);
    std::memcpy(wmb.data(), rx_buf_.data_ptr(), frame_len);
    frame.commit(frame_len);
    rx_buf_.consume(frame_len);

    session_->dispatch_request(frame);
  }
}

void QuicNgtcp2ServerConn::on_stream_data_acked(int64_t stream_id,
                                                 uint64_t offset,
                                                 uint64_t datalen)
{
  if (stream_id != rpc_stream_id_) return;
  release_acked_prefix(write_queue_, tx_stream_acked_offset_, offset, datalen);
}

// ngtcp2 static callbacks
int QuicNgtcp2ServerConn::cb_recv_client_initial(ngtcp2_conn* conn,
                                                   const ngtcp2_cid* dcid,
                                                   void*)
{
  return ngtcp2_crypto_recv_client_initial_cb(conn, dcid, nullptr);
}

int QuicNgtcp2ServerConn::cb_recv_crypto_data(ngtcp2_conn* conn,
                                               ngtcp2_encryption_level level,
                                               uint64_t offset,
                                               const uint8_t* data,
                                               size_t datalen, void*)
{
  return ngtcp2_crypto_recv_crypto_data_cb(conn, level, offset, data, datalen,
                                            nullptr);
}

int QuicNgtcp2ServerConn::cb_handshake_completed(ngtcp2_conn*, void* ud)
{
  auto* self = static_cast<QuicNgtcp2ServerConn*>(ud);
  self->handshake_finished_ = true;
  self->active_connection_ = std::make_unique<ActiveConnection>(monitor::Transport::Quic);
  NPRPC_QUIC_TRACE("server handshake completed for {}",
                   static_cast<QuicNgtcp2ServerConn*>(ud)->remote_ep_.address().to_string());
  return 0;
}

int QuicNgtcp2ServerConn::cb_recv_stream_data(ngtcp2_conn*, uint32_t /*flags*/,
                                               int64_t stream_id, uint64_t,
                                               const uint8_t* data,
                                               size_t datalen,
                                               void* ud, void*)
{
  static_cast<QuicNgtcp2ServerConn*>(ud)->on_stream_data(stream_id, data, datalen);
  return 0;
}

int QuicNgtcp2ServerConn::cb_acked_stream_data_offset(ngtcp2_conn*,
                                                       int64_t stream_id,
                                                       uint64_t offset,
                                                       uint64_t datalen,
                                                       void* ud,
                                                       void*)
{
  static_cast<QuicNgtcp2ServerConn*>(ud)->on_stream_data_acked(
      stream_id, offset, datalen);
  return 0;
}

int QuicNgtcp2ServerConn::cb_stream_open(ngtcp2_conn*, int64_t stream_id,
                                          void* ud)
{
  static_cast<QuicNgtcp2ServerConn*>(ud)->on_stream_opened(stream_id);
  return 0;
}

int QuicNgtcp2ServerConn::cb_stream_close(ngtcp2_conn*, uint32_t,
                                           int64_t stream_id, uint64_t,
                                           void* ud, void*)
{
  auto* self = static_cast<QuicNgtcp2ServerConn*>(ud);
  if (stream_id == self->rpc_stream_id_) {
    NPRPC_QUIC_TRACE("RPC stream closed");
    self->closed_ = true;
    self->remove_self();
  }
  return 0;
}

int QuicNgtcp2ServerConn::cb_get_new_connection_id(ngtcp2_conn*,
                                                    ngtcp2_cid* cid,
                                                    uint8_t* token,
                                                    size_t cidlen, void* ud)
{
  auto* self = static_cast<QuicNgtcp2ServerConn*>(ud);
  generate_cid(cid, cidlen);
  random_bytes(token, NGTCP2_STATELESS_RESET_TOKENLEN);
  self->listener_->associate_cid(cid, self->shared_from_this());
  return 0;
}

int QuicNgtcp2ServerConn::cb_remove_connection_id(ngtcp2_conn*,
                                                   const ngtcp2_cid* cid,
                                                   void* ud)
{
  static_cast<QuicNgtcp2ServerConn*>(ud)->listener_->dissociate_cid(cid);
  return 0;
}

void QuicNgtcp2ServerConn::cb_rand(uint8_t* dst, size_t len,
                                    const ngtcp2_rand_ctx*)
{
  random_bytes(dst, len);
}

// ===========================================================================
// QuicNgtcp2Listener — implementation
// ===========================================================================

QuicNgtcp2Listener::QuicNgtcp2Listener(boost::asio::io_context& ioc,
                                       std::string cert_file,
                                       std::string key_file,
                                       uint16_t    port)
    : ioc_(ioc)
    , cert_file_(std::move(cert_file))
    , key_file_(std::move(key_file))
    , port_(port)
    , socket_(ioc)
{}

QuicNgtcp2Listener::~QuicNgtcp2Listener() { stop(); }

static std::shared_ptr<SSL_CTX> make_server_ssl_context(
    const std::string& cert_file_, const std::string& key_file_,
    const std::string& client_ca_file)
{
  auto* ssl_ctx_ = SSL_CTX_new(TLS_server_method());
  std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> next(ssl_ctx_, SSL_CTX_free);
  if (!ssl_ctx_) { NPRPC_QUIC_ERROR("SSL_CTX_new failed"); return {}; }

#if defined(OPENSSL_IS_BORINGSSL)
  if (ngtcp2_crypto_boringssl_configure_server_context(ssl_ctx_) != 0) {
    NPRPC_QUIC_ERROR("ngtcp2_crypto_boringssl_configure_server_context failed");
    return {};
  }
#else
  SSL_CTX_set_options(ssl_ctx_,
      (SSL_OP_ALL & ~SSL_OP_DONT_INSERT_EMPTY_FRAGMENTS)
      | SSL_OP_SINGLE_ECDH_USE | SSL_OP_CIPHER_SERVER_PREFERENCE);
  if (SSL_CTX_set_ciphersuites(ssl_ctx_, crypto_ciphers()) != 1) {
    NPRPC_QUIC_ERROR("SSL_CTX_set_ciphersuites failed");
    return {};
  }
  SSL_CTX_set_mode(ssl_ctx_, SSL_MODE_RELEASE_BUFFERS);
#endif

  SSL_CTX_set_min_proto_version(ssl_ctx_, TLS1_3_VERSION);
  SSL_CTX_set_max_proto_version(ssl_ctx_, TLS1_3_VERSION);
#if defined(OPENSSL_IS_BORINGSSL)
  SSL_CTX_set_early_data_enabled(ssl_ctx_, 0);
#else
  SSL_CTX_set_max_early_data(ssl_ctx_, 0);
#endif

  if (SSL_CTX_set1_groups_list(ssl_ctx_, crypto_groups()) != 1) {
    NPRPC_QUIC_ERROR("SSL_CTX_set1_groups_list failed");
    return {};
  }

  // ALPN: select "nprpc"
  SSL_CTX_set_alpn_select_cb(
      ssl_ctx_,
      [](SSL*, const unsigned char** out, unsigned char* outlen,
         const unsigned char* in, unsigned int inlen, void*) -> int {
        for (unsigned int i = 0; i < inlen; ) {
          uint8_t len = in[i];
          if (i + 1u + len > inlen) break;
          if (len == 5 && std::memcmp(in + i + 1, "nprpc", 5) == 0) {
            *out    = in + i + 1;
            *outlen = len;
            return SSL_TLSEXT_ERR_OK;
          }
          i += 1u + len;
        }
        return SSL_TLSEXT_ERR_ALERT_FATAL;
      },
      nullptr);

  if (SSL_CTX_use_certificate_chain_file(ssl_ctx_, cert_file_.c_str()) != 1) {
    NPRPC_QUIC_ERROR("SSL_CTX_use_certificate_chain_file: {}",
                     ERR_error_string(ERR_get_error(), nullptr));
    return {};
  }
  if (SSL_CTX_use_PrivateKey_file(ssl_ctx_, key_file_.c_str(),
                                   SSL_FILETYPE_PEM) != 1) {
    NPRPC_QUIC_ERROR("SSL_CTX_use_PrivateKey_file: {}",
                     ERR_error_string(ERR_get_error(), nullptr));
    return {};
  }
  if (SSL_CTX_check_private_key(ssl_ctx_) != 1) {
    NPRPC_QUIC_ERROR("certificate/key mismatch");
    return {};
  }

  if (!client_ca_file.empty()) {
    if (SSL_CTX_load_verify_locations(ssl_ctx_, client_ca_file.c_str(), nullptr) != 1)
      return {};
    SSL_CTX_set_verify(ssl_ctx_, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, nullptr);
  }

  static const unsigned char sid[] = "nprpc quic server";
  SSL_CTX_set_session_id_context(ssl_ctx_, sid, sizeof(sid) - 1);
  return {next.release(), SSL_CTX_free};
}

bool QuicNgtcp2Listener::setup_ssl_ctx()
{
  auto next = make_server_ssl_context(cert_file_, key_file_, g_cfg.quic_client_ca_file);
  if (!next) return false;
  ssl_ctx_.store(std::move(next));
  return true;
}

void validate_quic_server_tls()
{
  if (!make_server_ssl_context(g_cfg.quic_cert_file, g_cfg.quic_key_file,
                               g_cfg.quic_client_ca_file))
    throw Exception("Invalid QUIC server credentials or client trust bundle");
}

bool QuicNgtcp2Listener::start()
{
  if (!setup_ssl_ctx()) return false;

  boost::system::error_code ec;
  socket_.open(boost::asio::ip::udp::v6(), ec);
  if (ec) { NPRPC_QUIC_ERROR("socket open: {}", ec.message()); return false; }

  socket_.set_option(boost::asio::ip::v6_only(false), ec);
  socket_.set_option(boost::asio::socket_base::reuse_address(true), ec);
  socket_.set_option(
      boost::asio::socket_base::receive_buffer_size(2 * 1024 * 1024), ec);
  socket_.set_option(
      boost::asio::socket_base::send_buffer_size(2 * 1024 * 1024), ec);
  socket_.bind(
      boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v6(), port_), ec);
  if (ec) { NPRPC_QUIC_ERROR("bind: {}", ec.message()); return false; }

  local_ep_ = socket_.local_endpoint();
  running_.store(true, std::memory_order_release);
  do_receive();
  NPRPC_QUIC_TRACE("QUIC listener on port {}", port_);
  return true;
}

void QuicNgtcp2Listener::stop()
{
  running_.store(false);
  boost::system::error_code ec;
  socket_.close(ec);
  std::lock_guard lock(conns_mutex_);
  for (auto& [cid, conn] : conns_) conn->close();
  conns_.clear();
  ssl_ctx_.store(nullptr);
}

void QuicNgtcp2Listener::do_receive()
{
  if (!running_.load(std::memory_order_acquire)) return;
  socket_.async_receive_from(
      boost::asio::buffer(recv_buf_), sender_ep_,
      [this, self = shared_from_this()](boost::system::error_code ec, size_t bytes) {
        if (ec) {
          if (running_.load()) NPRPC_QUIC_ERROR("recv: {}", ec.message());
          return;
        }
        handle_packet(sender_ep_, recv_buf_.data(), bytes);
        do_receive();
      });
}

std::shared_ptr<QuicNgtcp2ServerConn>
QuicNgtcp2Listener::find_connection(const ngtcp2_cid* dcid)
{
  std::lock_guard lock(conns_mutex_);
  auto it = conns_.find(cid_key(dcid));
  return it != conns_.end() ? it->second : nullptr;
}

void QuicNgtcp2Listener::associate_cid(const ngtcp2_cid* cid,
                                        std::shared_ptr<QuicNgtcp2ServerConn> conn)
{
  std::lock_guard lock(conns_mutex_);
  conns_[cid_key(cid)] = std::move(conn);
}

void QuicNgtcp2Listener::dissociate_cid(const ngtcp2_cid* cid)
{
  std::lock_guard lock(conns_mutex_);
  conns_.erase(cid_key(cid));
}

void QuicNgtcp2Listener::remove_connection(
    std::shared_ptr<QuicNgtcp2ServerConn> conn)
{
  std::lock_guard lock(conns_mutex_);
  for (auto it = conns_.begin(); it != conns_.end(); ) {
    if (it->second == conn) it = conns_.erase(it);
    else ++it;
  }
}

void QuicNgtcp2Listener::handle_packet(
    const boost::asio::ip::udp::endpoint& from,
    const uint8_t* data, size_t len)
{
  ngtcp2_version_cid vc;
  int rv = ngtcp2_pkt_decode_version_cid(&vc, data, len, kScidLen);
  if (rv < 0) {
    NPRPC_QUIC_TRACE("decode_version_cid: {}", ngtcp2_strerror(rv));
    return;
  }

  ngtcp2_cid dcid;
  dcid.datalen = vc.dcidlen;
  std::memcpy(dcid.data, vc.dcid, vc.dcidlen);

  auto conn = find_connection(&dcid);
  if (conn) {
    ngtcp2_pkt_info pi{};
    flat_buffer pkt{len};
    auto mb = pkt.prepare(len);
    std::memcpy(mb.data(), data, len);
    pkt.commit(len);
    conn->enqueue_packet(std::move(pkt), pi);
    return;
  }

  if (rv == NGTCP2_ERR_VERSION_NEGOTIATION) return; // ignore
  handle_new_connection(from, data, len, vc);
}

int QuicNgtcp2Listener::handle_new_connection(
    const boost::asio::ip::udp::endpoint& from,
    const uint8_t* data, size_t len,
    const ngtcp2_version_cid& /*vc*/)
{
  ngtcp2_pkt_hd hd;
  int rv = ngtcp2_accept(&hd, data, len);
  if (rv == NGTCP2_ERR_RETRY) {
    NPRPC_QUIC_TRACE("new connection needs retry — skipping");
    return 0;
  }
  if (rv != 0) {
    NPRPC_QUIC_TRACE("ngtcp2_accept: {}", ngtcp2_strerror(rv));
    return 0;
  }

  auto conn = std::make_shared<QuicNgtcp2ServerConn>(
      this, socket_, local_ep_, from,
      &hd.scid, &hd.dcid, hd.version, ssl_ctx_.load());

  if (!conn->init()) {
    NPRPC_QUIC_ERROR("failed to init new connection from {}", from.address().to_string());
    return -1;
  }

  std::lock_guard lock(conns_mutex_);
  // Register under our server CID so subsequent packets find it
  conns_[cid_key(&conn->scid())] = conn;
  // Also register under the client's original destination CID (what the
  // first Initial packet was addressed to — the listener CID we don't own)
  conns_[std::string(reinterpret_cast<const char*>(hd.dcid.data),
                      hd.dcid.datalen)] = conn;

  ngtcp2_pkt_info pi{};
  flat_buffer pkt{len};
  auto mb = pkt.prepare(len);
  std::memcpy(mb.data(), data, len);
  pkt.commit(len);
  conn->enqueue_packet(std::move(pkt), pi);

  NPRPC_QUIC_TRACE("accepted connection from {}", from.address().to_string());
  return 0;
}

// ===========================================================================
// QuicNgtcp2ClientConn — client-side QUIC connection
// ===========================================================================
class QuicNgtcp2ClientConn
    : public std::enable_shared_from_this<QuicNgtcp2ClientConn>
{
public:
  using Handler = std::function<void(const boost::system::error_code&, flat_buffer&)>;

  QuicNgtcp2ClientConn(boost::asio::io_context& ioc, const EndPoint& ep);
  ~QuicNgtcp2ClientConn();

  // Synchronously connect; blocks calling thread (not the io_context thread)
  bool connect();
  void close();
  bool is_connected() const { return connected_.load(); }

  void send_receive_async(flat_buffer&&               buf,
                          std::optional<Handler>&&    handler,
                          uint32_t                    timeout_ms);
  void send_stream_message(flat_buffer&& buf);

  // Register a handler for server-initiated Request frames (stream chunks)
  void set_request_dispatch(std::function<void(flat_buffer&)> fn)
  {
    request_dispatch_ = std::move(fn);
  }

private:
  void do_connect(boost::asio::ip::udp::endpoint      remote_ep,
                  std::move_only_function<void(bool)> on_done);
  void do_receive();
  void on_udp_recv(const uint8_t* data, size_t len);
  void do_write();
  void schedule_timer();
  void handle_expiry();
  void handle_error();
  void on_stream_data(int64_t stream_id, const uint8_t* data, size_t len);
  void on_stream_data_acked(int64_t stream_id, uint64_t offset, uint64_t datalen);
  void on_handshake_complete();
  bool setup_ssl_ctx();

  uint32_t generate_request_id()
  {
    return next_request_id_.fetch_add(1, std::memory_order_relaxed);
  }

  // ngtcp2 static callbacks
  static int  cb_recv_crypto_data(ngtcp2_conn*, ngtcp2_encryption_level,
                                   uint64_t, const uint8_t*, size_t, void*);
  static int  cb_handshake_completed(ngtcp2_conn*, void*);
  static int  cb_recv_stream_data(ngtcp2_conn*, uint32_t, int64_t, uint64_t,
                                   const uint8_t*, size_t, void*, void*);
  static int  cb_acked_stream_data_offset(ngtcp2_conn*, int64_t, uint64_t,
                                           uint64_t, void*, void*);
  static int  cb_stream_open(ngtcp2_conn*, int64_t, void*);
  static int  cb_stream_close(ngtcp2_conn*, uint32_t, int64_t, uint64_t, void*, void*);
  static int  cb_get_new_connection_id(ngtcp2_conn*, ngtcp2_cid*, uint8_t*, size_t, void*);
  static int  cb_remove_connection_id(ngtcp2_conn*, const ngtcp2_cid*, void*);
  static void cb_rand(uint8_t*, size_t, const ngtcp2_rand_ctx*);

  boost::asio::io_context& ioc_;
  EndPoint                 endpoint_;
  boost::asio::ip::udp::socket socket_;
  boost::asio::ip::udp::endpoint remote_ep_;
  boost::asio::strand<boost::asio::io_context::executor_type> strand_;
  boost::asio::steady_timer timer_;

  SSL_CTX*            ssl_ctx_  = nullptr;
  ngtcp2_conn*        conn_     = nullptr;
  ngtcp2_crypto_conn_ref conn_ref_{};
#if !defined(OPENSSL_IS_BORINGSSL)
  ngtcp2_crypto_ossl_ctx* ossl_ctx_ = nullptr;
#endif
  SSL* ssl_ = nullptr;

  ngtcp2_cid scid_; // our ephemeral CID
  ngtcp2_cid dcid_; // server's initial destination CID (random)

  int64_t     rpc_stream_id_ = -1;
  flat_buffer rx_buf_{flat_buffer::default_initial_size()};
  std::deque<flat_buffer> write_queue_;
  uint64_t    tx_stream_acked_offset_ = 0;
  uint64_t    tx_stream_sent_offset_  = 0;

  struct PendingRequest {
    std::optional<Handler>                    handler;
    std::chrono::steady_clock::time_point     timeout_point;
  };
  std::unordered_map<uint32_t, PendingRequest> pending_requests_;
  std::atomic<uint32_t>                        next_request_id_{1};

  std::atomic<bool> connected_{false};
  bool              closed_    = false;

  // Contiguous send buffer: kMaxBurst slots of kMaxUdpPayload each.
  std::array<uint8_t, kGsoMaxBytes>               txbuf_;
  std::array<uint8_t, 65536>                      recv_buf_;
  ngtcp2_ccerr                   last_error_{};

  // Stored during async connect; resolved by on_handshake_complete
  std::move_only_function<void(bool)> connect_callback_;

  // Dispatch hook for server-initiated Request frames (e.g. stream chunks)
  std::function<void(flat_buffer&)> request_dispatch_;

  // Packets/sends queued before handshake completes
  struct PreConnectSend {
    flat_buffer            buf;
    std::optional<Handler> handler;
    uint32_t               timeout_ms;
  };
  std::vector<PreConnectSend> pre_connect_queue_;
};

// ---------------------------------------------------------------------------
QuicNgtcp2ClientConn::QuicNgtcp2ClientConn(boost::asio::io_context& ioc,
                                            const EndPoint& ep)
    : ioc_(ioc)
    , endpoint_(ep)
    , socket_(ioc)
    , strand_(boost::asio::make_strand(ioc))
    , timer_(ioc)
{
  conn_ref_.get_conn = [](ngtcp2_crypto_conn_ref* r) -> ngtcp2_conn* {
    return static_cast<QuicNgtcp2ClientConn*>(r->user_data)->conn_;
  };
  conn_ref_.user_data = this;
}

QuicNgtcp2ClientConn::~QuicNgtcp2ClientConn()
{
  timer_.cancel();
  if (conn_) ngtcp2_conn_del(conn_);
  if (ssl_ctx_) SSL_CTX_free(ssl_ctx_);
#if defined(OPENSSL_IS_BORINGSSL)
  if (ssl_) { SSL_set_app_data(ssl_, nullptr); SSL_free(ssl_); }
#else
  if (ossl_ctx_) {
    if (auto* s = ngtcp2_crypto_ossl_ctx_get_ssl(ossl_ctx_)) {
      SSL_set_app_data(s, nullptr);
      SSL_free(s);
    }
    ngtcp2_crypto_ossl_ctx_del(ossl_ctx_);
  }
#endif
  // Fail all pending requests
  const boost::system::error_code ec{boost::asio::error::connection_aborted};
  for (auto& [id, req] : pending_requests_) {
    if (req.handler) {
      flat_buffer empty;
      (*req.handler)(ec, empty);
    }
  }
}

bool QuicNgtcp2ClientConn::setup_ssl_ctx()
{
  ssl_ctx_ = SSL_CTX_new(TLS_client_method());
  if (!ssl_ctx_) return false;

#if defined(OPENSSL_IS_BORINGSSL)
  if (ngtcp2_crypto_boringssl_configure_client_context(ssl_ctx_) != 0) {
    SSL_CTX_free(ssl_ctx_); ssl_ctx_ = nullptr; return false;
  }
#endif

  SSL_CTX_set_min_proto_version(ssl_ctx_, TLS1_3_VERSION);
  SSL_CTX_set_max_proto_version(ssl_ctx_, TLS1_3_VERSION);
  auto tls = client_tls_config.load(std::memory_order_acquire);
  if (!configure_client_credentials(ssl_ctx_, *tls)) return false;
  if (SSL_CTX_set1_groups_list(ssl_ctx_, crypto_groups()) != 1) {
    SSL_CTX_free(ssl_ctx_); ssl_ctx_ = nullptr; return false;
  }
  return true;
}

bool QuicNgtcp2ClientConn::connect()
{
  if (!setup_ssl_ctx()) {
    NPRPC_QUIC_ERROR("client SSL context setup failed");
    return false;
  }

  // Resolve endpoint address
  boost::asio::ip::address addr;
  boost::system::error_code ec;
  addr = boost::asio::ip::make_address(std::string(endpoint_.hostname()), ec);
  if (ec) {
    // Simple loopback fallback; production code should do async resolve
    addr = boost::asio::ip::address_v4::loopback();
  }
  remote_ep_ = {addr, static_cast<unsigned short>(endpoint_.port())};

  std::promise<bool> promise;
  auto future = promise.get_future();
  boost::asio::post(
      strand_,
      [this, remote_ep = remote_ep_, p = std::move(promise)]() mutable {
        do_connect(remote_ep, [p = std::move(p)](bool ok) mutable {
          p.set_value(ok);
        });
      });

  return future.get();
}

void QuicNgtcp2ClientConn::do_connect(boost::asio::ip::udp::endpoint   remote_ep,
                                       std::move_only_function<void(bool)> on_done)
{
  boost::system::error_code ec;
  const auto proto = remote_ep.address().is_v6()
      ? boost::asio::ip::udp::v6()
      : boost::asio::ip::udp::v4();

  socket_.open(proto, ec);
  if (ec) { NPRPC_QUIC_ERROR("socket open: {}", ec.message()); on_done(false); return; }
  socket_.set_option(boost::asio::socket_base::receive_buffer_size(4 * 1024 * 1024), ec);
  socket_.set_option(boost::asio::socket_base::send_buffer_size(4 * 1024 * 1024), ec);
  socket_.bind(boost::asio::ip::udp::endpoint(proto, 0), ec);
  if (ec) { NPRPC_QUIC_ERROR("socket bind: {}", ec.message()); on_done(false); return; }

  auto local_ep = socket_.local_endpoint();

  generate_cid(&scid_, kScidLen);
  generate_cid(&dcid_, kScidLen); // random server destination CID

  // Create SSL
  ssl_ = SSL_new(ssl_ctx_);
  if (!ssl_) { NPRPC_QUIC_ERROR("SSL_new failed"); on_done(false); return; }

  const std::string server_name(endpoint_.hostname());
  auto* verify = SSL_get0_param(ssl_);
  boost::system::error_code address_error;
  boost::asio::ip::make_address(server_name, address_error);
  const int verified_name = address_error
      ? X509_VERIFY_PARAM_set1_host(verify, server_name.c_str(), server_name.size())
      : X509_VERIFY_PARAM_set1_ip_asc(verify, server_name.c_str());
  if (verified_name != 1) { on_done(false); return; }
  X509_VERIFY_PARAM_set_hostflags(verify, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
  SSL_set_tlsext_host_name(ssl_, server_name.c_str());
  SSL_set_alpn_protos(ssl_, kNprpcAlpn, kNprpcAlpnLen);

  // ngtcp2 client callbacks
  ngtcp2_callbacks cbs{
      .client_initial        = ngtcp2_crypto_client_initial_cb,
      .recv_crypto_data      = cb_recv_crypto_data,
      .handshake_completed   = cb_handshake_completed,
      .encrypt               = ngtcp2_crypto_encrypt_cb,
      .decrypt               = ngtcp2_crypto_decrypt_cb,
      .hp_mask               = ngtcp2_crypto_hp_mask_cb,
      .recv_stream_data      = cb_recv_stream_data,
      .acked_stream_data_offset = cb_acked_stream_data_offset,
      .stream_open           = cb_stream_open,
      .stream_close          = cb_stream_close,
      .recv_retry            = ngtcp2_crypto_recv_retry_cb,
      .rand                  = cb_rand,
      .get_new_connection_id = cb_get_new_connection_id,
      .remove_connection_id  = cb_remove_connection_id,
      .update_key            = ngtcp2_crypto_update_key_cb,
      .delete_crypto_aead_ctx  = ngtcp2_crypto_delete_crypto_aead_ctx_cb,
      .delete_crypto_cipher_ctx = ngtcp2_crypto_delete_crypto_cipher_ctx_cb,
      .get_path_challenge_data  = ngtcp2_crypto_get_path_challenge_data_cb,
      .version_negotiation      = ngtcp2_crypto_version_negotiation_cb,
  };

  ngtcp2_settings settings;
  ngtcp2_settings_default(&settings);
  settings.initial_ts           = timestamp_ns();
  settings.cc_algo             = NGTCP2_CC_ALGO_CUBIC;
  settings.max_tx_udp_payload_size = kMaxUdpPayload;
  settings.initial_rtt        = 100 * NGTCP2_MICROSECONDS;
  settings.ack_thresh         = 1;

  ngtcp2_transport_params params;
  ngtcp2_transport_params_default(&params);
  params.initial_max_stream_data_bidi_local  = 16 * 1024 * 1024;
  params.initial_max_stream_data_bidi_remote = 16 * 1024 * 1024;
  params.initial_max_stream_data_uni         = 0;
  params.initial_max_data                    = 32 * 1024 * 1024;
  params.max_ack_delay                       = 1 * NGTCP2_MILLISECONDS;
  params.initial_max_streams_bidi            = 0; // server doesn't open streams
  params.initial_max_streams_uni             = 0;
  params.max_idle_timeout                    = 30 * NGTCP2_SECONDS;
  params.active_connection_id_limit          = 7;

  sockaddr_storage lss, rss;
  auto path = make_path(local_ep, remote_ep, lss, rss);

  int rv = ngtcp2_conn_client_new(&conn_, &dcid_, &scid_, &path,
                                   NGTCP2_PROTO_VER_V1, &cbs,
                                   &settings, &params, nullptr, this);
  if (rv != 0) {
    NPRPC_QUIC_ERROR("ngtcp2_conn_client_new: {}", ngtcp2_strerror(rv));
    on_done(false); return;
  }

#if defined(OPENSSL_IS_BORINGSSL)
  SSL_set_app_data(ssl_, &conn_ref_);
  SSL_set_connect_state(ssl_);
  ngtcp2_conn_set_tls_native_handle(conn_, ssl_);
#else
  if (ngtcp2_crypto_ossl_ctx_new(&ossl_ctx_, nullptr) != 0) {
    NPRPC_QUIC_ERROR("ngtcp2_crypto_ossl_ctx_new failed");
    on_done(false); return;
  }
  ngtcp2_crypto_ossl_ctx_set_ssl(ossl_ctx_, ssl_);
  if (ngtcp2_crypto_ossl_configure_client_session(ssl_) != 0) {
    NPRPC_QUIC_ERROR("ngtcp2_crypto_ossl_configure_client_session failed");
    on_done(false); return;
  }
  SSL_set_app_data(ssl_, &conn_ref_);
  SSL_set_connect_state(ssl_);
  ngtcp2_conn_set_tls_native_handle(conn_, ossl_ctx_);
#endif

  connect_callback_ = std::move(on_done);
  do_write();    // send Initial packet
  do_receive();  // start reading server responses
}

void QuicNgtcp2ClientConn::do_receive()
{
  if (closed_) return;
  socket_.async_receive_from(
      boost::asio::buffer(recv_buf_), remote_ep_,
      boost::asio::bind_executor(
          strand_,
          [self = shared_from_this()](boost::system::error_code ec, size_t n) {
            if (ec || self->closed_) return;
            self->on_udp_recv(self->recv_buf_.data(), n);
            self->do_receive();
          }));
}

void QuicNgtcp2ClientConn::on_udp_recv(const uint8_t* data, size_t len)
{
  if (closed_ || !conn_) return;

  sockaddr_storage lss, rss;
  auto local_ep = socket_.local_endpoint();
  auto path = make_path(local_ep, remote_ep_, lss, rss);
  ngtcp2_pkt_info pi{};

  int rv = ngtcp2_conn_read_pkt(conn_, &path, &pi, data, len, timestamp_ns());
  if (rv != 0) {
    switch (rv) {
    case NGTCP2_ERR_DRAINING:
    case NGTCP2_ERR_DROP_CONN:
      handle_error();
      return;
    default:
      NPRPC_QUIC_ERROR("client read_pkt: {}", ngtcp2_strerror(rv));
      if (!last_error_.error_code)
        ngtcp2_ccerr_set_liberr(&last_error_, rv, nullptr, 0);
      handle_error();
      return;
    }
  }
  schedule_timer();
  do_write();
}

void QuicNgtcp2ClientConn::do_write()
{
  if (closed_ || !conn_) return;

  // std::cerr << "We are here!\n";
  // ngtcp2_write_pkt callback: called once per packet slot by
  // ngtcp2_conn_write_aggregate_pkt2.  user_data = this.
  auto write_pkt_cb = [](ngtcp2_conn* conn, ngtcp2_path* path,
                          ngtcp2_pkt_info* pi, uint8_t* dest, size_t destlen,
                          ngtcp2_tstamp ts, void* user_data) -> ngtcp2_ssize {
    auto* self = static_cast<QuicNgtcp2ClientConn*>(user_data);
    const bool has_stream  = self->rpc_stream_id_ >= 0;
    const bool has_data    = !self->write_queue_.empty();
    const uint64_t fc_left = ngtcp2_conn_get_max_data_left(conn);
    // std::cerr << "[cli-cb] has_stream=" << has_stream
    //           << " has_data=" << has_data
    //           << " fc_left=" << fc_left
    //           << " destlen=" << destlen << "\n";
    if (has_stream && has_data && fc_left > 0) {
      std::array<ngtcp2_vec, kMaxWriteVecs> vecs{};
      const auto vec_count = build_unsent_vecs(
          self->write_queue_,
          self->tx_stream_acked_offset_,
          self->tx_stream_sent_offset_,
          vecs);
      if (vec_count == 0) {
        return ngtcp2_conn_write_pkt(conn, path, pi, dest, destlen, ts);
      }
      ngtcp2_ssize ndatalen = 0;
      auto nwrite = ngtcp2_conn_writev_stream(
          conn, path, pi, dest, destlen, &ndatalen,
          NGTCP2_WRITE_STREAM_FLAG_PADDING,
          self->rpc_stream_id_, vecs.data(), vec_count, ts);
      // std::cerr << "[cli-cb] writev_stream nwrite=" << nwrite << " ndatalen=" << ndatalen << "\n";
      if (nwrite == NGTCP2_ERR_STREAM_DATA_BLOCKED ||
          nwrite == NGTCP2_ERR_STREAM_SHUT_WR ||
          nwrite == NGTCP2_ERR_STREAM_NOT_FOUND) {
        // std::cerr << "[cli-cb] stream blocked/shut/not_found, falling back\n";
        return ngtcp2_conn_write_pkt(conn, path, pi, dest, destlen, ts);
      }
      if (nwrite > 0 && ndatalen > 0) {
        self->tx_stream_sent_offset_ += static_cast<uint64_t>(ndatalen);
      }
      return nwrite;
    }
    return ngtcp2_conn_write_pkt(conn, path, pi, dest, destlen, ts);
  };

  ngtcp2_path_storage ps;
  ngtcp2_path_storage_zero(&ps);
  ngtcp2_pkt_info pi{};
  size_t gso_size = 0;

  const size_t buflen = std::min(txbuf_.size(),
                                  ngtcp2_conn_get_send_quantum2(conn_));

  auto nwrite = ngtcp2_conn_write_aggregate_pkt2(
      conn_, &ps.path, &pi,
      txbuf_.data(), buflen, &gso_size,
      write_pkt_cb, kMaxBurst, timestamp_ns());

  ngtcp2_conn_update_pkt_tx_time(conn_, timestamp_ns());
  schedule_timer();

  if (!write_queue_.empty() && (nwrite == 0 || (nwrite > 0 && gso_size < 256))) {
    ngtcp2_conn_info cinfo{};
    ngtcp2_conn_get_conn_info2(conn_, &cinfo);
    const auto queue_bytes_now = queued_bytes(write_queue_);
    const auto stream_end_offset = tx_stream_acked_offset_ + queue_bytes_now;
    const auto unacked_bytes = tx_stream_sent_offset_ > tx_stream_acked_offset_
                                   ? tx_stream_sent_offset_ - tx_stream_acked_offset_
                                   : 0;
    const auto unsent_bytes = stream_end_offset > tx_stream_sent_offset_
                                  ? stream_end_offset - tx_stream_sent_offset_
                                  : 0;
    const auto now = timestamp_ns();
    const auto expiry = ngtcp2_conn_get_expiry2(conn_);
    const auto wait_ns = expiry == UINT64_MAX ? UINT64_MAX
                                               : (expiry > now ? expiry - now : 0);
    // std::cerr << "[cli-tx] nwrite=" << nwrite
    //           << " gso=" << gso_size
    //           << " queue=" << write_queue_.size()
    //           << " queue_bytes=" << queue_bytes_now
    //           << " sent_off=" << tx_stream_sent_offset_
    //           << " acked_off=" << tx_stream_acked_offset_
    //           << " unacked_bytes=" << unacked_bytes
    //           << " unsent_bytes=" << unsent_bytes
    //           << " cwnd=" << cinfo.cwnd
    //           << " bif=" << cinfo.bytes_in_flight
    //           << " cwnd_left=" << ngtcp2_conn_get_cwnd_left2(conn_)
    //           << " send_quantum=" << ngtcp2_conn_get_send_quantum2(conn_)
    //           << " srtt_ns=" << cinfo.smoothed_rtt
    //           << " latest_rtt_ns=" << cinfo.latest_rtt
    //           << " wait_ns=" << wait_ns << "\n";
  }

  if (nwrite < 0) {
    NPRPC_QUIC_ERROR("client write: {}", ngtcp2_strerror(static_cast<int>(nwrite)));
    handle_error();
    return;
  }

  if (nwrite == 0) return;

  send_gso(socket_.native_handle(), remote_ep_,
           txbuf_.data(), static_cast<size_t>(nwrite), gso_size);
}

void QuicNgtcp2ClientConn::schedule_timer()
{
  auto expiry = ngtcp2_conn_get_expiry(conn_);
  auto now    = timestamp_ns();
  if (expiry == UINT64_MAX) return;
  if (expiry <= now) {
    boost::asio::post(strand_, [self = shared_from_this()]() {
      if (!self->closed_) { self->handle_expiry(); self->do_write(); }
    });
    return;
  }
  timer_.expires_after(std::chrono::nanoseconds(expiry - now));
  timer_.async_wait(boost::asio::bind_executor(
      strand_,
      [self = shared_from_this()](boost::system::error_code ec) {
        if (ec || self->closed_) return;
        self->handle_expiry();
        self->do_write();
      }));
}

void QuicNgtcp2ClientConn::handle_expiry()
{
  int rv = ngtcp2_conn_handle_expiry(conn_, timestamp_ns());
  if (rv != 0) {
    NPRPC_QUIC_ERROR("client handle_expiry: {}", ngtcp2_strerror(rv));
    ngtcp2_ccerr_set_liberr(&last_error_, rv, nullptr, 0);
    handle_error();
  }
}

void QuicNgtcp2ClientConn::handle_error()
{
  if (closed_) return;
  closed_ = true;
  connected_.store(false, std::memory_order_release);
  timer_.cancel();
  boost::system::error_code ignored;
  socket_.close(ignored);

  // Invoke connect callback with failure (if still connecting)
  if (connect_callback_) {
    auto cb = std::move(connect_callback_);
    cb(false);
  }

  // Fail all pending requests
  const boost::system::error_code ec{boost::asio::error::connection_aborted};
  for (auto& [id, req] : pending_requests_) {
    if (req.handler) {
      flat_buffer empty;
      (*req.handler)(ec, empty);
    }
  }
  pending_requests_.clear();
}

void QuicNgtcp2ClientConn::on_stream_data(int64_t stream_id,
                                           const uint8_t* data, size_t len)
{
  if (stream_id != rpc_stream_id_) {
    if (conn_) {
      ngtcp2_conn_extend_max_stream_offset(conn_, stream_id, len);
      ngtcp2_conn_extend_max_offset(conn_, len);
    }
    return;
  }

  auto mb = rx_buf_.prepare(len);
  std::memcpy(mb.data(), data, len);
  rx_buf_.commit(len);

  if (conn_) {
    ngtcp2_conn_extend_max_stream_offset(conn_, stream_id, len);
    ngtcp2_conn_extend_max_offset(conn_, len);
  }

  // Dispatch complete NPRPC response frames
  while (rx_buf_.size() >= sizeof(uint32_t)) {
    uint32_t frame_len = 0;
    std::memcpy(&frame_len, rx_buf_.data_ptr(), sizeof(frame_len));
    if (frame_len < sizeof(impl::flat::Header) || frame_len > kMaxNprpcFrame) {
      NPRPC_QUIC_ERROR("client: invalid frame length: {}", frame_len);
      handle_error(); return;
    }
    if (rx_buf_.size() < frame_len) break;

    flat_buffer frame{frame_len};
    auto wmb = frame.prepare(frame_len);
    std::memcpy(wmb.data(), rx_buf_.data_ptr(), frame_len);
    frame.commit(frame_len);
    rx_buf_.consume(frame_len);

    // Check message type: Answer -> match pending request, Request -> server push (stream chunk)
    impl::flat::Header_Direct hdr(frame, 0);
    if (hdr.msg_type() == impl::MessageType::Answer) {
      const uint32_t rid = hdr.request_id();
      auto it = pending_requests_.find(rid);
      if (it != pending_requests_.end()) {
        if (it->second.handler)
          (*it->second.handler)(boost::system::error_code{}, frame);
        pending_requests_.erase(it);
      } else {
        NPRPC_QUIC_TRACE("client: no pending request for rid={}", rid);
      }
    } else {
      // Server-initiated Request: stream chunk or other push — dispatch to session
      if (request_dispatch_)
        request_dispatch_(frame);
    }
  }
}

void QuicNgtcp2ClientConn::on_stream_data_acked(int64_t stream_id,
                                                 uint64_t offset,
                                                 uint64_t datalen)
{
  if (stream_id != rpc_stream_id_) return;
  release_acked_prefix(write_queue_, tx_stream_acked_offset_, offset, datalen);
}

void QuicNgtcp2ClientConn::on_handshake_complete()
{
  NPRPC_QUIC_TRACE("client handshake complete");

  // Open our RPC bidi stream
  int rv = ngtcp2_conn_open_bidi_stream(conn_, &rpc_stream_id_, nullptr);
  if (rv != 0) {
    NPRPC_QUIC_ERROR("ngtcp2_conn_open_bidi_stream: {}", ngtcp2_strerror(rv));
    handle_error(); return;
  }
  NPRPC_QUIC_TRACE("opened RPC stream id={}", rpc_stream_id_);

  connected_.store(true, std::memory_order_release);

  if (connect_callback_) {
    auto cb = std::move(connect_callback_);
    cb(true);
  }

  // Flush any sends queued before handshake
  for (auto& ps : pre_connect_queue_) {
    send_receive_async(std::move(ps.buf), std::move(ps.handler), ps.timeout_ms);
  }
  pre_connect_queue_.clear();
}

void QuicNgtcp2ClientConn::send_receive_async(flat_buffer&&            buf,
                                               std::optional<Handler>&& handler,
                                               uint32_t                 timeout_ms)
{
  boost::asio::post(
      strand_,
      [self = shared_from_this(),
       b    = std::move(buf),
       h    = std::move(handler),
       timeout_ms]() mutable {
        if (self->closed_) {
          if (h) {
            flat_buffer empty;
            (*h)(boost::asio::error::connection_aborted, empty);
          }
          return;
        }

        if (!self->connected_.load()) {
          // Queue until handshake completes
          self->pre_connect_queue_.push_back({std::move(b), std::move(h), timeout_ms});
          return;
        }

        const uint32_t rid = self->generate_request_id();
        inject_request_id(b, rid);

        auto tp = std::chrono::steady_clock::now()
                  + std::chrono::milliseconds(timeout_ms ? timeout_ms : 30000);
        self->pending_requests_.emplace(rid, PendingRequest{std::move(h), tp});
        self->write_queue_.emplace_back(std::move(b));
        self->do_write();
      });
}

void QuicNgtcp2ClientConn::send_stream_message(flat_buffer&& buf)
{
  boost::asio::post(
      strand_,
      [self = shared_from_this(), b = std::move(buf)]() mutable {
        if (self->closed_) return;
        self->write_queue_.emplace_back(std::move(b));
        self->do_write();
      });
}

void QuicNgtcp2ClientConn::close()
{
  boost::asio::post(strand_, [self = shared_from_this()]() {
    if (!self->closed_ && self->conn_ && self->socket_.is_open()) {
      std::array<uint8_t, kMaxUdpPayload> packet;
      ngtcp2_path_storage ps;
      ngtcp2_path_storage_zero(&ps);
      ngtcp2_pkt_info pi{};
      ngtcp2_ccerr error{};
      const auto n = ngtcp2_conn_write_connection_close(
          self->conn_, &ps.path, &pi, packet.data(), packet.size(), &error, timestamp_ns());
      if (n > 0) send_gso(self->socket_.native_handle(), self->remote_ep_, packet.data(), n, n);
    }
    self->handle_error();
  });
}

// ngtcp2 static callbacks (client)
int QuicNgtcp2ClientConn::cb_recv_crypto_data(ngtcp2_conn* conn,
                                               ngtcp2_encryption_level level,
                                               uint64_t offset,
                                               const uint8_t* data,
                                               size_t datalen, void*)
{
  return ngtcp2_crypto_recv_crypto_data_cb(conn, level, offset, data, datalen,
                                            nullptr);
}

int QuicNgtcp2ClientConn::cb_handshake_completed(ngtcp2_conn*, void* ud)
{
  static_cast<QuicNgtcp2ClientConn*>(ud)->on_handshake_complete();
  return 0;
}

int QuicNgtcp2ClientConn::cb_recv_stream_data(ngtcp2_conn*, uint32_t,
                                               int64_t stream_id, uint64_t,
                                               const uint8_t* data,
                                               size_t datalen,
                                               void* ud, void*)
{
  static_cast<QuicNgtcp2ClientConn*>(ud)->on_stream_data(stream_id, data, datalen);
  return 0;
}

int QuicNgtcp2ClientConn::cb_acked_stream_data_offset(ngtcp2_conn*,
                                                       int64_t stream_id,
                                                       uint64_t offset,
                                                       uint64_t datalen,
                                                       void* ud,
                                                       void*)
{
  static_cast<QuicNgtcp2ClientConn*>(ud)->on_stream_data_acked(
      stream_id, offset, datalen);
  return 0;
}

int QuicNgtcp2ClientConn::cb_stream_open(ngtcp2_conn*, int64_t, void*)
{
  return 0; // client doesn't accept server-opened streams currently
}

int QuicNgtcp2ClientConn::cb_stream_close(ngtcp2_conn*, uint32_t,
                                           int64_t, uint64_t, void*, void*)
{
  return 0;
}

int QuicNgtcp2ClientConn::cb_get_new_connection_id(ngtcp2_conn*,
                                                    ngtcp2_cid* cid,
                                                    uint8_t* token,
                                                    size_t cidlen, void*)
{
  generate_cid(cid, cidlen);
  random_bytes(token, NGTCP2_STATELESS_RESET_TOKENLEN);
  return 0;
}

int QuicNgtcp2ClientConn::cb_remove_connection_id(ngtcp2_conn*,
                                                   const ngtcp2_cid*,
                                                   void*) { return 0; }

void QuicNgtcp2ClientConn::cb_rand(uint8_t* dst, size_t len,
                                    const ngtcp2_rand_ctx*)
{
  random_bytes(dst, len);
}

// ===========================================================================
// QuicNgtcp2ClientSession — Session wrapping QuicNgtcp2ClientConn
// ===========================================================================
class QuicNgtcp2ClientSession
    : public Session,
      public std::enable_shared_from_this<QuicNgtcp2ClientSession>
{
public:
  QuicNgtcp2ClientSession(const EndPoint& ep, boost::asio::io_context& ioc)
      : Session(ioc.get_executor())
      , conn_(std::make_shared<QuicNgtcp2ClientConn>(ioc, ep))
      , endpoint_(ep)
      , connected_(false)
  {
    ctx_.remote_endpoint = ep;
    // Route server-initiated Request frames (stream chunks) through handle_request
    conn_->set_request_dispatch([cell = self_cell_](flat_buffer& frame) {
      if (auto session = cell->lock()) {
        flat_buffer tx; // no reply expected for stream messages
        session->handle_request(frame, tx);
      }
    });
  }

  ~QuicNgtcp2ClientSession() override { conn_->close(); }
  void shutdown() override { conn_->close(); }

  void send_receive(flat_buffer& buf, uint32_t timeout_ms) override
  {
    if (!ensure_connected()) throw nprpc::ExceptionCommFailure();

    std::atomic<bool> done{false};
    boost::system::error_code result_ec;

    conn_->send_receive_async(
        std::move(buf),
        [&](const boost::system::error_code& ec, flat_buffer& resp) {
          result_ec = ec;
          if (!ec) buf = std::move(resp);
          done.store(true, std::memory_order_release);
          done.notify_one();
        },
        timeout_ms);

    done.wait(false);
    if (result_ec) throw nprpc::ExceptionCommFailure();
  }

  nprpc::Task<> send_receive_coro(flat_buffer& buf,
                                   uint32_t timeout_ms,
                                   std::stop_token /*st*/) override
  {
    if (!ensure_connected()) throw nprpc::ExceptionCommFailure();

    struct Awaiter {
      QuicNgtcp2ClientConn& conn;
      flat_buffer&          buf;
      uint32_t              timeout_ms;
      boost::system::error_code result_ec;

      bool await_ready() noexcept { return false; }

      void await_suspend(std::coroutine_handle<> h)
      {
        conn.send_receive_async(
            std::move(buf),
            [this, h](const boost::system::error_code& ec,
                      flat_buffer& resp) {
              result_ec = ec;
              if (!ec) buf = std::move(resp);
              h.resume();
            },
            timeout_ms);
      }

      void await_resume()
      {
        if (result_ec) throw nprpc::ExceptionCommFailure();
      }
    };

    co_await Awaiter{*conn_, buf, timeout_ms};
  }

  void send_receive_async(
      flat_buffer&&                          buf,
      std::optional<std::function<void(
          const boost::system::error_code&, flat_buffer&)>>&& handler,
      uint32_t timeout_ms) override
  {
    if (!ensure_connected()) {
      if (handler) {
        flat_buffer empty;
        (*handler)(boost::asio::error::connection_aborted, empty);
      }
      return;
    }
    conn_->send_receive_async(std::move(buf), std::move(handler), timeout_ms);
  }

  bool send_stream_message(flat_buffer&& buf) override
  {
    if (!conn_) return false;
    conn_->send_stream_message(std::move(buf));
    return true;
  }

  bool send_main_stream_message(flat_buffer&& buf) override
  {
    if (!conn_) return false;
    conn_->send_stream_message(std::move(buf));
    return true;
  }

  void timeout_action() override
  {
    // TODO: sweep timed-out pending_requests_ inside conn_
  }

private:
  bool ensure_connected()
  {
    if (connected_) return true;
    connected_ = conn_->connect();
    return connected_;
  }

  std::shared_ptr<QuicNgtcp2ClientConn> conn_;
  EndPoint                              endpoint_;
  bool                                  connected_;
};

// ===========================================================================
// Global listener instance
// ===========================================================================
static std::shared_ptr<QuicNgtcp2Listener> g_quic_listener;

// ===========================================================================
// Public API
// ===========================================================================

NPRPC_API void init_quic(boost::asio::io_context& ioc)
{
  if (g_cfg.listen_quic_port == 0) return;

  try {
    g_quic_listener = std::make_shared<QuicNgtcp2Listener>(
        ioc,
        g_cfg.quic_cert_file,
        g_cfg.quic_key_file,
        static_cast<uint16_t>(g_cfg.listen_quic_port));

    if (!g_quic_listener->start()) {
      NPRPC_QUIC_ERROR("failed to start QUIC listener");
      g_quic_listener.reset();
    }
  } catch (const std::exception& e) {
    NPRPC_QUIC_ERROR("init_quic exception: {}", e.what());
  }
}

NPRPC_API bool reload_quic_certificates()
{
  return !g_quic_listener || g_quic_listener->reload_certificates();
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
  auto session = std::make_shared<QuicNgtcp2ClientSession>(endpoint, ioc);
  session->bind_self(session);
  return session;
}

} // namespace nprpc::impl

#endif // NPRPC_QUIC_ENABLED

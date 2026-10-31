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
#include <condition_variable>
#include <future>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>
#include <string>
#include <map>
#include <unordered_map>
#include <unordered_set>
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
static constexpr int kMaxRecvBatch = 64;
// Room for the short header, the AEAD tag and the DATAGRAM frame in one packet.
static constexpr size_t kMaxDatagramSize = kMaxUdpPayload - 64;
static constexpr size_t kDatagramFrameOverhead = 9;
static constexpr uint64_t kMaxDatagramFrameSize = 65535;
static constexpr size_t kMaxQueuedDatagrams = 1024;
// Server-opened data streams a client allows at once.  One more than this
// open at the same time goes over the main stream instead.
static constexpr uint64_t kMaxDataStreams = 100;

static QuicTransportStats g_quic_stats;
static constexpr uint32_t kDefaultClientTimeoutMs = 30000;
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

// What it takes to carry frames as QUIC DATAGRAMs, on either side of a connection.
class DatagramQueue
{
public:
  // Called from any thread.  False means the datagram did not go out: too large
  // for one packet, unsupported by the peer, or too many already waiting.
  bool can_accept(size_t size) noexcept
  {
    if (size > max_size_.load(std::memory_order_acquire)) return false;
    if (queued_.fetch_add(1, std::memory_order_relaxed) >= kMaxQueuedDatagrams) {
      queued_.fetch_sub(1, std::memory_order_relaxed);
      return false;
    }
    return true;
  }

  // From here on, on the connection's strand.
  void push(flat_buffer&& frame) { frames_.push_back(std::move(frame)); }
  bool empty() const noexcept { return frames_.empty(); }

  void on_handshake_completed(ngtcp2_conn* conn) noexcept
  {
    const auto* params = ngtcp2_conn_get_remote_transport_params(conn);
    const uint64_t limit = params ? params->max_datagram_frame_size : 0;
    // A limit of zero is the peer saying it takes no datagrams.
    max_size_.store(limit > kDatagramFrameOverhead
        ? std::min<size_t>(kMaxDatagramSize, limit - kDatagramFrameOverhead) : 0,
        std::memory_order_release);
  }

  ngtcp2_ssize write(ngtcp2_conn* conn, ngtcp2_path* path, ngtcp2_pkt_info* pi,
                     uint8_t* dest, size_t destlen, ngtcp2_tstamp ts)
  {
    auto& frame = frames_.front();
    ngtcp2_vec vec{const_cast<uint8_t*>(frame.data_ptr()), frame.size()};
    int accepted = 0;
    const auto nwrite = ngtcp2_conn_writev_datagram(
        conn, path, pi, dest, destlen, &accepted,
        NGTCP2_WRITE_DATAGRAM_FLAG_PADDING, 0, &vec, 1, ts);
    if (accepted) {
      g_quic_stats.datagrams_sent.fetch_add(1, std::memory_order_relaxed);
      pop();
    } else if (nwrite == NGTCP2_ERR_INVALID_ARGUMENT || nwrite == NGTCP2_ERR_INVALID_STATE) {
      NPRPC_QUIC_ERROR("dropping datagram of {} bytes: {}", frame.size(),
                       ngtcp2_strerror(static_cast<int>(nwrite)));
      pop();
      return ngtcp2_conn_write_pkt(conn, path, pi, dest, destlen, ts);
    }
    return nwrite;
  }

private:
  void pop()
  {
    frames_.pop_front();
    queued_.fetch_sub(1, std::memory_order_relaxed);
  }

  std::deque<flat_buffer> frames_;
  std::atomic<size_t> queued_{0};
  std::atomic<size_t> max_size_{0};
};

// A datagram is one whole NPRPC frame.  False for anything else, which is not
// worth failing a connection over: nothing here was promised to arrive anyway.
static bool copy_datagram_frame(const uint8_t* data, size_t len, flat_buffer& frame)
{
  if (len < sizeof(impl::flat::Header)) return false;
  uint32_t frame_len = 0;
  std::memcpy(&frame_len, data, sizeof(frame_len));
  if (frame_len != len) return false;
  auto mb = frame.prepare(len);
  std::memcpy(mb.data(), data, len);
  frame.commit(len);
  return true;
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
  void schedule_write();
  bool write_pending_ = false; // On strand_.

  // Thread-safe: post a write to the RPC stream
  void enqueue_response(flat_buffer buf);
  // Thread-safe: post a write to the stream's own QUIC stream.  `last` ends it.
  void enqueue_stream_message(uint64_t nprpc_stream_id, bool last, flat_buffer buf);
  // Thread-safe: the reader cancelled, so nothing still queued is wanted.
  void cancel_data_stream(uint64_t nprpc_stream_id);
  // Thread-safe.  False means the datagram did not go out.
  bool send_datagram(flat_buffer&& frame);

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
  void on_data_stream_closed(int64_t stream_id);
  void on_datagram(const uint8_t* data, size_t len);
  ngtcp2_ssize write_data_stream(ngtcp2_conn*, ngtcp2_path*, ngtcp2_pkt_info*,
                                 uint8_t*, size_t, ngtcp2_tstamp);
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
  static int  cb_recv_datagram(ngtcp2_conn*, uint32_t, const uint8_t*, size_t, void*);
  static int  cb_get_new_connection_id(ngtcp2_conn*, ngtcp2_cid*, uint8_t*, size_t, void*);
  static int  cb_remove_connection_id(ngtcp2_conn*, const ngtcp2_cid*, void*);
  static void cb_rand(uint8_t*, size_t, const ngtcp2_rand_ctx*);

  std::shared_ptr<QuicNgtcp2Listener>                    listener_;
  boost::asio::ip::udp::socket&                         socket_;
  boost::asio::ip::udp::endpoint                        local_ep_;
  boost::asio::ip::udp::endpoint                        remote_ep_;
  boost::asio::strand<boost::asio::io_context::executor_type> strand_;
  boost::asio::steady_timer                             timer_;
  uint64_t last_scheduled_expiry_ = UINT64_MAX;

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

  // One server-opened QUIC stream per NPRPC stream, so a slow stream holds up
  // neither the others nor RPC replies.
  struct DataStream {
    uint64_t nprpc_stream_id = 0;
    std::deque<flat_buffer> queue;
    uint64_t acked_offset = 0;
    uint64_t sent_offset = 0;
    bool last_queued = false; // Send FIN after what is queued.
    bool fin_sent = false;
  };
  std::map<int64_t, DataStream> data_streams_;
  // Next to write, for taking turns; written round the map from here.
  int64_t next_data_stream_ = -1;
  // A value of -1 is a stream that found no room for a QUIC stream of its own,
  // and stays on the main stream so that it stays in order.
  std::unordered_map<uint64_t, int64_t> quic_stream_of_;
  std::unordered_set<uint64_t> cancelled_streams_;

  DatagramQueue datagrams_;

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
    // Weak: the manager belongs to this session, which the connection owns.
    ctx_.stream_manager->set_send_datagram_callback(
        [weak = std::weak_ptr(conn)](flat_buffer&& frame) {
          auto conn = weak.lock();
          return conn && conn->send_datagram(std::move(frame));
        });
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
    // Every message that comes here starts with its stream ID.
    if (buf.size() < sizeof(impl::flat::Header) + sizeof(uint64_t)) return false;
    uint64_t stream_id = 0;
    std::memcpy(&stream_id, buf.data_ptr() + sizeof(impl::flat::Header), sizeof(stream_id));
    const auto kind = impl::flat::Header_Direct(buf, 0).msg_id();
    const bool last = kind == MessageId::StreamCompletion || kind == MessageId::StreamError;
    conn->enqueue_stream_message(stream_id, last, std::move(buf));
    return true;
  }
  bool send_datagram(flat_buffer&& buf) override
  {
    auto conn = conn_.lock();
    return conn && conn->send_datagram(std::move(buf));
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
    if (frame.size() >= sizeof(impl::flat::Header) + sizeof(uint64_t) &&
        impl::flat::Header_Direct(frame, 0).msg_id() == MessageId::StreamCancellation) {
      uint64_t stream_id = 0;
      std::memcpy(&stream_id, frame.data_ptr() + sizeof(impl::flat::Header), sizeof(stream_id));
      if (auto conn = conn_.lock()) conn->cancel_data_stream(stream_id);
    }
    flat_buffer tx;
    if (measured_dispatch(monitor::Transport::Quic, frame, tx,
                         [&] { return handle_request(frame, tx); }) && tx.size() > 0) {
      inject_request_id(tx, rid);
      if (auto conn = conn_.lock()) conn->enqueue_response(std::move(tx));
    }
  }

  // A datagram gets no reply, whatever the servant leaves behind.
  void dispatch_datagram(flat_buffer& frame)
  {
    flat_buffer tx;
    handle_request(frame, tx);
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
  cbs.recv_datagram = cb_recv_datagram;

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
  params.max_datagram_frame_size             = kMaxDatagramFrameSize;
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
        self->schedule_write();
      });
}

void QuicNgtcp2ServerConn::schedule_write()
{
  // Datagrams drained together share one write, and through it one timer
  // re-arm, instead of each paying for their own.
  if (write_pending_) return;
  write_pending_ = true;
  boost::asio::post(strand_, [self = shared_from_this()]() {
    self->write_pending_ = false;
    if (!self->closed_) self->do_write();
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

void QuicNgtcp2ServerConn::enqueue_stream_message(uint64_t nprpc_stream_id,
                                                  bool last,
                                                  flat_buffer buf)
{
  boost::asio::post(
      strand_,
      [self = shared_from_this(), nprpc_stream_id, last, b = std::move(buf)]() mutable {
        if (self->closed_) return;
        // A chunk already on its way when the cancel came.
        if (self->cancelled_streams_.contains(nprpc_stream_id)) return;

        auto it = self->quic_stream_of_.find(nprpc_stream_id);
        if (it == self->quic_stream_of_.end()) {
          int64_t quic_stream = -1;
          if (ngtcp2_conn_open_uni_stream(self->conn_, &quic_stream, nullptr) == 0) {
            self->data_streams_[quic_stream].nprpc_stream_id = nprpc_stream_id;
            g_quic_stats.data_streams_opened.fetch_add(1, std::memory_order_relaxed);
          } else {
            quic_stream = -1;
            g_quic_stats.data_stream_fallbacks.fetch_add(1, std::memory_order_relaxed);
          }
          it = self->quic_stream_of_.emplace(nprpc_stream_id, quic_stream).first;
        }

        if (it->second < 0) {
          self->write_queue_.emplace_back(std::move(b));
          if (last) self->quic_stream_of_.erase(it);
        } else {
          auto& stream = self->data_streams_.at(it->second);
          stream.queue.emplace_back(std::move(b));
          stream.last_queued = stream.last_queued || last;
        }
        self->do_write();
      });
}

void QuicNgtcp2ServerConn::cancel_data_stream(uint64_t nprpc_stream_id)
{
  boost::asio::post(strand_, [self = shared_from_this(), nprpc_stream_id]() {
    if (self->closed_) return;
    self->cancelled_streams_.insert(nprpc_stream_id);
    auto it = self->quic_stream_of_.find(nprpc_stream_id);
    if (it == self->quic_stream_of_.end()) return;
    if (it->second >= 0) {
      ngtcp2_conn_shutdown_stream_write(self->conn_, 0, it->second, 0);
      self->data_streams_.erase(it->second);
    }
    self->quic_stream_of_.erase(it);
    self->do_write();
  });
}

bool QuicNgtcp2ServerConn::send_datagram(flat_buffer&& frame)
{
  if (closed_ || !datagrams_.can_accept(frame.size())) return false;
  boost::asio::post(strand_, [self = shared_from_this(), f = std::move(frame)]() mutable {
    if (self->closed_) return;
    self->datagrams_.push(std::move(f));
    self->do_write();
  });
  return true;
}

ngtcp2_ssize QuicNgtcp2ServerConn::write_data_stream(ngtcp2_conn* conn,
                                                     ngtcp2_path* path,
                                                     ngtcp2_pkt_info* pi,
                                                     uint8_t* dest,
                                                     size_t destlen,
                                                     ngtcp2_tstamp ts)
{
  auto it = data_streams_.lower_bound(next_data_stream_);
  for (size_t tried = 0, count = data_streams_.size(); tried < count; ++tried) {
    if (it == data_streams_.end()) it = data_streams_.begin();
    auto& stream = it->second;
    const int64_t id = it->first;
    ++it;

    std::array<ngtcp2_vec, kMaxWriteVecs> vecs{};
    const auto vec_count = build_unsent_vecs(
        stream.queue, stream.acked_offset, stream.sent_offset, vecs);
    const bool fin = stream.last_queued && !stream.fin_sent && vec_count < vecs.size();
    if (vec_count == 0 && !fin) continue;

    uint32_t flags = NGTCP2_WRITE_STREAM_FLAG_PADDING;
    if (fin) flags |= NGTCP2_WRITE_STREAM_FLAG_FIN;
    ngtcp2_ssize ndatalen = -1;
    const auto nwrite = ngtcp2_conn_writev_stream(
        conn, path, pi, dest, destlen, &ndatalen, flags, id, vecs.data(), vec_count, ts);
    if (nwrite == NGTCP2_ERR_STREAM_DATA_BLOCKED) continue; // Others may have credit.
    if (nwrite == NGTCP2_ERR_STREAM_SHUT_WR || nwrite == NGTCP2_ERR_STREAM_NOT_FOUND) {
      on_data_stream_closed(id);
      it = data_streams_.upper_bound(id);
      continue;
    }

    next_data_stream_ = id + 1;
    if (nwrite > 0 && ndatalen >= 0) {
      stream.sent_offset += static_cast<uint64_t>(ndatalen);
      size_t total = 0;
      for (size_t i = 0; i < vec_count; ++i) total += vecs[i].len;
      if (fin && static_cast<size_t>(ndatalen) == total) stream.fin_sent = true;
    }
    return nwrite;
  }
  return ngtcp2_conn_write_pkt(conn, path, pi, dest, destlen, ts);
}

void QuicNgtcp2ServerConn::on_data_stream_closed(int64_t stream_id)
{
  auto it = data_streams_.find(stream_id);
  if (it == data_streams_.end()) return;
  quic_stream_of_.erase(it->second.nprpc_stream_id);
  data_streams_.erase(it);
}

void QuicNgtcp2ServerConn::on_datagram(const uint8_t* data, size_t len)
{
  g_quic_stats.datagrams_received.fetch_add(1, std::memory_order_relaxed);
  flat_buffer frame;
  if (!copy_datagram_frame(data, len, frame)) return;
  // A client's first message can be a datagram, ahead of its first stream data.
  maybe_create_session();
  session_->dispatch_datagram(frame);
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
    if (!self->datagrams_.empty())
      return self->datagrams_.write(conn, path, pi, dest, destlen, ts);
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
        return self->write_data_stream(conn, path, pi, dest, destlen, ts);
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
        return self->write_data_stream(conn, path, pi, dest, destlen, ts);
      }
      if (nwrite > 0 && ndatalen > 0) {
        self->tx_stream_sent_offset_ += static_cast<uint64_t>(ndatalen);
      }
      return nwrite;
    }
    return self->write_data_stream(conn, path, pi, dest, destlen, ts);
  };

  ngtcp2_path_storage ps;
  ngtcp2_path_storage_zero(&ps);
  ngtcp2_pkt_info pi{};
  size_t gso_size = 0;

  // The high-level form clamps to one pacing burst and updates the pacer
  // itself, with a single timestamp.  See Http3Connection::on_write().
  auto nwrite = ngtcp2_conn_write_aggregate_pkt(
      conn_, &ps.path, &pi,
      txbuf_.data(), txbuf_.size(), &gso_size,
      write_pkt_cb, timestamp_ns());

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
    last_scheduled_expiry_ = UINT64_MAX;
    boost::asio::post(strand_, [self = shared_from_this()]() {
      if (!self->closed_) { self->handle_expiry(); self->do_write(); }
    });
    return;
  }

  // Re-arming a timer is a syscall; called per write, most leave it unchanged.
  if (expiry == last_scheduled_expiry_) return;
  last_scheduled_expiry_ = expiry;

  timer_.expires_after(std::chrono::nanoseconds(expiry - now));
  timer_.async_wait(boost::asio::bind_executor(
      strand_,
      [self = shared_from_this()](boost::system::error_code ec) {
        if (ec) return;
        self->last_scheduled_expiry_ = UINT64_MAX;
        if (self->closed_) return;
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
  if (stream_id == rpc_stream_id_) {
    release_acked_prefix(write_queue_, tx_stream_acked_offset_, offset, datalen);
    return;
  }
  auto it = data_streams_.find(stream_id);
  if (it != data_streams_.end())
    release_acked_prefix(it->second.queue, it->second.acked_offset, offset, datalen);
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
  self->datagrams_.on_handshake_completed(self->conn_);
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
  } else {
    self->on_data_stream_closed(stream_id);
  }
  return 0;
}

int QuicNgtcp2ServerConn::cb_recv_datagram(ngtcp2_conn*, uint32_t,
                                           const uint8_t* data, size_t len, void* ud)
{
  static_cast<QuicNgtcp2ServerConn*>(ud)->on_datagram(data, len);
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
  socket_.non_blocking(true, ec);
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
        for (int i = 1; i < kMaxRecvBatch; ++i) {
          boost::system::error_code drain_ec;
          const size_t more = socket_.receive_from(
              boost::asio::buffer(recv_buf_), sender_ep_, 0, drain_ec);
          if (drain_ec) break;
          handle_packet(sender_ep_, recv_buf_.data(), more);
        }
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
// Client connections run here rather than on the runtime's io_context, which
// has no thread at all unless the application started a pool, and whose threads
// are free to block in a call waiting for this very transport.
class QuicClientIo
{
public:
  QuicClientIo()
      : work_(boost::asio::make_work_guard(ioc))
      , thread_([this] {
          thread_id_ = std::this_thread::get_id();
          set_thread_name("quic_client");
          ioc.run();
        })
  {
  }

  // Runs whatever is already queued, such as closing connections, then stops.
  // Connections keep this object alive past that, because their sockets and
  // timers belong to ioc.
  void stop()
  {
    if (stopped_.exchange(true)) return;
    boost::asio::post(ioc, [this] { ioc.stop(); });
    thread_.join();
    // What is still queued holds connections, which hold this object.
    work_.reset();
    ioc.restart();
    ioc.poll();
  }

  ~QuicClientIo() { stop(); }

  bool stopped() const noexcept { return stopped_.load(); }
  bool on_this_thread() const noexcept { return std::this_thread::get_id() == thread_id_.load(); }

  boost::asio::io_context ioc{1};

private:
  boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_;
  std::atomic<std::thread::id> thread_id_;
  std::atomic<bool> stopped_{false};
  std::thread thread_;
};

class QuicNgtcp2ClientConn
    : public std::enable_shared_from_this<QuicNgtcp2ClientConn>
{
public:
  enum class ConnectResult { Connected, Failed, TimedOut };

  using Handler = std::function<void(const boost::system::error_code&, flat_buffer&)>;

  QuicNgtcp2ClientConn(std::shared_ptr<QuicClientIo> io, const EndPoint& ep);
  ~QuicNgtcp2ClientConn();

  // Blocks the caller until the handshake ends or timeout_ms passes.
  // A connection gets one attempt: it is replaced afterwards, not reused.
  ConnectResult connect(const boost::asio::ip::udp::endpoint& remote, uint32_t timeout_ms);
  bool connect_attempted() const noexcept { return connect_attempted_; }
  void close();
  bool is_connected() const { return connected_.load(); }
  bool is_closed() const { return closed_.load(); }

  void send_receive_async(flat_buffer&&               buf,
                          std::optional<Handler>&&    handler,
                          uint32_t                    timeout_ms);
  void send_stream_message(flat_buffer&& buf);
  // False means the datagram did not go out.
  bool send_datagram(flat_buffer&& frame);

  // Register a handler for server-initiated Request frames (stream chunks)
  void set_request_dispatch(std::function<void(flat_buffer&)> fn)
  {
    request_dispatch_ = std::move(fn);
  }

private:
  void do_connect(boost::asio::ip::udp::endpoint      remote_ep,
                  std::move_only_function<void(bool)> on_done);
  void do_receive();
  bool read_udp_packet(const uint8_t* data, size_t len);
  boost::asio::ip::udp::endpoint local_ep_;
  void do_write();
  void schedule_timer();
  void handle_expiry();
  void handle_error();
  void on_stream_data(int64_t stream_id, const uint8_t* data, size_t len);
  void on_stream_data_acked(int64_t stream_id, uint64_t offset, uint64_t datalen);
  void on_handshake_complete();
  bool setup_ssl_ctx();
  void arm_request_timer(std::chrono::steady_clock::time_point deadline);
  void fail_expired_requests();

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
  static int  cb_recv_datagram(ngtcp2_conn*, uint32_t, const uint8_t*, size_t, void*);
  static int  cb_get_new_connection_id(ngtcp2_conn*, ngtcp2_cid*, uint8_t*, size_t, void*);
  static int  cb_remove_connection_id(ngtcp2_conn*, const ngtcp2_cid*, void*);
  static void cb_rand(uint8_t*, size_t, const ngtcp2_rand_ctx*);

  // First, so it is destroyed last: everything below belongs to its io_context.
  std::shared_ptr<QuicClientIo> io_;
  EndPoint                 endpoint_;
  boost::asio::ip::udp::socket socket_;
  boost::asio::ip::udp::endpoint remote_ep_;
  boost::asio::strand<boost::asio::io_context::executor_type> strand_;
  boost::asio::steady_timer timer_;
  uint64_t last_scheduled_expiry_ = UINT64_MAX;
  boost::asio::steady_timer request_timer_;
  std::optional<std::chrono::steady_clock::time_point> request_timer_deadline_;

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
  // Server-opened streams, each carrying one NPRPC stream.
  std::unordered_map<int64_t, flat_buffer> data_stream_rx_;
  DatagramQueue datagrams_;
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
  std::atomic<bool> closed_{false};
  bool              connect_attempted_ = false;

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
QuicNgtcp2ClientConn::QuicNgtcp2ClientConn(std::shared_ptr<QuicClientIo> io,
                                            const EndPoint& ep)
    : io_(std::move(io))
    , endpoint_(ep)
    , socket_(io_->ioc)
    , strand_(boost::asio::make_strand(io_->ioc))
    , timer_(io_->ioc)
    , request_timer_(io_->ioc)
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

QuicNgtcp2ClientConn::ConnectResult QuicNgtcp2ClientConn::connect(
    const boost::asio::ip::udp::endpoint& remote, uint32_t timeout_ms)
{
  if (connect_attempted_ || io_->stopped()) return ConnectResult::Failed;
  connect_attempted_ = true;

  if (!setup_ssl_ctx()) {
    NPRPC_QUIC_ERROR("client SSL context setup failed");
    return ConnectResult::Failed;
  }

  remote_ep_ = remote;

  std::promise<bool> promise;
  auto future = promise.get_future();
  boost::asio::post(
      strand_,
      [self = shared_from_this(), remote_ep = remote_ep_, p = std::move(promise)]() mutable {
        self->do_connect(remote_ep, [p = std::move(p)](bool ok) mutable {
          p.set_value(ok);
        });
      });

  if (future.wait_for(std::chrono::milliseconds(timeout_ms)) != std::future_status::ready) {
    close();
    return ConnectResult::TimedOut;
  }
  return future.get() ? ConnectResult::Connected : ConnectResult::Failed;
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

  socket_.non_blocking(true, ec);
  local_ep_ = socket_.local_endpoint();
  auto local_ep = local_ep_;

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
  cbs.recv_datagram = cb_recv_datagram;

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
  params.initial_max_stream_data_uni         = 16 * 1024 * 1024;
  params.initial_max_data                    = 32 * 1024 * 1024;
  params.max_ack_delay                       = 1 * NGTCP2_MILLISECONDS;
  params.initial_max_streams_bidi            = 0; // server doesn't open streams
  params.initial_max_streams_uni             = kMaxDataStreams;
  params.max_idle_timeout                    = 30 * NGTCP2_SECONDS;
  params.active_connection_id_limit          = 7;
  params.max_datagram_frame_size             = kMaxDatagramFrameSize;

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
            if (self->closed_ || ec == boost::asio::error::operation_aborted) return;
            if (ec) {
              // Nothing would read this socket again, so nothing would ever
              // complete what is waiting on it.
              NPRPC_QUIC_ERROR("client recv: {}", ec.message());
              self->handle_error();
              return;
            }
            // Drain what is queued before writing: one wake-up per datagram
            // cannot keep up with a sender, and neither can an ACK attempt
            // per datagram.
            bool ok = self->read_udp_packet(self->recv_buf_.data(), n);
            for (int i = 1; ok && i < kMaxRecvBatch; ++i) {
              boost::system::error_code drain_ec;
              const size_t more = self->socket_.receive_from(
                  boost::asio::buffer(self->recv_buf_), self->remote_ep_, 0, drain_ec);
              if (drain_ec) break;
              ok = self->read_udp_packet(self->recv_buf_.data(), more);
            }
            if (!ok || self->closed_) return;
            self->schedule_timer();
            self->do_write();
            self->do_receive();
          }));
}

bool QuicNgtcp2ClientConn::read_udp_packet(const uint8_t* data, size_t len)
{
  if (closed_ || !conn_) return false;

  sockaddr_storage lss, rss;
  auto path = make_path(local_ep_, remote_ep_, lss, rss);
  ngtcp2_pkt_info pi{};

  int rv = ngtcp2_conn_read_pkt(conn_, &path, &pi, data, len, timestamp_ns());
  if (rv != 0) {
    switch (rv) {
    case NGTCP2_ERR_DRAINING:
    case NGTCP2_ERR_DROP_CONN:
      handle_error();
      return false;
    default:
      NPRPC_QUIC_ERROR("client read_pkt: {}", ngtcp2_strerror(rv));
      if (!last_error_.error_code)
        ngtcp2_ccerr_set_liberr(&last_error_, rv, nullptr, 0);
      handle_error();
      return false;
    }
  }
  return true;
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
    if (!self->datagrams_.empty())
      return self->datagrams_.write(conn, path, pi, dest, destlen, ts);
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

  // The high-level form clamps to one pacing burst and updates the pacer
  // itself, with a single timestamp.  See Http3Connection::on_write().
  auto nwrite = ngtcp2_conn_write_aggregate_pkt(
      conn_, &ps.path, &pi,
      txbuf_.data(), txbuf_.size(), &gso_size,
      write_pkt_cb, timestamp_ns());

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
    last_scheduled_expiry_ = UINT64_MAX;
    boost::asio::post(strand_, [self = shared_from_this()]() {
      if (!self->closed_) { self->handle_expiry(); self->do_write(); }
    });
    return;
  }
  // Re-arming a timer is a syscall; called per write, most leave it unchanged.
  if (expiry == last_scheduled_expiry_) return;
  last_scheduled_expiry_ = expiry;

  timer_.expires_after(std::chrono::nanoseconds(expiry - now));
  timer_.async_wait(boost::asio::bind_executor(
      strand_,
      [self = shared_from_this()](boost::system::error_code ec) {
        if (ec) return;
        self->last_scheduled_expiry_ = UINT64_MAX;
        if (self->closed_) return;
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
  request_timer_.cancel();
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

  for (auto& queued : pre_connect_queue_) {
    if (queued.handler) {
      flat_buffer empty;
      (*queued.handler)(ec, empty);
    }
  }
  pre_connect_queue_.clear();
}

void QuicNgtcp2ClientConn::arm_request_timer(std::chrono::steady_clock::time_point deadline)
{
  // One timer for all requests.  Most deadlines fall after the one already
  // armed, and most calls finish first, so this rarely costs a syscall.
  if (request_timer_deadline_ && *request_timer_deadline_ <= deadline) return;
  request_timer_deadline_ = deadline;
  request_timer_.expires_at(deadline);
  request_timer_.async_wait(boost::asio::bind_executor(
      strand_,
      [self = shared_from_this()](boost::system::error_code ec) {
        if (ec) return; // Re-armed, and a newer wait owns the deadline; or closed.
        self->request_timer_deadline_.reset();
        if (!self->closed_) self->fail_expired_requests();
      }));
}

void QuicNgtcp2ClientConn::fail_expired_requests()
{
  const auto now = std::chrono::steady_clock::now();
  std::optional<std::chrono::steady_clock::time_point> next;
  for (auto it = pending_requests_.begin(); it != pending_requests_.end();) {
    if (it->second.timeout_point > now) {
      if (!next || it->second.timeout_point < *next) next = it->second.timeout_point;
      ++it;
      continue;
    }
    auto handler = std::move(it->second.handler);
    it = pending_requests_.erase(it);
    if (handler) {
      flat_buffer empty;
      (*handler)(boost::asio::error::timed_out, empty);
    }
  }
  if (next) arm_request_timer(*next);
}

void QuicNgtcp2ClientConn::on_stream_data(int64_t stream_id,
                                           const uint8_t* data, size_t len)
{
  if (stream_id != rpc_stream_id_ && ngtcp2_is_bidi_stream(stream_id)) {
    if (conn_) {
      ngtcp2_conn_extend_max_stream_offset(conn_, stream_id, len);
      ngtcp2_conn_extend_max_offset(conn_, len);
    }
    return;
  }

  auto& rx_buf_ = stream_id == rpc_stream_id_ ? this->rx_buf_ : data_stream_rx_[stream_id];
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
  datagrams_.on_handshake_completed(conn_);

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
                  + std::chrono::milliseconds(timeout_ms ? timeout_ms : kDefaultClientTimeoutMs);
        self->pending_requests_.emplace(rid, PendingRequest{std::move(h), tp});
        self->arm_request_timer(tp);
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

bool QuicNgtcp2ClientConn::send_datagram(flat_buffer&& frame)
{
  if (closed_ || !datagrams_.can_accept(frame.size())) return false;
  boost::asio::post(strand_, [self = shared_from_this(), f = std::move(frame)]() mutable {
    if (self->closed_) return;
    self->datagrams_.push(std::move(f));
    self->do_write();
  });
  return true;
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
  return 0;
}

int QuicNgtcp2ClientConn::cb_stream_close(ngtcp2_conn* conn, uint32_t,
                                           int64_t stream_id, uint64_t, void* ud, void*)
{
  if (!ngtcp2_is_bidi_stream(stream_id)) {
    static_cast<QuicNgtcp2ClientConn*>(ud)->data_stream_rx_.erase(stream_id);
    // Gives the server back the stream it just finished with.
    ngtcp2_conn_extend_max_streams_uni(conn, 1);
  }
  return 0;
}

int QuicNgtcp2ClientConn::cb_recv_datagram(ngtcp2_conn*, uint32_t,
                                           const uint8_t* data, size_t len, void* ud)
{
  auto* self = static_cast<QuicNgtcp2ClientConn*>(ud);
  g_quic_stats.datagrams_received.fetch_add(1, std::memory_order_relaxed);
  flat_buffer frame;
  if (copy_datagram_frame(data, len, frame) && self->request_dispatch_)
    self->request_dispatch_(frame);
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
  QuicNgtcp2ClientSession(const EndPoint& ep,
                          boost::asio::io_context& ioc,
                          std::shared_ptr<QuicClientIo> io)
      : Session(ioc.get_executor())
      , io_(std::move(io))
      , endpoint_(ep)
  {
    ctx_.remote_endpoint = ep;
    conn_ = make_conn();
  }

  ~QuicNgtcp2ClientSession() override { current_conn()->close(); }
  void shutdown() override { current_conn()->close(); }

  void send_receive(flat_buffer& buf, uint32_t timeout_ms) override
  {
    // The thread that would deliver the reply cannot also wait for it.
    if (io_->on_this_thread()) {
      NPRPC_QUIC_ERROR("blocking call from a QUIC client transport callback");
      throw nprpc::ExceptionCommFailure();
    }
    auto conn = connected_conn(timeout_ms);

    // Shared, because a reply can still arrive after this call has given up.
    struct Waiter {
      std::mutex mutex;
      std::condition_variable cv;
      bool done = false;
      boost::system::error_code ec;
      flat_buffer response;
    };
    auto waiter = std::make_shared<Waiter>();

    conn->send_receive_async(
        std::move(buf),
        [waiter](const boost::system::error_code& ec, flat_buffer& resp) {
          std::lock_guard lock(waiter->mutex);
          waiter->ec = ec;
          if (!ec) waiter->response = std::move(resp);
          waiter->done = true;
          waiter->cv.notify_one();
        },
        timeout_ms);

    // The connection's own timer normally answers first; this covers a client
    // thread that is stuck or has already stopped.
    std::unique_lock lock(waiter->mutex);
    const auto limit = std::chrono::milliseconds(effective_timeout(timeout_ms)) + kWaitGrace;
    if (!waiter->cv.wait_for(lock, limit, [&] { return waiter->done; }))
      throw nprpc::ExceptionTimeout();
    if (waiter->ec == boost::asio::error::timed_out) throw nprpc::ExceptionTimeout();
    if (waiter->ec) throw nprpc::ExceptionCommFailure();
    buf = std::move(waiter->response);
  }

  nprpc::Task<> send_receive_coro(flat_buffer& buf,
                                   uint32_t timeout_ms,
                                   std::stop_token /*st*/) override
  {
    auto conn = connected_conn(timeout_ms);

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
        if (result_ec == boost::asio::error::timed_out) throw nprpc::ExceptionTimeout();
        if (result_ec) throw nprpc::ExceptionCommFailure();
      }
    };

    co_await Awaiter{*conn, buf, timeout_ms};
  }

  void send_receive_async(
      flat_buffer&&                          buf,
      std::optional<std::function<void(
          const boost::system::error_code&, flat_buffer&)>>&& handler,
      uint32_t timeout_ms) override
  {
    std::shared_ptr<QuicNgtcp2ClientConn> conn;
    boost::system::error_code ec;
    try {
      conn = connected_conn(timeout_ms);
    } catch (const nprpc::ExceptionTimeout&) {
      ec = boost::asio::error::timed_out;
    } catch (const nprpc::Exception&) {
      ec = boost::asio::error::connection_aborted;
    }
    if (!conn) {
      if (handler) {
        flat_buffer empty;
        (*handler)(ec, empty);
      }
      return;
    }
    conn->send_receive_async(std::move(buf), std::move(handler), timeout_ms);
  }

  bool send_stream_message(flat_buffer&& buf) override
  {
    auto conn = current_conn();
    if (conn->is_closed()) return false;
    conn->send_stream_message(std::move(buf));
    return true;
  }

  bool send_main_stream_message(flat_buffer&& buf) override
  {
    return send_stream_message(std::move(buf));
  }

  bool send_datagram(flat_buffer&& buf) override
  {
    try {
      return connected_conn(0)->send_datagram(std::move(buf));
    } catch (const nprpc::Exception&) {
      return false;
    }
  }

  // QuicNgtcp2ClientConn expires its own requests.
  void timeout_action() override {}

private:
  static constexpr auto kWaitGrace = std::chrono::seconds(1);

  static uint32_t effective_timeout(uint32_t timeout_ms) noexcept
  {
    return timeout_ms ? timeout_ms : kDefaultClientTimeoutMs;
  }

  std::shared_ptr<QuicNgtcp2ClientConn> make_conn()
  {
    auto conn = std::make_shared<QuicNgtcp2ClientConn>(io_, endpoint_);
    // Route server-initiated Request frames (stream chunks) through handle_request
    conn->set_request_dispatch([cell = self_cell_](flat_buffer& frame) {
      if (auto session = cell->lock()) {
        flat_buffer tx; // no reply expected for stream messages
        session->handle_request(frame, tx);
      }
    });
    return conn;
  }

  std::shared_ptr<QuicNgtcp2ClientConn> current_conn()
  {
    std::lock_guard lock(conn_mutex_);
    return conn_;
  }

  // Throws rather than returning an unusable connection.  A closed connection
  // is replaced, so a timeout or a lost peer does not fail this session for good.
  std::shared_ptr<QuicNgtcp2ClientConn> connected_conn(uint32_t timeout_ms)
  {
    std::lock_guard lock(conn_mutex_);
    if (conn_->is_connected()) return conn_;
    if (io_->on_this_thread()) throw nprpc::ExceptionCommFailure();

    using Clock = std::chrono::steady_clock;
    const auto deadline = Clock::now() + std::chrono::milliseconds(effective_timeout(timeout_ms));
    const auto remaining_ms = [&] {
      return std::max<int64_t>(0, std::chrono::duration_cast<std::chrono::milliseconds>(
          deadline - Clock::now()).count());
    };

    const auto addresses = resolve(static_cast<uint32_t>(remaining_ms()));

    // UDP reports nothing about an address that does not answer, so each one
    // gets a fair share of the time left rather than all of it.
    bool timed_out = false;
    for (size_t i = 0; i < addresses.size(); ++i) {
      if (conn_->connect_attempted() || conn_->is_closed()) conn_ = make_conn();
      const auto share = remaining_ms() / static_cast<int64_t>(addresses.size() - i);
      switch (conn_->connect(addresses[i], static_cast<uint32_t>(share))) {
      case QuicNgtcp2ClientConn::ConnectResult::Connected: return conn_;
      case QuicNgtcp2ClientConn::ConnectResult::TimedOut: timed_out = true; break;
      case QuicNgtcp2ClientConn::ConnectResult::Failed: break;
      }
    }
    if (timed_out) throw nprpc::ExceptionTimeout();
    throw nprpc::ExceptionCommFailure();
  }

  // Runs on the client thread, because getaddrinfo itself takes no timeout.
  std::vector<boost::asio::ip::udp::endpoint> resolve(uint32_t timeout_ms)
  {
    using Resolver = boost::asio::ip::udp::resolver;
    using Addresses = std::vector<boost::asio::ip::udp::endpoint>;
    auto resolver = std::make_shared<Resolver>(io_->ioc);
    std::promise<Addresses> promise;
    auto future = promise.get_future();
    resolver->async_resolve(
        std::string(endpoint_.hostname()), std::to_string(endpoint_.port()),
        Resolver::flags{},
        [resolver, p = std::move(promise)](const boost::system::error_code& ec,
                                           Resolver::results_type results) mutable {
          Addresses addresses;
          if (ec) {
            NPRPC_QUIC_ERROR("cannot resolve QUIC host: {}", ec.message());
          } else {
            for (const auto& entry : results) addresses.push_back(entry.endpoint());
          }
          p.set_value(std::move(addresses));
        });

    if (future.wait_for(std::chrono::milliseconds(timeout_ms)) != std::future_status::ready) {
      boost::asio::post(io_->ioc, [resolver] { resolver->cancel(); });
      throw nprpc::ExceptionTimeout();
    }
    auto addresses = future.get();
    if (addresses.empty()) throw nprpc::ExceptionCommFailure();
    return addresses;
  }

  std::shared_ptr<QuicClientIo>         io_;
  EndPoint                              endpoint_;
  std::mutex                            conn_mutex_;
  std::shared_ptr<QuicNgtcp2ClientConn> conn_;
};

// ===========================================================================
// Global listener instance
// ===========================================================================
static std::shared_ptr<QuicNgtcp2Listener> g_quic_listener;

static std::mutex g_quic_client_io_mutex;
static std::shared_ptr<QuicClientIo> g_quic_client_io;

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

NPRPC_API const QuicTransportStats& quic_transport_stats() noexcept
{
  return g_quic_stats;
}

NPRPC_API void stop_quic_client()
{
  std::shared_ptr<QuicClientIo> io;
  {
    std::lock_guard lock(g_quic_client_io_mutex);
    io = std::move(g_quic_client_io);
    g_quic_client_io.reset();
  }
  if (io) io->stop();
}

NPRPC_API std::shared_ptr<Session>
make_quic_client_session(const EndPoint& endpoint, boost::asio::io_context& ioc)
{
  std::shared_ptr<QuicClientIo> io;
  {
    std::lock_guard lock(g_quic_client_io_mutex);
    if (!g_quic_client_io) g_quic_client_io = std::make_shared<QuicClientIo>();
    io = g_quic_client_io;
  }
  auto session = std::make_shared<QuicNgtcp2ClientSession>(endpoint, ioc, std::move(io));
  session->bind_self(session);
  return session;
}

} // namespace nprpc::impl

#endif // NPRPC_QUIC_ENABLED

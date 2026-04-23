// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include <nprpc/impl/http3_server.hpp>

// This file implements HTTP/3 and WebTransport using ngtcp2 + nghttp3 backend
#if defined(NPRPC_HTTP3_ENABLED) && defined(NPRPC_HTTP3_BACKEND_NGHTTP3)

#include <nprpc/impl/http_file_cache.hpp>
#include <nprpc/impl/http_request_throttler.hpp>
#include <nprpc/impl/http_rpc_session.hpp>
#include <nprpc/impl/http_utils.hpp>
#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/impl/misc/thread_identity.hpp>
#ifdef NPRPC_SSR_ENABLED
#include <nprpc/impl/ssr_manager.hpp>
#endif
#include <nprpc/common.hpp>

#include <nghttp3/nghttp3.h>
#include <ngtcp2/ngtcp2.h>
#include <ngtcp2/ngtcp2_crypto.h>
#if defined(OPENSSL_IS_BORINGSSL)
# include <ngtcp2/ngtcp2_crypto_boringssl.h>
#else
# include <ngtcp2/ngtcp2_crypto_ossl.h>
#endif

#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>

#include <boost/asio.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/steady_timer.hpp>

#if !defined(_WIN32)
# include <errno.h>
# if defined(NPRPC_HTTP3_REUSEPORT_BPF_ENABLED)
#  include <bpf/bpf.h>
#  include <bpf/libbpf.h>
# endif
# include <netinet/in.h>
# include <netinet/udp.h>
# include <sys/socket.h>
# include <sys/uio.h>
#endif

#include "../logging.hpp"
#include <nprpc/impl/lock_free_ring_buffer.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <deque>
#include <format>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <map>
#include <memory_resource>
#include <span>
#include <thread>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <ankerl/unordered_dense.h>

//==============================================================================
// Configuration macros
//==============================================================================

#define NPRPC_ENABLE_HTTP3_TRACE 0
#define NPRPC_ENABLE_HTTP3_RESPONSE_DEBUG 0
#define NPRPC_NGTCP2_ENABLE_LOGGING 0
#define NPRPC_ENABLE_HTTP3_REUSEPORT_SANITY 0
#define NPRPC_ENABLE_HTTP3_MONOTONIC_TIMESTAMP_WORKAROUND 1

//==============================================================================
// Logging macros
//==============================================================================

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

#if NPRPC_ENABLE_HTTP3_RESPONSE_DEBUG
# define NPRPC_HTTP3_DEBUG(format_string, ...)               \
  NPRPC_LOG_INFO(                                            \
    "[HTTP/3][DBG] " format_string __VA_OPT__(, ) __VA_ARGS__);
#else
# define NPRPC_HTTP3_DEBUG(format_string, ...) do {} while (0)
#endif

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
static constexpr uint8_t MAX_HTTP3_EMBEDDED_WORKER_ID = 160;

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

namespace {

constexpr std::size_t kHttp3ResponseChunkSize = 64 * 1024;
constexpr std::size_t SEND_PACKET_SLAB_SIZE = 64;

#if !defined(_WIN32) && defined(UDP_SEGMENT)
constexpr std::size_t kMaxUdpGsoSegments = MAX_PKTS_BURST;
#endif

// SHM egress frame header (matches ShmEgressFrame in quic_shm_channel.hpp).
// npquicrouter reads these and calls sendmsg(GSO) preserving kernel batching.
#pragma pack(push, 1)
struct ShmEgressFrame {
  uint32_t payload_len;
  uint16_t gso_segment_size;
  uint8_t  ep_len;
  uint8_t  _pad;
  uint8_t  ep_storage[28];
};
#pragma pack(pop)

static_assert(sizeof(ShmEgressFrame) == 36,
              "ShmEgressFrame size mismatch — update quic_shm_channel.hpp");

// SHM ingress frame header (matches ShmIngressFrame in quic_shm_channel.hpp).
// npquicrouter writes the real client endpoint + raw datagram here.
// Http3Server reads these via ingress_loop() and calls handle_packet().
#pragma pack(push, 1)
struct ShmIngressFrame {
  uint32_t payload_len;
  uint8_t  ep_len;
  uint8_t  _pad[3];
  uint8_t  ep_storage[28];
};
#pragma pack(pop)

static_assert(sizeof(ShmIngressFrame) == 36,
              "ShmIngressFrame size mismatch — update quic_shm_channel.hpp");

} // namespace

size_t default_http3_worker_count() noexcept
{
  const auto hw = std::thread::hardware_concurrency();
  if (hw == 0) {
    return 1;
  }

  return std::min<size_t>(8, hw);
}

size_t effective_http3_worker_count() noexcept
{
  return g_cfg.http3_worker_count != 0 ? g_cfg.http3_worker_count
                                       : default_http3_worker_count();
}

#if defined(SO_REUSEPORT)
using reuse_port_socket_option =
    boost::asio::detail::socket_option::boolean<SOL_SOCKET, SO_REUSEPORT>;

bool enable_reuse_port(boost::asio::ip::udp::socket& socket,
                       boost::system::error_code& ec)
{
  socket.set_option(reuse_port_socket_option(true), ec);
  return !ec;
}
#endif

bool can_embed_http3_worker_id(size_t worker_count) noexcept
{
  return worker_count > 0 && worker_count - 1 <= MAX_HTTP3_EMBEDDED_WORKER_ID;
}

} // anonymous namespace

//==============================================================================
// Utility functions
//==============================================================================

namespace {

uint64_t timestamp_ns()
{
  auto now = std::chrono::steady_clock::now();
  auto now_ns = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          now.time_since_epoch())
          .count());

#if NPRPC_ENABLE_HTTP3_MONOTONIC_TIMESTAMP_WORKAROUND
  static std::atomic<uint64_t> last_ts{0};

  auto observed = last_ts.load(std::memory_order_relaxed);
  for (;;) {
    auto clamped = std::max(now_ns, observed);
    if (last_ts.compare_exchange_weak(observed, clamped,
                                      std::memory_order_relaxed,
                                      std::memory_order_relaxed)) {
      return clamped;
    }
  }
#else
  return now_ns;
#endif
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

void generate_server_connection_id(ngtcp2_cid* cid,
                                   size_t cidlen,
                                   uint8_t worker_id)
{
  cid->datalen = cidlen;
  random_bytes(cid->data, cidlen);

  if (cidlen != 0) {
    cid->data[0] = worker_id;
  }
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

std::string cid_to_hex(const ngtcp2_cid* cid)
{
  std::string hex;
  hex.reserve(cid->datalen * 2);
  for (size_t i = 0; i < cid->datalen; ++i) {
    hex += "0123456789abcdef"[cid->data[i] >> 4];
    hex += "0123456789abcdef"[cid->data[i] & 0xf];
  }
  return hex;
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
  // Fixed-size inline array — no heap allocation for response header vectors.
  // Max entries: 12 — CORS RPC (9) or static file + cache headers (7):
  //   status, server, content-type, content-length,
  //   cache-control, etag, last-modified,
  //   access-control-allow-origin, access-control-allow-credentials,
  //   vary, access-control-allow-methods, access-control-allow-headers.
  struct NvArray {
    std::array<nghttp3_nv, 12> storage{};
    size_t count = 0;
    void clear() noexcept { count = 0; }
    void reserve(size_t) noexcept {}
    void push_back(nghttp3_nv nv) noexcept { storage[count++] = nv; }
    nghttp3_nv* data() noexcept { return storage.data(); }
    const nghttp3_nv* data() const noexcept { return storage.data(); }
    size_t size() const noexcept { return count; }
  } headers;
  std::string status;
  std::string content_length;
  std::string content_type;
  std::string allow_origin;
};

struct PendingSendPacketPayload {
  std::array<uint8_t, MAX_UDP_PAYLOAD_SIZE> bytes;
};

struct PendingSendPacket {
  boost::asio::ip::udp::endpoint remote_ep;
  uint8_t* payload = nullptr;
  std::size_t payload_len = 0;
  std::uint16_t gso_segment_size = 0;
  PendingSendPacket* next_free = nullptr;
  PendingSendPacket* next_send = nullptr;

  uint8_t* payload_data() noexcept { return payload; }
  const uint8_t* payload_data() const noexcept { return payload; }
  static constexpr std::size_t payload_capacity() noexcept
  {
    return MAX_UDP_PAYLOAD_SIZE;
  }

  std::span<const uint8_t> bytes() const noexcept
  {
    return {payload, payload_len};
  }
};

struct Http3Stream {
  // Per-request PMR arena. 4 KB covers typical request headers
  // (≤20 headers × ~64 B/pair + long strings like path/authority) with zero
  // heap traffic. Overflow falls back to the system heap silently (the default
  // upstream is new_delete_resource). In debug builds the upstream is wired to
  // null_memory_resource() so any overflow throws std::bad_alloc immediately.
  alignas(std::max_align_t) std::byte arena_[4096];
#if !defined(NDEBUG)
  std::pmr::monotonic_buffer_resource mr_{arena_, sizeof(arena_),
                                          std::pmr::null_memory_resource()};
#else
  std::pmr::monotonic_buffer_resource mr_{arena_, sizeof(arena_)};
#endif
  std::pmr::polymorphic_allocator<> alloc_{&mr_};

  int64_t stream_id = -1;
  std::pmr::string method{alloc_};
  std::pmr::string path{alloc_};
  std::pmr::string authority{alloc_};
  std::pmr::string scheme{alloc_};
  std::pmr::string protocol{alloc_};
  std::pmr::string content_type{alloc_};
  std::pmr::string accept{alloc_};
  // Flat vector — O(1) amortised insertion, linear scan is faster than tree
  // traversal for ≤~20 headers due to cache locality.
  std::pmr::vector<std::pair<std::pmr::string, std::pmr::string>> headers{alloc_};
  size_t content_length = 0;
  bool malformed_content_length = false;
  flat_buffer request_body;
  bool request_body_preallocated = false;
  bool request_body_too_large = false;

  bool http_stream = false;
  bool webtransport_session = false;
  bool webtransport_child_stream = false;
  bool webtransport_rejected = false;
  WebTransportChildBinding webtransport_child_binding =
      WebTransportChildBinding::Unbound;
  bool response_started = false;
  int64_t webtransport_session_id = -1;
  uint64_t webtransport_native_stream_id = 0;
  flat_buffer webtransport_probe_buffer;
  std::deque<flat_buffer> raw_write_queue;
  bool raw_write_scheduled = false;
  bool wt_data_stream_opened = false; // nghttp3 WT data stream write registered
  bool wt_write_fin_pending = false; // EOF should be sent after all data is drained
  size_t wt_write_offset = 0; // offset within front chunk already provided

  // Response data - kept alive for async sending
  // For cached files: cached_file keeps the data alive (zero-copy)
  // For static responses: response_data points to body data
  // For dynamic responses: use a string to hold the body
  flat_buffer dynamic_body;
  std::pmr::string response_content_type{alloc_}; // Store response content-type for lifetime
  PreparedResponseHeaders response_headers;
  CachedFileGuard cached_file;
  const uint8_t* response_data = nullptr;
  size_t response_len = 0;
  size_t response_offset = 0; // How much has been sent
  bool headers_sent = false;
  bool body_complete = false;
  bool fin_sent = false;
};

// Lookup a header by name in the flat per-request headers vector.
// Returns a pointer to the value string, or nullptr if not found.
static const std::pmr::string*
find_stream_header(
    const std::pmr::vector<std::pair<std::pmr::string, std::pmr::string>>& hdrs,
    std::string_view name) noexcept
{
  for (const auto& [k, v] : hdrs)
    if (k == name) return &v;
  return nullptr;
}

// Parse an HTTP-date (RFC 7231 preferred format) into a file_time_type.
// Returns nullopt if the string cannot be parsed.
// No heap allocation, no C function calls.
static std::optional<std::filesystem::file_time_type>
parse_http_date(std::string_view s) noexcept
{
  // "Mon, 18 Apr 2026 12:34:56 GMT" — fixed 29 bytes.
  //  0         1         2
  //  0123456789012345678901234567890
  if (s.size() != 29) return std::nullopt;

  // Validate all fixed separators and GMT suffix up front.
  if (s[3] != ',' || s[4] != ' ' || s[7] != ' ' || s[11] != ' ' ||
      s[16] != ' ' || s[19] != ':' || s[22] != ':' || s[25] != ' ' ||
      s[26] != 'G' || s[27] != 'M' || s[28] != 'T')
    return std::nullopt;

  // Parse fixed-width decimal. Uses unsigned subtraction — wraps on non-digit.
  auto parse_int = [&](int pos, int len) noexcept -> int {
    int val = 0;
    for (int i = 0; i < len; ++i) {
      const unsigned c =
          static_cast<unsigned>(static_cast<unsigned char>(s[pos + i])) - '0';
      if (c > 9) return -1;
      val = val * 10 + static_cast<int>(c);
    }
    return val;
  };

  // Month: pack s[8..10] into a uint32_t with a single 4-byte load.
  // s[11]==' ' is already validated above, so reading 4 bytes is safe.
  // Masking the high byte yields the same 3-char key as the constexpr table.
  static constexpr auto m3 = [](char a, char b, char c) constexpr -> uint32_t {
    return uint32_t(uint8_t(a)) | (uint32_t(uint8_t(b)) << 8)
           | (uint32_t(uint8_t(c)) << 16);
  };
  static constexpr uint32_t month_keys[12] = {
      m3('J','a','n'), m3('F','e','b'), m3('M','a','r'), m3('A','p','r'),
      m3('M','a','y'), m3('J','u','n'), m3('J','u','l'), m3('A','u','g'),
      m3('S','e','p'), m3('O','c','t'), m3('N','o','v'), m3('D','e','c'),
  };
  uint32_t mon;
  std::memcpy(&mon, s.data() + 8, 4); // single MOV r32; s[11]==' ' is valid
  mon &= 0x00FF'FFFFu;

  int month = -1;
  for (int i = 0; i < 12; ++i) {
    if (mon == month_keys[i]) { month = i; break; }
  }
  if (month == -1) return std::nullopt;

  const int day  = parse_int(5, 2);
  const int year = parse_int(12, 4);
  const int hour = parse_int(17, 2);
  const int min  = parse_int(20, 2);
  const int sec  = parse_int(23, 2);

  if (day < 1 || day > 31) return std::nullopt;
  if (year < 1970)         return std::nullopt;
  if (hour > 23)           return std::nullopt;
  if (min  > 59)           return std::nullopt;
  if (sec  > 60)           return std::nullopt; // 60 = leap second

  // Compute seconds since Unix epoch directly — no timegm, no struct tm.
  //
  // Leap days since 1970 up to but not including `year`:
  //   floor((y-1)/4) - floor((y-1)/100) + floor((y-1)/400) - 477
  //   (477 = same expression evaluated at y=1970)
  static constexpr int days_before_month[12] = {
      0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
  };
  const bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
  const int leap_days = (year - 1) / 4 - (year - 1) / 100 + (year - 1) / 400
                        - 477;

  const int64_t days = static_cast<int64_t>(year - 1970) * 365
                       + leap_days
                       + days_before_month[month]
                       + (month > 1 && leap ? 1 : 0)
                       + (day - 1);

  const int64_t epoch_sec = days * 86400
                            + static_cast<int64_t>(hour) * 3600
                            + min * 60
                            + sec;

  auto sys = std::chrono::sys_seconds{std::chrono::seconds{epoch_sec}};
  return std::chrono::clock_cast<std::filesystem::file_time_type::clock>(sys);
}

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

  // Called by server during graceful shutdown — sends HTTP/3 GOAWAY notice
  // then initiates a QUIC closing period. Must be called on the strand.
  void initiate_shutdown();

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
  int reject_oversized_request_body(Http3Stream* stream);
  void reject_webtransport_stream(Http3Stream* stream,
                                  std::string_view reason,
                                  size_t buffered_size);

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
  int send_not_modified(Http3Stream* stream, CachedFileGuard cached_file);
  int send_webtransport_connect_response(Http3Stream* stream);

  // RPC Handling
  int handle_rpc_request(Http3Stream* stream);
  int handle_webtransport_connect(Http3Stream* stream);

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

  // Aggregate packet writing callback for ngtcp2_conn_write_aggregate_pkt2
  ngtcp2_ssize write_pkt(ngtcp2_path* path, ngtcp2_pkt_info* pi,
                         uint8_t* dest, size_t destlen, ngtcp2_tstamp ts);
  static ngtcp2_ssize write_pkt_cb(ngtcp2_conn* conn, ngtcp2_path* path,
                                   ngtcp2_pkt_info* pi, uint8_t* dest,
                                   size_t destlen, ngtcp2_tstamp ts,
                                   void* user_data);

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

    // Get the SSL object to check state before and after
#if defined(OPENSSL_IS_BORINGSSL)
    auto ssl = static_cast<SSL*>(ngtcp2_conn_get_tls_native_handle(conn));
#else
    auto ossl_ctx = static_cast<ngtcp2_crypto_ossl_ctx*>(
        ngtcp2_conn_get_tls_native_handle(conn));
    auto ssl = ngtcp2_crypto_ossl_ctx_get_ssl(ossl_ctx);
#endif
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
  static nghttp3_ssize wt_read_data_cb(nghttp3_conn* conn,
                                       int64_t stream_id,
                                       nghttp3_vec* vec,
                                       size_t veccnt,
                                       uint32_t* pflags,
                                       void* user_data,
                                       void* stream_user_data);
  static int http_recv_wt_data_cb(nghttp3_conn* conn,
                                  int64_t session_id,
                                  int64_t stream_id,
                                  const uint8_t* data,
                                  size_t datalen,
                                  void* conn_user_data,
                                  void* stream_user_data);
  int recv_wt_data(int64_t session_id, int64_t stream_id,
                   const uint8_t* data, size_t datalen);

  Http3Server* server_;
  boost::asio::ip::udp::socket& socket_;
  boost::asio::ip::udp::endpoint local_ep_;
  boost::asio::ip::udp::endpoint remote_ep_;
  boost::asio::strand<boost::asio::io_context::executor_type> strand_;
  boost::asio::steady_timer timer_;

  ngtcp2_conn* conn_ = nullptr;
  ngtcp2_crypto_conn_ref conn_ref_;
  nghttp3_conn* httpconn_ = nullptr;
#if !defined(OPENSSL_IS_BORINGSSL)
  ngtcp2_crypto_ossl_ctx* ossl_ctx_ = nullptr;
#endif
  SSL* ssl_ = nullptr;
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

  // Current scratch packet borrowed from the server packet pool.
  PendingSendPacket* send_packet_ = nullptr;

  // Contiguous TX buffer for ngtcp2_conn_write_aggregate_pkt2 (GSO)
  std::array<uint8_t, 64 * 1024> txbuf_;

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
      , throttle_session_key_(http_request_throttler().allocate_session_key())
      , rx_buffer_(4 * 1024 * 1024)
  {
    ctx_.remote_endpoint = EndPoint(EndPointType::WebTransport,
                                    remote_ep.address().to_string(),
                                    remote_ep.port());
  }

  ~WebTransportControlSession() override
  {
    http_request_throttler().release_session(throttle_session_key_);
  }

  bool process_bytes(int64_t transport_stream_id, const uint8_t* data, size_t datalen)
  {
    auto& receive_buffer = receive_buffer_for(transport_stream_id);

    if (receive_buffer.size() > g_cfg.http_webtransport_max_message_size ||
        datalen > g_cfg.http_webtransport_max_message_size -
                      receive_buffer.size()) {
      NPRPC_LOG_ERROR(
          "[HTTP/3][WT] Rejecting oversized control/native payload: transport_stream_id={} buffered={} incoming={} limit={}",
          transport_stream_id, receive_buffer.size(), datalen,
          g_cfg.http_webtransport_max_message_size);
      return false;
    }

    append_bytes(receive_buffer, data, datalen);

    while (receive_buffer.size() >= sizeof(std::uint32_t)) {
      std::uint32_t message_len = 0;
      std::memcpy(&message_len, receive_buffer.data_ptr(), sizeof(message_len));

      if (message_len < sizeof(impl::flat::Header)) {
        NPRPC_LOG_ERROR(
            "[HTTP/3][WT] Rejecting undersized framed message: transport_stream_id={} size={} min={}",
            transport_stream_id, message_len, sizeof(impl::flat::Header));
        return false;
      }

      if (message_len > g_cfg.http_webtransport_max_message_size) {
        NPRPC_LOG_ERROR(
            "[HTTP/3][WT] Rejecting oversized framed message: transport_stream_id={} size={} limit={}",
            transport_stream_id, message_len, g_cfg.http_webtransport_max_message_size);
        return false;
      }

      const size_t frame_len = static_cast<size_t>(message_len);
      if (receive_buffer.size() < frame_len) {
        break;
      }

      rx_buffer_.clear();
      auto mb = rx_buffer_.prepare(frame_len);
      std::memcpy(mb.data(), receive_buffer.data_ptr(), frame_len);
      rx_buffer_.commit(frame_len);

      // Only rate-limit messages that require a reply (FunctionCall, StreamInit,
      // AddReference, etc.).  Fire-and-forget stream control messages
      // (WindowUpdate, DataChunk, Completion, Error, Cancellation) must never
      // be throttled — rejecting a window update starves the server-side writer
      // coroutine and kills the entire session.
      const bool is_fire_and_forget = [&]() {
        if (frame_len < sizeof(impl::flat::Header)) return false;
        const auto* hdr = reinterpret_cast<const impl::flat::Header*>(
            rx_buffer_.data().data());
        switch (hdr->msg_id) {
        case MessageId::StreamDataChunk:
        case MessageId::StreamCompletion:
        case MessageId::StreamError:
        case MessageId::StreamCancellation:
        case MessageId::StreamWindowUpdate:
          return true;
        default:
          return false;
        }
      }();

      if (!is_fire_and_forget && !allow_request()) {
        NPRPC_LOG_ERROR(
            "[HTTP/3][WT] Rejecting throttled session message: session_stream_id={} transport_stream_id={} size={} rate={} burst={}",
            session_stream_id_, transport_stream_id, frame_len,
            g_cfg.http_webtransport_requests_per_session_per_second,
            g_cfg.http_webtransport_requests_burst);
        return false;
      }

      const auto request_id = extract_request_id(rx_buffer_);
      flat_buffer tx_buffer{std::max(last_tx_size_, flat_buffer::default_initial_size())};
      const bool needs_reply = handle_request(rx_buffer_, tx_buffer);

      if (needs_reply) {
        last_tx_size_ = std::max(tx_buffer.size(), flat_buffer::default_initial_size());
        inject_request_id(tx_buffer, request_id);
        if (control_stream_id_ < 0) {
          NPRPC_LOG_ERROR("[HTTP/3][WT] Cannot reply before control stream is bound");
          return false;
        }
        queue_buffer(control_stream_id_, std::move(tx_buffer));
      }

      receive_buffer.consume(frame_len);
    }

    return true;
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
    receive_buffers_.erase(transport_stream_id);

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

  bool allow_child_stream_open(uint8_t bind_kind)
  {
    if (http_request_throttler().allow_session_stream_open(
            throttle_session_key_,
            g_cfg.http_webtransport_stream_opens_per_session_per_second,
            g_cfg.http_webtransport_stream_opens_burst)) {
      return true;
    }

    NPRPC_LOG_ERROR(
        "[HTTP/3][WT] Rejecting throttled child stream open: session_stream_id={} bind_kind={} rate={} burst={}",
        session_stream_id_, static_cast<unsigned>(bind_kind),
        g_cfg.http_webtransport_stream_opens_per_session_per_second,
        g_cfg.http_webtransport_stream_opens_burst);
    return false;
  }

private:
  bool allow_request()
  {
    return http_request_throttler().allow_session_request(
        throttle_session_key_,
        g_cfg.http_webtransport_requests_per_session_per_second,
        g_cfg.http_webtransport_requests_burst);
  }

  flat_buffer& receive_buffer_for(int64_t transport_stream_id)
  {
    auto it = receive_buffers_.find(transport_stream_id);
    if (it == receive_buffers_.end()) {
      it = receive_buffers_
               .emplace(transport_stream_id,
                        flat_buffer(flat_buffer::default_initial_size()))
               .first;
    }

    return it->second;
  }

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
  uint64_t throttle_session_key_;
  flat_buffer rx_buffer_;
  size_t last_tx_size_{flat_buffer::default_initial_size()};
  std::unordered_map<int64_t, flat_buffer> receive_buffers_;
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

#if NPRPC_ENABLE_HTTP3_REUSEPORT_SANITY
struct Http3ReusePortSanitySnapshot {
  uint8_t worker_id = 0;
  uint64_t receive_callbacks = 0;
  uint64_t received_packets = 0;
  uint64_t initial_packets = 0;
  uint64_t established_packets = 0;
};
#endif

class Http3ServerRuntime; // forward declaration for friend

class Http3Server
{
public:
  friend class Http3Connection;
  friend class Http3ServerRuntime;

  Http3Server(boost::asio::io_context& ioc,
              const std::string& cert_file,
              const std::string& key_file,
              uint16_t port,
              uint8_t worker_id);
  ~Http3Server();

  bool start();
  void stop();

  boost::asio::io_context& io_context() { return ioc_; }
  boost::asio::ip::udp::socket& socket() { return socket_; }
  uint8_t worker_id() const noexcept { return worker_id_; }
  bool    running()   const noexcept { return running_.load(std::memory_order_acquire); }

  // Send packet to remote endpoint
  int send_packet(const boost::asio::ip::udp::endpoint& remote_ep,
                  const uint8_t* data,
                  size_t len);

  bool allow_rpc_request(const boost::asio::ip::udp::endpoint& remote_ep);
  bool allow_webtransport_connect(const boost::asio::ip::udp::endpoint& remote_ep);

  // Connection management
  void associate_cid(const ngtcp2_cid* cid,
                     std::shared_ptr<Http3Connection> conn);
  void dissociate_cid(const ngtcp2_cid* cid);
  void remove_connection(std::shared_ptr<Http3Connection> conn);

#if NPRPC_ENABLE_HTTP3_REUSEPORT_SANITY
  Http3ReusePortSanitySnapshot reuseport_sanity_snapshot() const noexcept
  {
    return {
        .worker_id = worker_id_,
        .receive_callbacks = sanity_receive_callbacks_,
        .received_packets = sanity_received_packets_,
        .initial_packets = sanity_initial_packets_,
        .established_packets = sanity_established_packets_,
    };
  }
#endif

private:
  bool allow_new_connection(const boost::asio::ip::udp::endpoint& remote_ep);
  int send_packet(PendingSendPacket* packet);
  PendingSendPacket* acquire_send_packet();
  void recycle_send_packet(PendingSendPacket* packet);
  void drain_send_inbox();
  bool try_send_gso_batch();
  void send_batch(PendingSendPacket** packets, size_t count);
  void send_aggregated(const boost::asio::ip::udp::endpoint& remote_ep,
                       const uint8_t* data, size_t len, size_t gso_size);
  void on_connection_accepted(const boost::asio::ip::udp::endpoint& remote_ep);
  void do_send();
  void do_receive();
  static boost::asio::ip::udp::endpoint decode_ingress_ep(const ShmIngressFrame& hdr);
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

  static constexpr std::size_t cache_line_size = std::hardware_destructive_interference_size;

  boost::asio::io_context& ioc_;
  std::string cert_file_;
  std::string key_file_;
  uint16_t port_;
  uint8_t worker_id_;

  boost::asio::ip::udp::socket socket_;
  boost::asio::ip::udp::endpoint local_ep_;  // Local bound address
  boost::asio::ip::udp::endpoint remote_ep_; // For async_receive_from
  std::array<uint8_t, 65536> recv_buf_;

  SSL_CTX* ssl_ctx_ = nullptr;

  // Map from CID to connection (includes both SCID and additional CIDs)
  ankerl::unordered_dense::map<std::string, std::shared_ptr<Http3Connection>>
      connections_;

  std::mutex send_storage_mutex_;
  PendingSendPacket* send_ready_head_ = nullptr;
  PendingSendPacket* send_ready_tail_ = nullptr;
  std::size_t send_ready_size_ = 0;
  std::vector<std::unique_ptr<PendingSendPacket[]>> send_packet_storage_;
  std::vector<std::unique_ptr<PendingSendPacketPayload[]>>
      send_packet_payload_storage_;
#if !defined(_WIN32) && defined(UDP_SEGMENT)
  bool udp_gso_supported_ = true;
#endif

#if NPRPC_ENABLE_HTTP3_REUSEPORT_SANITY
  uint64_t sanity_receive_callbacks_ = 0;
  uint64_t sanity_received_packets_ = 0;
  uint64_t sanity_initial_packets_ = 0;
  uint64_t sanity_established_packets_ = 0;
#endif

  // All four are accessed only from the single ioc_ thread, except running_
  // which is written by stop() from an external thread.
  PendingSendPacket* send_pool_head_{nullptr};
  PendingSendPacket* send_inbox_head_{nullptr};
  bool send_in_progress_{false};
  std::atomic<bool> running_{false};

  // Optional SHM egress ring (writer side, opened from npquicrouter's SHM).
  // When present, send_aggregated() writes frames here instead of calling
  // sendmsg(), so that npquicrouter can forward them as a single
  // sendmsg(GSO) batch to the QUIC client.
  std::unique_ptr<nprpc::impl::LockFreeRingBuffer> egress_ring_;


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
  generate_server_connection_id(&scid_, SCID_LEN, server_->worker_id());

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
#if defined(OPENSSL_IS_BORINGSSL)
  if (ssl_) {
    SSL_set_app_data(ssl_, nullptr);
    SSL_free(ssl_);
  }
#else
  if (ossl_ctx_) {
    auto ssl = ngtcp2_crypto_ossl_ctx_get_ssl(ossl_ctx_);
    if (ssl) {
      SSL_set_app_data(ssl, nullptr);
      SSL_free(ssl);
    }
    ngtcp2_crypto_ossl_ctx_del(ossl_ctx_);
  }
#endif
}

bool Http3Connection::init()
{
  NPRPC_HTTP3_TRACE("Http3Connection::init() starting...");

#if !defined(OPENSSL_IS_BORINGSSL)
  // Create the OpenSSL QUIC context wrapper (not needed for BoringSSL)
  if (ngtcp2_crypto_ossl_ctx_new(&ossl_ctx_, nullptr) != 0) {
    NPRPC_LOG_ERROR("[HTTP/3][E] Failed to create OpenSSL QUIC context");
    return false;
  }
#endif

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
  ssl_ = SSL_new(ssl_ctx_);
  if (!ssl_) {
    NPRPC_LOG_ERROR("[HTTP/3][E] SSL_new failed: {}",
                    ERR_error_string(ERR_get_error(), nullptr));
    return false;
  }

#if defined(OPENSSL_IS_BORINGSSL)
  // The QUIC method was set on ssl_ctx_ in Http3Server::start();
  // per-connection init only needs to set app data and accept state.
  SSL_set_app_data(ssl_, &conn_ref_);
  SSL_set_accept_state(ssl_);

  ngtcp2_conn_set_tls_native_handle(conn_, ssl_);
#else
  ngtcp2_crypto_ossl_ctx_set_ssl(ossl_ctx_, ssl_);

  // Configure SSL for QUIC server FIRST - this sets up the QUIC TLS callbacks
  if (ngtcp2_crypto_ossl_configure_server_session(ssl_) != 0) {
    NPRPC_LOG_ERROR("[HTTP/3][E] Failed to configure server SSL session");
    SSL_free(ssl_);
    ssl_ = nullptr;
    return false;
  }

  // Set app data AFTER configuring for QUIC (reference order)
  SSL_set_app_data(ssl_, &conn_ref_);
  SSL_set_accept_state(ssl_);
  SSL_set_quic_tls_early_data_enabled(ssl_, 0);

  ngtcp2_conn_set_tls_native_handle(conn_, ossl_ctx_);
#endif

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

  // NPRPC_HTTP3_TRACE("Reading packet, len={}", len);

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

  // NPRPC_HTTP3_TRACE("Packet processed successfully, handshake_completed={}",
  //                   ngtcp2_conn_get_handshake_completed(conn_));

  print_ssl_state("on_read end");

  schedule_timer();

  // Send any pending data (handshake responses, etc.)
  on_write();

  return 0;
}

// Static trampoline for ngtcp2_conn_write_aggregate_pkt2
ngtcp2_ssize Http3Connection::write_pkt_cb(
    ngtcp2_conn* conn, ngtcp2_path* path, ngtcp2_pkt_info* pi,
    uint8_t* dest, size_t destlen, ngtcp2_tstamp ts, void* user_data)
{
  auto* self = static_cast<Http3Connection*>(user_data);
  return self->write_pkt(path, pi, dest, destlen, ts);
}

// Writes a single QUIC packet — called repeatedly by aggregate_pkt2
ngtcp2_ssize Http3Connection::write_pkt(
    ngtcp2_path* path, ngtcp2_pkt_info* pi,
    uint8_t* dest, size_t destlen, ngtcp2_tstamp ts)
{
  std::array<nghttp3_vec, 16> vec;
  ngtcp2_vec raw_vec{};

  for (;;) {
    int64_t stream_id = -1;
    int fin = 0;
    nghttp3_ssize sveccnt = 0;
    bool using_http_stream = false;

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
        return NGTCP2_ERR_CALLBACK_FAILURE;
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

    auto nwrite = ngtcp2_conn_writev_stream(
        conn_, path, pi, dest, destlen, &ndatalen, flags, stream_id,
        using_http_stream ? reinterpret_cast<const ngtcp2_vec*>(vec.data())
                          : &raw_vec,
        static_cast<size_t>(sveccnt), ts);

    if (nwrite < 0) {
      switch (nwrite) {
      case NGTCP2_ERR_STREAM_DATA_BLOCKED:
        if (using_http_stream && httpconn_) {
          nghttp3_conn_block_stream(httpconn_, stream_id);
        } else if (raw_stream) {
          // Re-enqueue so it retries when unblocked; continue to let
          // other streams write in this burst.
          schedule_raw_writable_stream(raw_stream);
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
            ngtcp2_ccerr_set_application_error(
                &last_error_, nghttp3_err_infer_quic_app_error_code(rv),
                nullptr, 0);
            return NGTCP2_ERR_CALLBACK_FAILURE;
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
      return NGTCP2_ERR_CALLBACK_FAILURE;
    }

    if (using_http_stream && ndatalen >= 0 && httpconn_) {
      auto rv = nghttp3_conn_add_write_offset(httpconn_, stream_id,
                                              static_cast<size_t>(ndatalen));
      if (rv != 0) {
        ngtcp2_ccerr_set_application_error(
            &last_error_, nghttp3_err_infer_quic_app_error_code(rv), nullptr,
            0);
        return NGTCP2_ERR_CALLBACK_FAILURE;
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

    return nwrite;
  }
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

  // NPRPC_HTTP3_TRACE("on_write called");

  ngtcp2_path_storage ps;
  ngtcp2_pkt_info pi;
  size_t gso_size = 0;

  ngtcp2_path_storage_zero(&ps);

  auto nwrite = ngtcp2_conn_write_aggregate_pkt2(
      conn_, &ps.path, &pi, txbuf_.data(), txbuf_.size(), &gso_size,
      write_pkt_cb, MAX_PKTS_BURST, timestamp_ns());
  if (nwrite < 0) {
    return handle_error();
  }

  ngtcp2_conn_update_pkt_tx_time(conn_, timestamp_ns());

  if (nwrite > 0) {
    server_->send_aggregated(remote_ep_, txbuf_.data(),
                             static_cast<size_t>(nwrite), gso_size);
  }

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

void Http3Connection::initiate_shutdown()
{
  // Post onto our strand so this is safe to call from any thread
  // (Http3Server::stop dispatches to ioc_, connections live on strands).
  boost::asio::post(strand_, [self = shared_from_this()]() {
    if (self->closed_) return;

    // Phase 1: send GOAWAY with max stream ID so the client stops opening new
    // requests while we finish in-flight ones (RFC 9114 §5.2).
    if (self->httpconn_) {
      int rv = nghttp3_conn_submit_shutdown_notice(self->httpconn_);
      if (rv != 0) {
        NPRPC_HTTP3_ERROR("nghttp3_conn_submit_shutdown_notice: {}",
                          nghttp3_strerror(rv));
      }
    }

    // Flush the GOAWAY frame and move to the QUIC closing period.
    self->on_write();
    self->start_closing_period();
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
  callbacks.recv_wt_data = http_recv_wt_data_cb;

  nghttp3_settings settings;
  nghttp3_settings_default(&settings);
  settings.qpack_max_dtable_capacity = 4096;
  settings.qpack_blocked_streams = 100;
  settings.enable_connect_protocol = 1;
  settings.h3_datagram = 1;
  settings.wt_enabled = 1;

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

  if (!httpconn_) {
    return 0;
  }

  // Feed ALL stream data to nghttp3 — it handles WT stream framing internally.
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

  const auto n_sv = std::string_view(reinterpret_cast<const char*>(n.base), n.len);
  const auto v_sv = std::string_view(reinterpret_cast<const char*>(v.base), v.len);

  // Store all headers in the arena-backed flat vector.
  stream->headers.emplace_back(
      std::pmr::string(n_sv, stream->alloc_),
      std::pmr::string(v_sv, stream->alloc_));

  switch (token) {
  case NGHTTP3_QPACK_TOKEN__PATH:
    stream->path = v_sv;
    break;
  case NGHTTP3_QPACK_TOKEN__METHOD:
    stream->method = v_sv;
    break;
  case NGHTTP3_QPACK_TOKEN__AUTHORITY:
    stream->authority = v_sv;
    break;
  default:
    if (n_sv == ":scheme") {
      stream->scheme = v_sv;
    } else if (n_sv == ":protocol") {
      stream->protocol = v_sv;
    }
    break;
  case NGHTTP3_QPACK_TOKEN_CONTENT_TYPE:
    stream->content_type = v_sv;
    break;
  case NGHTTP3_QPACK_TOKEN_CONTENT_LENGTH: {
    const auto parsed = parse_http_content_length(v_sv);
    if (!parsed) {
      stream->malformed_content_length = true;
      NPRPC_HTTP3_ERROR(
          "Rejecting malformed Content-Length stream_id={} path='{}' value='{}'",
          stream->stream_id, stream->path, v_sv);
      break;
    }

    stream->content_length = *parsed;
    break;
  }
  case NGHTTP3_QPACK_TOKEN_ACCEPT:
    stream->accept = v_sv;
    break;
  }

  // NPRPC_HTTP3_DEBUG("Stream {} recv_header", stream->stream_id);
  // for (const auto& [key, value] : stream->headers) {
  //   NPRPC_HTTP3_DEBUG("\t{}: {}", key, value);
  // }
}

int Http3Connection::http_end_headers(Http3Stream* stream)
{
  NPRPC_HTTP3_TRACE("Request: {} {}", stream->method, stream->path);

  if (stream->malformed_content_length) {
    return send_static_response(stream, 400, "text/plain",
                                "Malformed Content-Length header");
  }

  if (stream->request_body_too_large ||
      stream->content_length > g_cfg.http_max_request_body_size) {
    return reject_oversized_request_body(stream);
  }

  if (stream->content_length != 0 && !stream->request_body_preallocated) {
    stream->request_body = flat_buffer(stream->content_length);
    stream->request_body_preallocated = true;
  }

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
    {
      const auto* _orig = find_stream_header(stream->headers, "origin");
      NPRPC_HTTP3_TRACE("Received WebTransport CONNECT stream_id={} authority='{}' scheme='{}' origin='{}'",
                        stream->stream_id, stream->authority, stream->scheme,
                        _orig ? std::string_view{*_orig} : std::string_view{});
    }
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
          std::string("https://") + stream->authority.c_str() + stream->path.c_str();

      // Filter out HTTP/2 pseudo-headers (start with ':') for Web API
      // compatibility
      std::map<std::string, std::string> filtered_headers;
      for (const auto& [key, value] : stream->headers) {
        if (!key.empty() && key[0] != ':') {
          filtered_headers[std::string(key)] = std::string(value);
        }
      }

      // Forward to SSR (synchronous call)
      auto ssr_response =
          nprpc::impl::forward_to_ssr(std::string_view(stream->method), url, filtered_headers,
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
    std::string request_path(stream->path);

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

    // Conditional GET: If-None-Match takes precedence over If-Modified-Since
    // (RFC 7232 §6). Both etag and last_modified_str are pre-computed in
    // CachedFile — zero per-request string allocation.
    {
      const auto* inm = find_stream_header(stream->headers, "if-none-match");
      if (inm) {
        if (std::string_view{*inm} == cached_file->etag()) {
          return send_not_modified(stream, std::move(cached_file));
        }
      } else {
        const auto* ims = find_stream_header(stream->headers, "if-modified-since");
        if (ims) {
          auto req_time = parse_http_date(*ims);
          if (req_time && cached_file->mtime() <= *req_time) {
            return send_not_modified(stream, std::move(cached_file));
          }
        }
      }
    }

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
          std::string("https://") + stream->authority.c_str() + stream->path.c_str();

      // Filter out HTTP/2 pseudo-headers
      std::map<std::string, std::string> filtered_headers;
      for (const auto& [key, value] : stream->headers) {
        if (!key.empty() && key[0] != ':') {
          filtered_headers[std::string(key)] = std::string(value);
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

  // Add cache-validation headers — point directly into CachedFile's
  // pre-computed strings. No per-request allocation.
  constexpr uint8_t ncnv =
      NGHTTP3_NV_FLAG_NO_COPY_NAME | NGHTTP3_NV_FLAG_NO_COPY_VALUE;
  headers.headers.push_back({
      .name    = reinterpret_cast<uint8_t*>(const_cast<char*>("cache-control")),
      .value   = reinterpret_cast<uint8_t*>(const_cast<char*>("public, max-age=3600")),
      .namelen  = 13, .valuelen = 20, .flags = ncnv,
  });
  headers.headers.push_back({
      .name    = reinterpret_cast<uint8_t*>(const_cast<char*>("etag")),
      .value   = reinterpret_cast<uint8_t*>(const_cast<char*>(stream->cached_file->etag().data())),
      .namelen  = 4, .valuelen = stream->cached_file->etag().size(), .flags = ncnv,
  });
  headers.headers.push_back({
      .name    = reinterpret_cast<uint8_t*>(const_cast<char*>("last-modified")),
      .value   = reinterpret_cast<uint8_t*>(const_cast<char*>(stream->cached_file->last_modified_str().data())),
      .namelen  = 13, .valuelen = stream->cached_file->last_modified_str().size(), .flags = ncnv,
  });

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
  if (!server_->allow_rpc_request(remote_ep_)) {
    NPRPC_LOG_ERROR(
        "[HTTP/3] Rejecting throttled RPC request from {} path='{}'",
        remote_ep_.address().to_string(), stream ? std::string_view(stream->path) : std::string_view{});
    return send_static_response(stream, 429, "text/plain", "Too Many Requests");
  }

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

int Http3Connection::reject_oversized_request_body(Http3Stream* stream)
{
  if (!stream) {
    return -1;
  }

  if (stream->response_started) {
    return 0;
  }

  const auto buffered_size = stream->request_body.size();

  stream->response_started = true;
  stream->request_body_too_large = true;
  stream->request_body.clear();

  NPRPC_HTTP3_ERROR(
      "Rejecting oversized request body stream_id={} path='{}' content_length={} buffered={} limit={}",
      stream->stream_id, stream->path, stream->content_length,
      buffered_size, g_cfg.http_max_request_body_size);

  return send_static_response(stream, 413, "text/plain",
                              "Request body too large");
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
    const auto* origin_hdr = find_stream_header(stream->headers, "origin");
    const auto allowed_origin =
      origin_hdr
        ? get_allowed_http_origin(*origin_hdr)
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
  const auto* origin_hdr = find_stream_header(stream->headers, "origin");
  const auto allowed_origin =
      origin_hdr
          ? get_allowed_http_origin(*origin_hdr)
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

int Http3Connection::send_not_modified(Http3Stream* stream,
                                       CachedFileGuard cached_file)
{
  // Pin the file in the stream so its pre-computed strings stay alive until
  // nghttp3 finishes draining the 304 headers (stream close).
  stream->cached_file = std::move(cached_file);

  auto& prepared = stream->response_headers;
  prepared.headers.clear();
  prepared.status = "304";

  constexpr uint8_t ncnv =
      NGHTTP3_NV_FLAG_NO_COPY_NAME | NGHTTP3_NV_FLAG_NO_COPY_VALUE;
  auto add_sv = [&](const char* name, size_t nl, std::string_view val) {
    prepared.headers.push_back({
        .name     = reinterpret_cast<uint8_t*>(const_cast<char*>(name)),
        .value    = reinterpret_cast<uint8_t*>(const_cast<char*>(val.data())),
        .namelen  = nl,
        .valuelen = val.size(),
        .flags    = ncnv,
    });
  };

  add_sv(":status",       7,  prepared.status);
  add_sv("etag",          4,  stream->cached_file->etag());
  add_sv("cache-control", 13, "public, max-age=3600");
  add_sv("last-modified", 13, stream->cached_file->last_modified_str());

  log_http3_response_submit("not_modified", stream, 304, "", 0);

  int rv = nghttp3_conn_submit_response(httpconn_, stream->stream_id,
                                        prepared.headers.data(),
                                        prepared.headers.size(), nullptr);
  if (rv != 0) {
    NPRPC_HTTP3_ERROR("nghttp3_conn_submit_response (304): {}", nghttp3_strerror(rv));
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

      const auto* origin_hdr = find_stream_header(stream->headers, "origin");
      const auto allowed_origin =
        origin_hdr
          ? get_allowed_http_origin(*origin_hdr)
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

  log_http3_response_submit("webtransport_connect", stream, 200, "", 0);
  int rv = nghttp3_conn_submit_wt_response(httpconn_, stream->stream_id,
                                           headers.headers.data(),
                                           headers.headers.size());
  if (rv != 0) {
    NPRPC_HTTP3_ERROR("nghttp3_conn_submit_wt_response(CONNECT): {}", nghttp3_strerror(rv));
    return -1;
  }

  return 0;
}

int Http3Connection::handle_webtransport_connect(Http3Stream* stream)
{
  if (!server_->allow_webtransport_connect(remote_ep_)) {
    const auto* _oh = find_stream_header(stream->headers, "origin");
    NPRPC_HTTP3_ERROR(
        "Rejecting throttled WebTransport CONNECT stream_id={} origin='{}' authority='{}'",
        stream->stream_id,
        _oh ? std::string_view{*_oh} : std::string_view{},
        stream->authority);
    return send_static_response(stream, 429, "text/plain", "Too Many Requests");
  }

  if (stream->scheme != "https" || stream->authority.empty()) {
    NPRPC_HTTP3_ERROR("Rejecting WebTransport CONNECT stream_id={} scheme='{}' authority='{}'",
                      stream->stream_id, stream->scheme, stream->authority);
    return send_static_response(stream, 400, "text/plain",
                                "Invalid WebTransport CONNECT request");
  }

  const auto* origin_hdr = find_stream_header(stream->headers, "origin");
  const auto origin =
      origin_hdr ? std::string_view{*origin_hdr} : std::string_view{};
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
      stream->protocol, origin);

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

void Http3Connection::reject_webtransport_stream(Http3Stream* stream,
                                                 std::string_view reason,
                                                 size_t buffered_size)
{
  if (!stream || stream->webtransport_rejected) {
    return;
  }

  stream->webtransport_rejected = true;
  stream->webtransport_probe_buffer.clear();

  NPRPC_LOG_ERROR(
      "[HTTP/3][WT] Rejecting stream {} session={} reason='{}' buffered={} limit={}",
      stream->stream_id, stream->webtransport_session_id, reason, buffered_size,
      g_cfg.http_webtransport_max_message_size);

  ngtcp2_conn_shutdown_stream_read(conn_, 0, stream->stream_id,
                                   NGHTTP3_H3_MESSAGE_ERROR);
  ngtcp2_conn_shutdown_stream_write(conn_, 0, stream->stream_id,
                                    NGHTTP3_H3_MESSAGE_ERROR);
}

void Http3Connection::queue_raw_stream_write(int64_t stream_id,
                                             flat_buffer&& data)
{
  boost::asio::post(
      strand_, [self = shared_from_this(), stream_id, data = std::move(data)]() mutable {
        if (self->closed_) return;
        auto* stream = self->find_stream(stream_id);
        if (!stream) return; // stream already closed, discard

        // Detect terminal stream messages (StreamCompletion/StreamError)
        // to signal EOF to nghttp3 after all data is drained.
        // Only for Native streams — the Control stream multiplexes all
        // NPRPC traffic and must never be FIN'd.
        if (stream->webtransport_child_binding == WebTransportChildBinding::Native &&
            data.size() >= sizeof(impl::flat::Header)) {
          const auto* hdr = reinterpret_cast<const impl::flat::Header*>(
              data.data().data());
          if (hdr->msg_id == MessageId::StreamCompletion ||
              hdr->msg_id == MessageId::StreamError) {
            NPRPC_HTTP3_DEBUG(
                "conn={} wt_write_fin_pending stream_id={} msg_id={} msg_len={}",
                self->debug_id(), stream_id, static_cast<int>(hdr->msg_id),
                data.size());
            stream->wt_write_fin_pending = true;
          }
        }

        stream->raw_write_queue.emplace_back(std::move(data));

        // For WT data streams, use nghttp3's write scheduling.
        if (stream->webtransport_child_stream && self->httpconn_) {
          if (!stream->wt_data_stream_opened) {
            nghttp3_data_reader dr{.read_data = wt_read_data_cb};
            int rv = nghttp3_conn_open_wt_data_stream(
                self->httpconn_,
                stream->webtransport_session_id,
                stream_id, &dr, stream);
            if (rv != 0) {
              NPRPC_HTTP3_ERROR(
                  "nghttp3_conn_open_wt_data_stream: stream_id={} session_id={} err={}",
                  stream_id, stream->webtransport_session_id,
                  nghttp3_strerror(rv));
              return;
            }
            stream->wt_data_stream_opened = true;
          } else {
            // Stream already opened — resume it so writev_stream picks up new data.
            nghttp3_conn_resume_stream(self->httpconn_, stream_id);
          }
        } else {
          // Non-WT raw stream: use the old raw write scheduling.
          if (stream->raw_write_queue.size() == 1) {
            self->schedule_raw_writable_stream(stream);
          }
        }
        self->on_write();
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
  stream->response_content_type = content_type; // Store content-type for lifetime
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
# if defined(OPENSSL_IS_BORINGSSL)
  auto ssl = ssl_;
# else
  auto ssl = ngtcp2_crypto_ossl_ctx_get_ssl(ossl_ctx_);
# endif
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

  // Forward ALL stream closures to nghttp3 (including WT data streams).
  if (h->httpconn_) {
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
  auto h = static_cast<Http3Connection*>(user_data);
  generate_server_connection_id(cid, cidlen, h->server_->worker_id());

  if (ngtcp2_crypto_generate_stateless_reset_token(
          token, g_static_secret.data(), g_static_secret.size(), cid) != 0) {
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }

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
  // For WT data streams, consume acknowledged data from raw_write_queue.
  // wt_write_offset is relative to current queue start, so adjust it as
  // we pop/consume chunks from the front.
  auto stream = static_cast<Http3Stream*>(stream_user_data);
  if (stream && stream->wt_data_stream_opened) {
    auto remaining = datalen;
    while (remaining > 0 && !stream->raw_write_queue.empty()) {
      auto& front = stream->raw_write_queue.front();
      if (front.size() <= remaining) {
        remaining -= front.size();
        stream->wt_write_offset -= front.size();
        stream->raw_write_queue.pop_front();
      } else {
        auto n = static_cast<size_t>(remaining);
        front.consume(n);
        stream->wt_write_offset -= n;
        remaining = 0;
      }
    }
  }
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
    const auto current_size = stream->request_body.size();
    if (current_size > g_cfg.http_max_request_body_size ||
        datalen > g_cfg.http_max_request_body_size - current_size) {
      stream->request_body_too_large = true;
      return h->reject_oversized_request_body(stream);
    }

    if (!stream->request_body_preallocated && stream->content_length != 0) {
      stream->request_body = flat_buffer(stream->content_length);
      stream->request_body_preallocated = true;
    }

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

  try {
    if (stream) {
      h->http_recv_header(stream, token, name, value);
    }
  } catch (const std::exception& e) {
    NPRPC_HTTP3_ERROR("Exception in http_recv_header_cb: {}", e.what());
    return NGHTTP3_ERR_CALLBACK_FAILURE;
  } catch (...) {
    NPRPC_HTTP3_ERROR("Unknown exception in http_recv_header_cb");
    return NGHTTP3_ERR_CALLBACK_FAILURE;
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

  try {
    if (stream && h->http_end_headers(stream) != 0) {
      return NGHTTP3_ERR_CALLBACK_FAILURE;
    }
  } catch (const std::exception& e) {
    NPRPC_HTTP3_ERROR("Exception in http_end_headers_cb: {}", e.what());
    return NGHTTP3_ERR_CALLBACK_FAILURE;
  } catch (...) {
    NPRPC_HTTP3_ERROR("Unknown exception in http_end_headers_cb");
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
  const auto chunk_len = std::min(remaining, kHttp3ResponseChunkSize);
  vec[0].base =
      const_cast<uint8_t*>(stream->response_data + stream->response_offset);
  vec[0].len = chunk_len;
  stream->response_offset += chunk_len;

  NPRPC_HTTP3_DEBUG(
      "read_data stream_id={} path='{}' chunk_len={} offset={} next_offset={} total={} eof=true",
      stream_id, stream->path, chunk_len, current_offset,
      stream->response_offset, stream->response_len);

  if (stream->response_offset == stream->response_len) {
    *pflags |= NGHTTP3_DATA_FLAG_EOF;
  }

  return 1;
}

// WT data stream read callback — serves data from raw_write_queue.
// Advances wt_write_offset; buffers freed later in acked_stream_data_cb.
nghttp3_ssize Http3Connection::wt_read_data_cb(nghttp3_conn* conn,
                                                int64_t stream_id,
                                                nghttp3_vec* vec,
                                                size_t veccnt,
                                                uint32_t* pflags,
                                                void* user_data,
                                                void* stream_user_data)
{
  auto stream = static_cast<Http3Stream*>(stream_user_data);
  if (!stream) {
    return NGHTTP3_ERR_WOULDBLOCK;
  }
  if (stream->raw_write_queue.empty()) {
    if (stream->wt_write_fin_pending) {
      NPRPC_HTTP3_DEBUG(
          "wt_read_data_cb stream_id={} wt_write_fin_pending=true empty_queue eof",
          stream_id);
      *pflags |= NGHTTP3_DATA_FLAG_EOF;
      return 0;
    }
    return NGHTTP3_ERR_WOULDBLOCK;
  }

  // Find the first chunk that hasn't been fully scheduled yet.
  // wt_write_offset is cumulative across all chunks in the queue.
  size_t cumulative = 0;
  for (auto& chunk : stream->raw_write_queue) {
    cumulative += chunk.size();
    if (stream->wt_write_offset < cumulative) {
      const size_t offset_in_chunk =
          chunk.size() - (cumulative - stream->wt_write_offset);
      const size_t remaining = chunk.size() - offset_in_chunk;
      vec[0].base = chunk.data_ptr() + offset_in_chunk;
      vec[0].len = remaining;
      stream->wt_write_offset += remaining;
      return 1;
    }
  }

  // All data already scheduled.
  // If the stream is done (StreamCompletion/StreamError was queued),
  // signal EOF so nghttp3 sends FIN and the QUIC stream can close,
  // which allows extend_max_streams_bidi to reclaim the slot.
  if (stream->wt_write_fin_pending) {
    *pflags |= NGHTTP3_DATA_FLAG_EOF;
    return 0;
  }

  return NGHTTP3_ERR_WOULDBLOCK;
}

int Http3Connection::http_recv_wt_data_cb(nghttp3_conn* conn,
                                          int64_t session_id,
                                          int64_t stream_id,
                                          const uint8_t* data,
                                          size_t datalen,
                                          void* conn_user_data,
                                          void* stream_user_data)
{
  auto h = static_cast<Http3Connection*>(conn_user_data);
  if (h->recv_wt_data(session_id, stream_id, data, datalen) != 0) {
    return NGHTTP3_ERR_CALLBACK_FAILURE;
  }
  // nghttp3 does NOT include WT data payload in the nconsumed value
  // returned by read_stream2, so we must extend flow control here.
  ngtcp2_conn_extend_max_stream_offset(h->conn_, stream_id, datalen);
  ngtcp2_conn_extend_max_offset(h->conn_, datalen);
  return 0;
}

int Http3Connection::recv_wt_data(int64_t session_id,
                                  int64_t stream_id,
                                  const uint8_t* data,
                                  size_t datalen)
{
  NPRPC_HTTP3_DEBUG(
      "conn={} recv_wt_data session_id={} stream_id={} datalen={}", debug_id(),
      session_id, stream_id, datalen);
  auto* stream = find_stream(stream_id);
  if (!stream) {
    stream = create_stream(stream_id);
  }

  // Mark as WT child stream and associate with session
  if (!stream->webtransport_child_stream) {
    stream->webtransport_child_stream = true;
    stream->webtransport_session_id = session_id;
    // Set stream user data so nghttp3 callbacks get our Http3Stream*
    nghttp3_conn_set_stream_user_data(httpconn_, stream_id, stream);
  }

  // Binding protocol: first byte determines control vs native stream
  if (stream->webtransport_child_binding == WebTransportChildBinding::Unbound) {
    // Accumulate into probe buffer for binding detection
    append_bytes(stream->webtransport_probe_buffer, data, datalen);

    if (stream->webtransport_probe_buffer.size() < 1) {
      return 0;
    }

    const auto* probe = stream->webtransport_probe_buffer.data_ptr();
    const auto bind_kind = probe[0];
    size_t binding_len = 0;

    auto session = get_or_create_webtransport_control_session(session_id);

    NPRPC_HTTP3_DEBUG(
        "conn={} wt_binding_probe stream_id={} session_stream_id={} bind_kind={} buffered={}",
        debug_id(), stream_id, session_id, bind_kind,
        stream->webtransport_probe_buffer.size());

    switch (bind_kind) {
    case k_webtransport_bind_control:
      if (!session->allow_child_stream_open(bind_kind)) {
        NPRPC_LOG_ERROR("[HTTP/3][WT] child stream open throttled for control binding");
        return -1;
      }
      stream->webtransport_child_binding = WebTransportChildBinding::Control;
      session->bind_control_stream(stream_id);
      binding_len = 1;
      NPRPC_HTTP3_DEBUG(
          "conn={} wt_bind_control transport_stream_id={} session_stream_id={}",
          debug_id(), stream_id, session_id);
      break;
    case k_webtransport_bind_native_stream:
      if (stream->webtransport_probe_buffer.size() < 9) {
        return 0; // Need more data (1 byte kind + 8 byte stream_id)
      }
      if (!session->allow_child_stream_open(bind_kind)) {
        NPRPC_LOG_ERROR("[HTTP/3][WT] child stream open throttled for native binding");
        return -1;
      }
      std::memcpy(&stream->webtransport_native_stream_id, probe + 1,
                   sizeof(stream->webtransport_native_stream_id));
      stream->webtransport_child_binding = WebTransportChildBinding::Native;
      session->bind_native_stream(stream->webtransport_native_stream_id,
                                  stream_id);
      binding_len = 9;
      NPRPC_HTTP3_DEBUG(
          "conn={} wt_bind_native transport_stream_id={} session_stream_id={} native_stream_id={}",
          debug_id(), stream_id, session_id,
          stream->webtransport_native_stream_id);
      break;
    default:
      NPRPC_LOG_ERROR("[HTTP/3][WT] Unknown child stream binding kind {}",
                      bind_kind);
      return -1;
    }

    // Forward remaining data after binding header to session
    if (stream->webtransport_probe_buffer.size() > binding_len) {
      if (!session->process_bytes(
              stream_id,
              stream->webtransport_probe_buffer.data_ptr() + binding_len,
              stream->webtransport_probe_buffer.size() - binding_len)) {
        return -1;
      }
    }
    stream->webtransport_probe_buffer.clear();
    return 0;
  }

  // Already bound — forward payload directly to session
  NPRPC_HTTP3_DEBUG(
      "conn={} wt_child_payload transport_stream_id={} session_stream_id={} binding={} datalen={}",
      debug_id(), stream_id, session_id,
      static_cast<int>(stream->webtransport_child_binding), datalen);
  auto session = get_or_create_webtransport_control_session(session_id);
  if (!session->process_bytes(stream_id, data, datalen)) {
    return -1;
  }
  return 0;
}

//==============================================================================
// Http3Server Implementation
//==============================================================================

#if !defined(_WIN32) && defined(NPRPC_HTTP3_REUSEPORT_BPF_ENABLED) && \
    defined(SO_ATTACH_REUSEPORT_EBPF)
#include "http3_quic_reuseport_bpf.inc"

namespace {

class Http3ReusePortBpfProgram
{
public:
  bool load(size_t worker_count, size_t scid_len)
  {
    struct Config {
      uint32_t worker_count;
      uint32_t scid_len;
    } config{static_cast<uint32_t>(worker_count), static_cast<uint32_t>(scid_len)};

    constexpr uint32_t key = 0;
    bpf_map* config_map = nullptr;
    bpf_program* program = nullptr;
    int rc = 0;

    object_.reset(bpf_object__open_mem(nprpc_http3_quic_reuseport_bpf_obj,
                                       nprpc_http3_quic_reuseport_bpf_obj_len,
                                       nullptr));
    if (!object_) {
      NPRPC_HTTP3_ERROR("Failed to open embedded HTTP/3 reuseport BPF object");
      return false;
    }

    rc = bpf_object__load(object_.get());
    if (rc != 0) {
      NPRPC_HTTP3_ERROR("Failed to load HTTP/3 reuseport BPF object: {}",
                        rc);
      return false;
    }

    program = bpf_object__find_program_by_name(object_.get(),
                                               "quic_select_reuseport");
    if (!program) {
      NPRPC_HTTP3_ERROR("Failed to locate HTTP/3 reuseport BPF program");
      return false;
    }

    prog_fd_ = bpf_program__fd(program);
    if (prog_fd_ < 0) {
      NPRPC_HTTP3_ERROR("Failed to acquire HTTP/3 reuseport BPF program fd");
      return false;
    }

    config_map = bpf_object__find_map_by_name(object_.get(), "config_map");
    if (!config_map) {
      NPRPC_HTTP3_ERROR("Failed to locate HTTP/3 reuseport BPF config map");
      return false;
    }

    rc = bpf_map_update_elem(bpf_map__fd(config_map), &key, &config, BPF_ANY);
    if (rc != 0) {
      NPRPC_HTTP3_ERROR("Failed to configure HTTP/3 reuseport BPF program: {}",
                        std::strerror(errno));
      return false;
    }

    auto* sockarray = bpf_object__find_map_by_name(object_.get(),
                                                    "reuseport_array");
    if (!sockarray) {
      NPRPC_HTTP3_ERROR("Failed to locate HTTP/3 reuseport BPF sockarray map");
      return false;
    }

    sockarray_fd_ = bpf_map__fd(sockarray);
    if (sockarray_fd_ < 0) {
      NPRPC_HTTP3_ERROR("Failed to acquire HTTP/3 reuseport BPF sockarray fd");
      return false;
    }

    return true;
  }

  bool register_socket(uint32_t index, int socket_fd)
  {
    if (sockarray_fd_ < 0) {
      return false;
    }

    uint64_t fd_val = static_cast<uint64_t>(socket_fd);
    int rc = bpf_map_update_elem(sockarray_fd_, &index, &fd_val, BPF_ANY);
    if (rc != 0) {
      NPRPC_HTTP3_ERROR(
          "Failed to register socket fd {} at index {} in reuseport BPF: {}",
          socket_fd, index, std::strerror(errno));
      return false;
    }
    return true;
  }

  bool attach(int socket_fd) const
  {
    if (prog_fd_ < 0) {
      return false;
    }

    if (::setsockopt(socket_fd, SOL_SOCKET, SO_ATTACH_REUSEPORT_EBPF, &prog_fd_,
                     sizeof(prog_fd_)) != 0) {
      NPRPC_HTTP3_ERROR("Failed to attach HTTP/3 reuseport BPF program: {}",
                        std::strerror(errno));
      return false;
    }

    return true;
  }

private:
  struct bpf_object_deleter {
    void operator()(bpf_object* object) const { bpf_object__close(object); }
  };

  std::unique_ptr<bpf_object, bpf_object_deleter> object_;
  int prog_fd_ = -1;
  int sockarray_fd_ = -1;
};

} // namespace
#endif

Http3Server::Http3Server(boost::asio::io_context& ioc,
                         const std::string& cert_file,
                         const std::string& key_file,
                         uint16_t port,
                         uint8_t worker_id)
    : ioc_(ioc)
    , cert_file_(cert_file)
    , key_file_(key_file)
    , port_(port)
    , worker_id_(worker_id)
    , socket_(ioc)
{
}

Http3Server::~Http3Server() { stop(); }

bool Http3Server::start()
{
  // Initialize static secret for tokens
  init_static_secret();

  // Create SSL context
  ssl_ctx_ = SSL_CTX_new(TLS_server_method());
  if (!ssl_ctx_) {
    NPRPC_HTTP3_ERROR("Failed to create SSL context: {}", ERR_error_string(ERR_get_error(), nullptr));
    return false;
  }

#if defined(OPENSSL_IS_BORINGSSL)
  // Configure the context for QUIC (sets QUIC method and TLS 1.3 on the ctx)
  if (ngtcp2_crypto_boringssl_configure_server_context(ssl_ctx_) != 0) {
    NPRPC_HTTP3_ERROR("Failed to configure SSL context for QUIC");
    SSL_CTX_free(ssl_ctx_);
    ssl_ctx_ = nullptr;
    return false;
  }
#endif

  // Set TLS 1.3 minimum (required for QUIC)
  SSL_CTX_set_min_proto_version(ssl_ctx_, TLS1_3_VERSION);
  SSL_CTX_set_max_proto_version(ssl_ctx_, TLS1_3_VERSION);

  // Disable 0-RTT. NPRPC serves mutable RPC and upgrade endpoints where
  // replayable early data is not an acceptable tradeoff.
#if defined(OPENSSL_IS_BORINGSSL)
  SSL_CTX_set_early_data_enabled(ssl_ctx_, 0);
#else
  SSL_CTX_set_max_early_data(ssl_ctx_, 0);
#endif

#if !defined(OPENSSL_IS_BORINGSSL)
  // SSL options (OpenSSL-specific flags; BoringSSL does not expose these)
  SSL_CTX_set_options(
      ssl_ctx_, (SSL_OP_ALL & ~SSL_OP_DONT_INSERT_EMPTY_FRAGMENTS) |
            SSL_OP_SINGLE_ECDH_USE | SSL_OP_CIPHER_SERVER_PREFERENCE);

  // Set ciphersuites for TLS 1.3 (BoringSSL does not expose this — built-in)
  if (SSL_CTX_set_ciphersuites(ssl_ctx_, crypto_default_ciphers()) != 1) {
    NPRPC_HTTP3_TRACE("Failed to set ciphersuites");
    SSL_CTX_free(ssl_ctx_);
    ssl_ctx_ = nullptr;
    return false;
  }

  SSL_CTX_set_mode(ssl_ctx_, SSL_MODE_RELEASE_BUFFERS);
#endif

  // Set groups for key exchange
  if (SSL_CTX_set1_groups_list(ssl_ctx_, crypto_default_groups()) != 1) {
    NPRPC_HTTP3_TRACE("Failed to set groups");
    SSL_CTX_free(ssl_ctx_);
    ssl_ctx_ = nullptr;
    return false;
  }

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

  if (effective_http3_worker_count() > 1) {
#if defined(SO_REUSEPORT)
    if (!enable_reuse_port(socket_, ec)) {
      NPRPC_HTTP3_ERROR("Failed to enable SO_REUSEPORT: {}",
                        ec.message());
      boost::system::error_code close_ec;
      socket_.close(close_ec);
      SSL_CTX_free(ssl_ctx_);
      ssl_ctx_ = nullptr;
      return false;
    }
#else
    NPRPC_HTTP3_ERROR(
        "HTTP/3 worker_count={} requires SO_REUSEPORT support on this platform",
        effective_http3_worker_count());
    boost::system::error_code close_ec;
    socket_.close(close_ec);
    SSL_CTX_free(ssl_ctx_);
    ssl_ctx_ = nullptr;
    return false;
#endif
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

  running_.store(true, std::memory_order_release);

  NPRPC_HTTP3_TRACE("Server listening on port {} (nghttp3/ngtcp2 backend)",
                    port_);

  // Open SHM rings created by npquicrouter (if configured).
  if (!g_cfg.shm_egress_channel.empty()) {
    const auto egress_name =
        nprpc::impl::make_shm_name(g_cfg.shm_egress_channel, "s2c");
    // Retry a few times — npquicrouter may start slightly after nprpc.
    for (int attempt = 0; attempt < 50; ++attempt) {
      try {
        egress_ring_  = nprpc::impl::LockFreeRingBuffer::open(egress_name);
        NPRPC_LOG_INFO("Http3Server[{}]: opened SHM egress '{}'",
                       worker_id_, egress_name);
        break;
      } catch (...) {
        if (attempt == 49) {
          NPRPC_LOG_WARN(
              "Http3Server[{}]: SHM egress ring '{}' not found after retries — "
              "falling back to direct sendmsg",
              worker_id_, egress_name);
        } else {
          struct timespec ts = {0, 100'000'000}; // 100 ms
          nanosleep(&ts, nullptr);
        }
      }
    }
  }

  // SHM ingress (c2s ring) is owned by Http3ServerRuntime — a single reader
  // thread dispatches packets to each worker's ioc_.  Individual workers only
  // use async_receive_from when NOT in SHM mode.
  if (!egress_ring_) {
    // UDP ingress mode: standard async_receive_from loop.
    do_receive();
  }
  // (In SHM mode Http3ServerRuntime::start() will call do_receive() for us
  //  once the ingress dispatch thread is running, if needed. Currently the
  //  socket is kept open for egress sendmsg fallback only.)

  return true;
}

void Http3Server::stop()
{
  if (!running_.exchange(false, std::memory_order_acq_rel)) {
    return;
  }

  // Graceful shutdown: send HTTP/3 GOAWAY to every open connection so clients
  // know which requests were accepted and can retry any that weren't.
  // We dispatch onto the io_context (not the individual strands) because stop()
  // is called from outside; each initiate_shutdown() posts to its own strand
  // internally via on_write/start_closing_period.
  {
    std::promise<void> shutdown_notices_sent;
    auto wait_for_notices = shutdown_notices_sent.get_future();
    boost::asio::dispatch(ioc_, [this, &shutdown_notices_sent]() {
      for (auto& [key, conn] : connections_) {
        conn->initiate_shutdown();
      }
      shutdown_notices_sent.set_value();
    });
    wait_for_notices.wait();
  }

  boost::system::error_code ec;
  socket_.close(ec);

  std::promise<void> connections_cleared;
  auto wait_for_clear = connections_cleared.get_future();
  boost::asio::dispatch(ioc_, [this, &connections_cleared]() {
    connections_.clear();
    send_inbox_head_ = nullptr;
    send_in_progress_ = false;
    connections_cleared.set_value();
  });
  wait_for_clear.wait();

  if (ssl_ctx_) {
    SSL_CTX_free(ssl_ctx_);
    ssl_ctx_ = nullptr;
  }
}

int Http3Server::send_packet(const boost::asio::ip::udp::endpoint& remote_ep,
                             const uint8_t* data,
                             size_t len)
{
  if (!running_.load(std::memory_order_acquire)) {
    return -1;
  }

  if (len > MAX_UDP_PAYLOAD_SIZE) {
    NPRPC_HTTP3_ERROR("send_packet payload exceeds MAX_UDP_PAYLOAD_SIZE: {} > {}",
                      len, MAX_UDP_PAYLOAD_SIZE);
    return -1;
  }

  auto packet = acquire_send_packet();
  packet->remote_ep = remote_ep;
  packet->payload_len = len;
  packet->gso_segment_size = static_cast<std::uint16_t>(len);
  if (len != 0) {
    std::memcpy(packet->payload_data(), data, len);
  }

  return send_packet(std::move(packet));
}

int Http3Server::send_packet(PendingSendPacket* packet)
{
  if (!running_.load(std::memory_order_acquire) || !packet) {
    return -1;
  }

  packet->next_send = send_inbox_head_;
  send_inbox_head_ = packet;

  if (!send_in_progress_) {
    send_in_progress_ = true;
    boost::asio::dispatch(ioc_, [this]() { do_send(); });
  }

  return 0;
}

PendingSendPacket* Http3Server::acquire_send_packet()
{
  if (auto* head = send_pool_head_) {
    send_pool_head_ = head->next_free;
    head->next_free = nullptr;
    head->next_send = nullptr;
    return head;
  }

  std::lock_guard lock(send_storage_mutex_);
  auto packet_slab = std::make_unique<PendingSendPacket[]>(SEND_PACKET_SLAB_SIZE);
  auto payload_slab =
      std::make_unique<PendingSendPacketPayload[]>(SEND_PACKET_SLAB_SIZE);

  for (std::size_t index = 0; index < SEND_PACKET_SLAB_SIZE; ++index) {
    packet_slab[index].payload = payload_slab[index].bytes.data();
  }

  send_packet_storage_.push_back(std::move(packet_slab));
  send_packet_payload_storage_.push_back(std::move(payload_slab));

  auto* slab = send_packet_storage_.back().get();
  for (std::size_t index = 1; index < SEND_PACKET_SLAB_SIZE; ++index) {
    recycle_send_packet(&slab[index]);
  }

  return &slab[0];
}

void Http3Server::recycle_send_packet(PendingSendPacket* packet)
{
  if (!packet) {
    return;
  }

  packet->payload_len = 0;
  packet->gso_segment_size = 0;
  packet->next_send = nullptr;
  packet->next_free = send_pool_head_;
  send_pool_head_ = packet;
}

void Http3Server::drain_send_inbox()
{
  auto* list = std::exchange(send_inbox_head_, nullptr);
  if (!list) {
    return;
  }

  PendingSendPacket* reversed = nullptr;
    auto* tail = list;
    std::size_t drained = 0;
    while (list) {
    auto* next = list->next_send;
    list->next_send = reversed;
    reversed = list;
    list = next;
      ++drained;
  }

  if (send_ready_tail_) {
    send_ready_tail_->next_send = reversed;
  } else {
    send_ready_head_ = reversed;
  }
  send_ready_tail_ = tail;
  send_ready_size_ += drained;
}

bool Http3Server::try_send_gso_batch()
{
#if !defined(_WIN32) && defined(UDP_SEGMENT)
  if (!udp_gso_supported_) {
    return false;
  }

  drain_send_inbox();

  if (send_ready_size_ < 2) {
    return false;
  }

  auto* first = send_ready_head_;
  if (!first || first->payload_len == 0) {
    return false;
  }

  const auto segment_size = first->gso_segment_size;

  std::array<iovec, kMaxUdpGsoSegments> iov{};
  std::size_t batch_size = 0;
  std::size_t total_bytes = 0;
  auto* batch_tail = first;

  for (auto* candidate = first;
       batch_size < kMaxUdpGsoSegments && candidate;
       candidate = candidate->next_send) {
    if (candidate->payload_len == 0 ||
        candidate->gso_segment_size != segment_size ||
        candidate->remote_ep != first->remote_ep) {
      break;
    }

    iov[batch_size].iov_base = candidate->payload_data();
    iov[batch_size].iov_len = candidate->payload_len;
    total_bytes += candidate->payload_len;
    batch_tail = candidate;
    ++batch_size;
  }

  if (batch_size < 2) {
    return false;
  }

  std::array<unsigned char, CMSG_SPACE(sizeof(std::uint16_t))> control{};
  msghdr msg{};
  msg.msg_name = first->remote_ep.data();
  msg.msg_namelen = static_cast<socklen_t>(first->remote_ep.size());
  msg.msg_iov = iov.data();
  msg.msg_iovlen = batch_size;
  msg.msg_control = control.data();
  msg.msg_controllen = control.size();

  auto* cmsg = CMSG_FIRSTHDR(&msg);
  cmsg->cmsg_level = IPPROTO_UDP;
  cmsg->cmsg_type = UDP_SEGMENT;
  cmsg->cmsg_len = CMSG_LEN(sizeof(std::uint16_t));

  std::memcpy(CMSG_DATA(cmsg), &segment_size, sizeof(segment_size));

  const auto sent = ::sendmsg(socket_.native_handle(), &msg, MSG_DONTWAIT);
  if (sent < 0) {
    const auto err = errno;
    if (err == EAGAIN || err == EWOULDBLOCK || err == EINTR) {
      return false;
    }

    if (err == EINVAL || err == EOPNOTSUPP || err == ENOPROTOOPT ||
        err == ENOTSUP) {
      udp_gso_supported_ = false;
      NPRPC_HTTP3_TRACE("UDP GSO disabled after sendmsg failure: {}",
                        std::strerror(err));
      return false;
    }

    NPRPC_HTTP3_TRACE("UDP GSO sendmsg failed: {}", std::strerror(err));
    return false;
  }

  if (static_cast<std::size_t>(sent) != total_bytes) {
    NPRPC_HTTP3_TRACE("UDP GSO sendmsg short write: {} of {} bytes",
                      sent, total_bytes);
    return false;
  }

  send_ready_head_ = batch_tail->next_send;
  batch_tail->next_send = nullptr;
  send_ready_size_ -= batch_size;
  if (!send_ready_head_) {
    send_ready_tail_ = nullptr;
  }

  auto* packet = first;
  while (packet) {
    auto* next = packet->next_send;
    recycle_send_packet(packet);
    packet = next;
  }

  return true;
#else
  return false;
#endif
}

void Http3Server::send_batch(PendingSendPacket** packets, size_t count)
{
  if (!count || !running_.load(std::memory_order_acquire)) {
    return;
  }

#if !defined(_WIN32) && defined(UDP_SEGMENT)
  if (udp_gso_supported_ && count >= 2) {
    auto* first = packets[0];
    const auto segment_size = first->gso_segment_size;

    bool gso_ok = true;
    for (size_t i = 1; i < count; ++i) {
      if (packets[i]->remote_ep != first->remote_ep) {
        gso_ok = false;
        break;
      }
      // All packets except the last must equal the segment size;
      // the last packet may be shorter (runt).
      if (i < count - 1 && packets[i]->payload_len != segment_size) {
        gso_ok = false;
        break;
      }
    }

    if (gso_ok) {
      std::array<iovec, MAX_PKTS_BURST> iov{};
      size_t total_bytes = 0;
      for (size_t i = 0; i < count; ++i) {
        iov[i].iov_base = packets[i]->payload_data();
        iov[i].iov_len = packets[i]->payload_len;
        total_bytes += packets[i]->payload_len;
      }

      alignas(cmsghdr) unsigned char control[CMSG_SPACE(sizeof(std::uint16_t))]{};
      msghdr msg{};
      msg.msg_name = first->remote_ep.data();
      msg.msg_namelen = static_cast<socklen_t>(first->remote_ep.size());
      msg.msg_iov = iov.data();
      msg.msg_iovlen = count;
      msg.msg_control = control;
      msg.msg_controllen = sizeof(control);

      auto* cmsg = CMSG_FIRSTHDR(&msg);
      cmsg->cmsg_level = IPPROTO_UDP;
      cmsg->cmsg_type = UDP_SEGMENT;
      cmsg->cmsg_len = CMSG_LEN(sizeof(std::uint16_t));
      std::memcpy(CMSG_DATA(cmsg), &segment_size, sizeof(segment_size));

      const auto sent = ::sendmsg(socket_.native_handle(), &msg, MSG_DONTWAIT);
      if (sent >= 0 && static_cast<size_t>(sent) == total_bytes) {
        for (size_t i = 0; i < count; ++i) {
          recycle_send_packet(packets[i]);
        }
        return;
      }

      const auto err = errno;
      if (err == EINVAL || err == EOPNOTSUPP || err == ENOPROTOOPT ||
          err == ENOTSUP) {
        udp_gso_supported_ = false;
        NPRPC_HTTP3_TRACE("UDP GSO disabled after send_batch failure: {}",
                          std::strerror(err));
      }
      // Fall through to per-packet send
    }
  }
#endif

#if !defined(_WIN32)
  for (size_t i = 0; i < count; ++i) {
    auto* p = packets[i];
    const auto sent = ::sendto(
        socket_.native_handle(),
        p->payload_data(), p->payload_len,
        MSG_DONTWAIT,
        p->remote_ep.data(),
        static_cast<socklen_t>(p->remote_ep.size()));
    if (sent >= 0) {
      recycle_send_packet(p);
    } else {
      for (size_t j = i; j < count; ++j) {
        send_packet(packets[j]);
      }
      return;
    }
  }
#else
  for (size_t i = 0; i < count; ++i) {
    send_packet(packets[i]);
  }
#endif
}

void Http3Server::send_aggregated(
    const boost::asio::ip::udp::endpoint& remote_ep,
    const uint8_t* data, size_t len, size_t gso_size)
{
  if (!len || !running_.load(std::memory_order_acquire)) {
    return;
  }

#if !defined(_WIN32)
  // ── SHM egress path ──────────────────────────────────────────────────────
  // Write the payload + routing header to the ring buffer so npquicrouter can
  // forward it with a single sendmsg(GSO) call, preserving kernel batching.
  if (egress_ring_) {
    const size_t msg_size = sizeof(ShmEgressFrame) + len;
    auto rsv = egress_ring_->try_reserve_write(msg_size);
    if (rsv) {
      ShmEgressFrame hdr{};
      hdr.payload_len      = static_cast<uint32_t>(len);
      hdr.gso_segment_size = static_cast<uint16_t>(
          (gso_size && len > gso_size) ? gso_size : 0);
      const auto ep_size   = remote_ep.size();
      hdr.ep_len           = static_cast<uint8_t>(ep_size);
      std::memcpy(hdr.ep_storage, remote_ep.data(), ep_size);

      std::memcpy(rsv.data,                   &hdr, sizeof(hdr));
      std::memcpy(rsv.data + sizeof(hdr), data, len);
      egress_ring_->commit_write(rsv, msg_size);
      return;
    }
    // Ring full — fall through to direct sendmsg as best-effort.
    NPRPC_HTTP3_TRACE("send_aggregated: SHM ring full, falling back to sendmsg");
  }
  // ─────────────────────────────────────────────────────────────────────────

  iovec msg_iov{};
  msg_iov.iov_base = const_cast<uint8_t*>(data);
  msg_iov.iov_len = len;

  msghdr msg{};
  auto ep_data = remote_ep.data();
  msg.msg_name = const_cast<void*>(static_cast<const void*>(ep_data));
  msg.msg_namelen = static_cast<socklen_t>(remote_ep.size());
  msg.msg_iov = &msg_iov;
  msg.msg_iovlen = 1;

#ifdef UDP_SEGMENT
  alignas(cmsghdr) unsigned char control[CMSG_SPACE(sizeof(std::uint16_t))]{};
  if (gso_size && len > gso_size) {
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);

    auto* cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = IPPROTO_UDP;
    cmsg->cmsg_type = UDP_SEGMENT;
    cmsg->cmsg_len = CMSG_LEN(sizeof(std::uint16_t));
    auto seg = static_cast<std::uint16_t>(gso_size);
    std::memcpy(CMSG_DATA(cmsg), &seg, sizeof(seg));
  }
#endif

  const auto sent = ::sendmsg(socket_.native_handle(), &msg, MSG_DONTWAIT);
  if (sent >= 0) {
    return;
  }

  const auto err = errno;

#ifdef UDP_SEGMENT
  // GSO failed — fall back to per-packet sendto
  if (len > gso_size && gso_size > 0 &&
      (err == EINVAL || err == EIO || err == EOPNOTSUPP ||
       err == ENOPROTOOPT || err == ENOTSUP)) {
    NPRPC_HTTP3_TRACE("GSO sendmsg failed ({}), falling back to per-packet",
                      std::strerror(err));
    const uint8_t* p = data;
    size_t remaining = len;
    while (remaining > 0) {
      auto chunk = std::min(gso_size, remaining);
      ::sendto(socket_.native_handle(), p, chunk, MSG_DONTWAIT,
               ep_data, static_cast<socklen_t>(remote_ep.size()));
      p += chunk;
      remaining -= chunk;
    }
    return;
  }
#endif

  if (err != EAGAIN && err != EWOULDBLOCK) {
    NPRPC_HTTP3_TRACE("send_aggregated failed: {}", std::strerror(err));
  }
#else
  // Windows fallback: per-packet
  const uint8_t* p = data;
  size_t remaining = len;
  while (remaining > 0 && gso_size > 0) {
    auto chunk = std::min(gso_size, remaining);
    boost::system::error_code ec;
    socket_.send_to(boost::asio::buffer(p, chunk), remote_ep,
                    boost::asio::socket_base::message_flags{}, ec);
    p += chunk;
    remaining -= chunk;
  }
#endif
}

void Http3Server::do_send()
{
  drain_send_inbox();
  while (try_send_gso_batch()) {
    drain_send_inbox();
  }

  PendingSendPacket* packet = nullptr;
  if (!running_.load(std::memory_order_acquire)) {
    send_ready_head_ = nullptr;
    send_ready_tail_ = nullptr;
    send_ready_size_ = 0;
    send_inbox_head_ = nullptr;
    send_in_progress_ = false;
    return;
  }

  if (!send_ready_head_) {
    send_in_progress_ = false;
    drain_send_inbox();
    if (!send_ready_head_) {
      return;
    }
    send_in_progress_ = true;
  }

  packet = send_ready_head_;

  socket_.async_send_to(
      boost::asio::buffer(packet->bytes().data(), packet->bytes().size()),
      packet->remote_ep,
      [this, packet](boost::system::error_code ec, std::size_t) {
        if (send_ready_head_) {
          send_ready_head_ = send_ready_head_->next_send;
          if (!send_ready_head_) {
            send_ready_tail_ = nullptr;
          }
          if (send_ready_size_ != 0) {
            --send_ready_size_;
          }
        }

        if (!running_.load(std::memory_order_acquire) ||
            ec == boost::asio::error::operation_aborted) {
          send_ready_head_ = nullptr;
          send_ready_tail_ = nullptr;
          send_ready_size_ = 0;
          send_inbox_head_ = nullptr;
          send_in_progress_ = false;
          return;
        }

        if (ec) {
          NPRPC_HTTP3_TRACE("async_send_to failed: {}", ec.message());
        }

        recycle_send_packet(packet);

        drain_send_inbox();

        if (!send_ready_head_) {
          send_in_progress_ = false;
          drain_send_inbox();
          if (!send_ready_head_) {
            return;
          }
          send_in_progress_ = true;
        }

        do_send();
      });
}

void Http3Server::associate_cid(const ngtcp2_cid* cid,
                                std::shared_ptr<Http3Connection> conn)
{
  std::string key = cid_to_string(cid);
  connections_[key] = conn;
}

void Http3Server::dissociate_cid(const ngtcp2_cid* cid)
{
  std::string key = cid_to_string(cid);
  connections_.erase(key);
}

void Http3Server::remove_connection(std::shared_ptr<Http3Connection> conn)
{
  // std::lock_guard lock(mutex_);
  http_request_throttler().on_http3_connection_closed(
  conn->remote_endpoint().address());
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
  if (!running_.load(std::memory_order_acquire)) {
    return;
  }

  socket_.async_receive_from(
      boost::asio::buffer(recv_buf_), remote_ep_,
      [this](boost::system::error_code ec, std::size_t bytes_recvd) {
        if (!ec && bytes_recvd > 0) {
#if NPRPC_ENABLE_HTTP3_REUSEPORT_SANITY
          ++sanity_receive_callbacks_;
          ++sanity_received_packets_;
#endif
          handle_packet(remote_ep_, recv_buf_.data(), bytes_recvd);
        }

        if (running_.load(std::memory_order_acquire) &&
            (!ec || ec == boost::asio::error::message_size)) {
          do_receive();
        }
      });
}

// static
boost::asio::ip::udp::endpoint
Http3Server::decode_ingress_ep(const ShmIngressFrame& hdr)
{
  namespace ip = boost::asio::ip;
  if (hdr.ep_len == sizeof(sockaddr_in)) {
    sockaddr_in sin{};
    std::memcpy(&sin, hdr.ep_storage, sizeof(sin));
    return ip::udp::endpoint(
        ip::address_v4(ntohl(sin.sin_addr.s_addr)),
        ntohs(sin.sin_port));
  } else {
    sockaddr_in6 sin6{};
    std::memcpy(&sin6, hdr.ep_storage, sizeof(sin6));
    ip::address_v6::bytes_type bytes{};
    std::memcpy(bytes.data(), sin6.sin6_addr.s6_addr, 16);
    return ip::udp::endpoint(
        ip::address_v6(bytes, sin6.sin6_scope_id),
        ntohs(sin6.sin6_port));
  }
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
#if NPRPC_ENABLE_HTTP3_REUSEPORT_SANITY
    ++sanity_established_packets_;
#endif
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

#if NPRPC_ENABLE_HTTP3_REUSEPORT_SANITY
  ++sanity_initial_packets_;
#endif

  // Validate QUIC version
  if (!ngtcp2_is_supported_version(hd.version)) {
    send_version_negotiation(remote_ep, vc);
    return;
  }

  ngtcp2_cid scid;
  scid.datalen = vc.scidlen;
  memcpy(scid.data, vc.scid, vc.scidlen);

  // Create new connection
  if (!allow_new_connection(remote_ep)) {
    NPRPC_HTTP3_ERROR("Dropping throttled QUIC connection attempt from {}",
                      remote_ep.address().to_string());
    return;
  }

  // Note: client_scid is the client's Source CID (from hd.scid/vc.scid)
  //       client_dcid is what client sent as Destination CID (from
  //       hd.dcid/vc.dcid)

  NPRPC_HTTP3_TRACE(
      "Accepting new connection from {}, client DCID={}, client SCID={}",
      remote_ep.address().to_string(), cid_to_hex(&dcid),
      cid_to_hex(&scid));

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
  on_connection_accepted(remote_ep);

  NPRPC_HTTP3_TRACE("New connection from {}", remote_ep.address().to_string());

  // Feed the initial packet
  ngtcp2_pkt_info pi{};
  conn->enqueue_packet(copy_bytes_to_buffer(data, len), pi);
}

bool Http3Server::allow_new_connection(
    const boost::asio::ip::udp::endpoint& remote_ep)
{
  return http_request_throttler().allow_http3_new_connection(
    remote_ep.address(),
      g_cfg.http3_max_active_connections_per_ip,
      g_cfg.http3_max_new_connections_per_ip_per_second,
      g_cfg.http3_max_new_connections_burst);
}

void Http3Server::on_connection_accepted(
    const boost::asio::ip::udp::endpoint& remote_ep)
{
  http_request_throttler().on_http3_connection_accepted(
      remote_ep.address());
}

bool Http3Server::allow_rpc_request(const boost::asio::ip::udp::endpoint& remote_ep)
{
  return http_request_throttler().allow_http_rpc_request(
      remote_ep.address(),
      g_cfg.http_rpc_max_requests_per_ip_per_second,
      g_cfg.http_rpc_max_requests_burst);
}

bool Http3Server::allow_webtransport_connect(
    const boost::asio::ip::udp::endpoint& remote_ep)
{
  return http_request_throttler().allow_webtransport_connect(
    remote_ep.address(),
      g_cfg.http_webtransport_connects_per_ip_per_second,
      g_cfg.http_webtransport_connects_burst);
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

  auto it = connections_.find(key);
  return it != connections_.end() ? it->second : nullptr;
}

class Http3ServerRuntime
{
public:
  ~Http3ServerRuntime() { stop(); }

  bool start(const std::string& cert_file,
             const std::string& key_file,
             uint16_t port)
  {
    const auto worker_count = effective_http3_worker_count();

    if (!can_embed_http3_worker_id(worker_count)) {
      NPRPC_HTTP3_ERROR(
          "HTTP/3 worker_count={} exceeds embedded worker ID range 0..{}",
          worker_count, MAX_HTTP3_EMBEDDED_WORKER_ID);
      return false;
    }

    workers_.reserve(worker_count);

    for (size_t index = 0; index < worker_count; ++index) {
      auto worker =
          std::make_unique<Worker>(cert_file, key_file, port,
                                   static_cast<uint8_t>(index));
      if (!worker->server->start()) {
        NPRPC_HTTP3_ERROR("Failed to start HTTP/3 worker {}/{}", index + 1,
                          worker_count);
        stop();
        return false;
      }
      workers_.push_back(std::move(worker));
    }

#if !defined(_WIN32) && defined(NPRPC_HTTP3_REUSEPORT_BPF_ENABLED) && \
    defined(SO_ATTACH_REUSEPORT_EBPF)
    if (worker_count > 1) {
      reuseport_bpf_ = std::make_unique<Http3ReusePortBpfProgram>();
      if (!reuseport_bpf_->load(worker_count, SCID_LEN)) {
        stop();
        return false;
      }

      for (size_t i = 0; i < worker_count; ++i) {
        int fd = workers_[i]->server->socket().native_handle();
        if (!reuseport_bpf_->register_socket(static_cast<uint32_t>(i), fd)) {
          stop();
          return false;
        }
      }

      if (!reuseport_bpf_->attach(
              workers_.front()->server->socket().native_handle())) {
        stop();
        return false;
      }
    }
#endif

    for (auto& worker : workers_) {
      worker->thread = std::thread([ioc = &worker->ioc, worker_id = worker->server->worker_id()] {
        nprpc::impl::set_thread_name("h3_" + std::to_string(worker_id));
        ioc->run();
      });
    }

    NPRPC_LOG_INFO("[HTTP/3] Started {} dedicated worker(s) on UDP port {}",
                   worker_count, port);

    // Start the single SHM ingress reader (if configured).
    if (!g_cfg.shm_ingress_channel.empty()) {
      const auto ingress_name =
          nprpc::impl::make_shm_name(g_cfg.shm_ingress_channel, "c2s");
      for (int attempt = 0; attempt < 50; ++attempt) {
        try {
          ingress_ring_ = nprpc::impl::LockFreeRingBuffer::open(ingress_name);
          NPRPC_LOG_INFO("[HTTP/3] Opened SHM ingress ring '{}'", ingress_name);
          break;
        } catch (...) {
          if (attempt == 49) {
            NPRPC_LOG_WARN("[HTTP/3] SHM ingress ring '{}' not found — "
                           "falling back to UDP receive", ingress_name);
          } else {
            struct timespec ts = {0, 100'000'000};
            nanosleep(&ts, nullptr);
          }
        }
      }
      if (ingress_ring_) {
        ingress_running_.store(true, std::memory_order_release);
        ingress_thread_ = std::thread(&Http3ServerRuntime::ingress_loop_impl, this);
      }
    }

    return true;
  }

  void stop() noexcept
  {
    // Stop ingress reader before shutting down workers so no new packets
    // are posted after workers' connections_ are cleared.
    if (ingress_running_.exchange(false, std::memory_order_acq_rel)) {
      if (ingress_thread_.joinable()) ingress_thread_.join();
    }
    ingress_ring_.reset();

    for (auto& worker : workers_) {
      if (worker->server) {
        worker->server->stop();
      }
    }

    for (auto& worker : workers_) {
      worker->work_guard.reset();
      worker->ioc.stop();
    }

    for (auto& worker : workers_) {
      if (worker->thread.joinable()) {
        worker->thread.join();
      }
    }

#if NPRPC_ENABLE_HTTP3_REUSEPORT_SANITY
    log_reuseport_sanity_report();
#endif

    workers_.clear();
#if !defined(_WIN32) && defined(NPRPC_HTTP3_REUSEPORT_BPF_ENABLED) && \
  defined(SO_ATTACH_REUSEPORT_EBPF)
    reuseport_bpf_.reset();
#endif
  }

private:
#if NPRPC_ENABLE_HTTP3_REUSEPORT_SANITY
  void log_reuseport_sanity_report() const
  {
    if (workers_.empty()) {
      return;
    }

    std::vector<Http3ReusePortSanitySnapshot> snapshots;
    snapshots.reserve(workers_.size());

    uint64_t total_receive_callbacks = 0;
    uint64_t total_initial_packets = 0;
    uint64_t max_receive_callbacks = 0;
    uint64_t min_receive_callbacks = std::numeric_limits<uint64_t>::max();

    for (const auto& worker : workers_) {
      auto snapshot = worker->server->reuseport_sanity_snapshot();
      total_receive_callbacks += snapshot.receive_callbacks;
      total_initial_packets += snapshot.initial_packets;
      max_receive_callbacks =
          std::max(max_receive_callbacks, snapshot.receive_callbacks);
      min_receive_callbacks =
          std::min(min_receive_callbacks, snapshot.receive_callbacks);
      snapshots.push_back(snapshot);
    }

    NPRPC_LOG_ERROR(
        "[HTTP/3][SANITY] reuseport totals receive_callbacks={} initial_packets={} workers={}",
        total_receive_callbacks, total_initial_packets, snapshots.size());

    for (const auto& snapshot : snapshots) {
      const double receive_share =
          total_receive_callbacks == 0
              ? 0.0
              : (100.0 * static_cast<double>(snapshot.receive_callbacks) /
                 static_cast<double>(total_receive_callbacks));
      const double initial_share =
          total_initial_packets == 0
              ? 0.0
              : (100.0 * static_cast<double>(snapshot.initial_packets) /
                 static_cast<double>(total_initial_packets));

      NPRPC_LOG_ERROR(
          "[HTTP/3][SANITY] worker={} receive_callbacks={} ({:.1f}%) received_packets={} initial_packets={} ({:.1f}%) established_packets={}",
          snapshot.worker_id, snapshot.receive_callbacks, receive_share,
          snapshot.received_packets, snapshot.initial_packets, initial_share,
          snapshot.established_packets);
    }

    if (snapshots.size() > 1 && total_receive_callbacks >= snapshots.size()) {
      const double imbalance_ratio =
          min_receive_callbacks == 0
              ? std::numeric_limits<double>::infinity()
              : static_cast<double>(max_receive_callbacks) /
                    static_cast<double>(min_receive_callbacks);

      if (min_receive_callbacks == 0 || imbalance_ratio > 2.0) {
        NPRPC_LOG_ERROR(
            "[HTTP/3][SANITY] suspicious receive imbalance detected: min_callbacks={} max_callbacks={} ratio={:.2f}",
            min_receive_callbacks, max_receive_callbacks, imbalance_ratio);
      }
    }
  }
#endif

  struct Worker {
    explicit Worker(const std::string& cert_file,
                    const std::string& key_file,
                    uint16_t port,
                    uint8_t worker_id)
        : work_guard(boost::asio::make_work_guard(ioc))
        , server(std::make_unique<Http3Server>(ioc, cert_file, key_file, port,
                                               worker_id))
    {
    }

    boost::asio::io_context ioc;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type>
        work_guard;
    std::unique_ptr<Http3Server> server;
    std::thread thread;
  };

  std::vector<std::unique_ptr<Worker>> workers_;
#if !defined(_WIN32) && defined(NPRPC_HTTP3_REUSEPORT_BPF_ENABLED) && \
    defined(SO_ATTACH_REUSEPORT_EBPF)
  std::unique_ptr<Http3ReusePortBpfProgram> reuseport_bpf_;
#endif

  // Single SHM ingress reader — replaces per-worker ingress_thread_.
  // Reads the c2s ring, parses DCID, and dispatches each packet to the
  // correct worker's ioc_ so that handle_packet() always runs on the
  // thread that owns the corresponding Http3Connection.
  std::unique_ptr<nprpc::impl::LockFreeRingBuffer> ingress_ring_;
  std::thread                                      ingress_thread_;
  std::atomic<bool>                                ingress_running_{false};

  // Dispatch a packet payload to the correct worker by DCID routing,
  // mirroring the BPF program's logic:
  //   short-header: pkt[1] = dcid[0] = embedded worker_id
  //   long-header:  FNV-1a of first 4 DCID bytes % worker_count
  void dispatch_ingress_packet(const boost::asio::ip::udp::endpoint& ep,
                               std::vector<uint8_t> pkt)
  {
    const size_t n = workers_.size();
    size_t target  = 0;

    if (n > 1 && pkt.size() >= 2) {
      if (pkt[0] & 0x80) {
        // Long-header: DCID starts at byte 6 for QUIC v1
        // Layout: first_byte(1) + version(4) + dcid_len(1) + dcid(...)
        if (pkt.size() >= 6) {
          const uint8_t dcid_len = pkt[5];
          if (dcid_len >= 4 && pkt.size() >= size_t(6 + dcid_len)) {
            // FNV-1a over first 4 DCID bytes (matches BPF program)
            uint32_t h = 2166136261u;
            for (int i = 0; i < 4; ++i) {
              h ^= pkt[6 + i];
              h *= 16777619u;
            }
            target = h % n;
          }
        }
      } else {
        // Short-header: pkt[1] = dcid[0] = worker_id
        target = pkt[1] % n;
      }
    }

    auto* server = workers_[target]->server.get();
    boost::asio::post(workers_[target]->ioc,
                      [server, ep, pkt = std::move(pkt)]() mutable {
                        if (!server->running()) return;
                        server->handle_packet(ep, pkt.data(), pkt.size());
                      });
  }

  void ingress_loop_impl()
  {
    nprpc::impl::set_thread_name("h3_ingress");
    while (ingress_running_.load(std::memory_order_acquire)) {
      auto view = ingress_ring_->try_read_view();
      if (!view) {
        struct timespec ts = {0, 50'000}; // 50 µs
        nanosleep(&ts, nullptr);
        continue;
      }

      if (view.size < sizeof(ShmIngressFrame)) {
        ingress_ring_->commit_read(view);
        continue;
      }

      ShmIngressFrame hdr{};
      std::memcpy(&hdr, view.data, sizeof(hdr));

      if (view.size < sizeof(ShmIngressFrame) + hdr.payload_len ||
          hdr.ep_len == 0 || hdr.ep_len > sizeof(hdr.ep_storage)) {
        ingress_ring_->commit_read(view);
        continue;
      }

      // Copy payload before releasing the ring slot.
      std::vector<uint8_t> pkt(
          view.data + sizeof(ShmIngressFrame),
          view.data + sizeof(ShmIngressFrame) + hdr.payload_len);
      const auto ep = Http3Server::decode_ingress_ep(hdr);
      ingress_ring_->commit_read(view);

      dispatch_ingress_packet(ep, std::move(pkt));
    }
  }
}; // end Http3ServerRuntime

//==============================================================================
// Global HTTP/3 Server Instance
//==============================================================================

static std::unique_ptr<Http3ServerRuntime> g_http3_server;

NPRPC_API void init_http3_server(boost::asio::io_context& ioc)
{
  (void)ioc;

  if (!g_cfg.http3_enabled)
    return;

  NPRPC_HTTP3_TRACE("Initializing on port {} (nghttp3 backend)",
                    g_cfg.listen_http_port);

  g_http3_server = std::make_unique<Http3ServerRuntime>();

  if (!g_http3_server->start(g_cfg.http_cert_file, g_cfg.http_key_file,
                             g_cfg.listen_http_port)) {
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

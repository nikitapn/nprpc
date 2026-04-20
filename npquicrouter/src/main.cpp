// npquicrouter — SNI-based QUIC + TLS reverse proxy
//
// Routes TLS (TCP) and QUIC (UDP) connections to different backend services
// based on the SNI hostname in the TLS ClientHello.
//
// TCP: accept → peek TLS ClientHello → extract SNI → TCP splice to backend
// UDP: recvfrom → decrypt QUIC v1 Initial → extract SNI → per-session forward

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <span>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>

#include <glaze/json.hpp>

#include "quic_initial.hpp"
#include "quic_shm_channel.hpp"
#include "sni_parser.hpp"

#if defined(NPRPC_ROUTER_REUSEPORT_BPF_ENABLED) && defined(SO_ATTACH_REUSEPORT_EBPF)
# include <bpf/bpf.h>
# include <bpf/libbpf.h>
# include "quic_reuseport_router_bpf.inc"
#endif

#define QUIC_DBG_PLAIN(msg) do { if (quic_debug_enabled) std::cerr << "[QUIC]" << msg << "\n"; } while(0)

namespace asio = boost::asio;
using tcp = asio::ip::tcp;
using udp = asio::ip::udp;

// Must match SCID_LEN in http3_server_nghttp3.cpp.
// The backend embeds worker_id in cid->data[0] of every SCID it generates,
// enabling the router BPF to route 1-RTT short-header packets by data[1].
static constexpr size_t kQuicServerScidLen = 18;

//==============================================================================
// Config
//==============================================================================

struct RouteEntry {
    std::string sni;
    std::string tcp_backend; // "ip:port"
    std::string udp_backend; // "ip:port"
};

struct Config {
    std::string  listen_address        = "0.0.0.0";
    uint16_t     listen_tcp_port       = 443;
    uint16_t     listen_udp_port       = 443;
    uint16_t     http_redirect_port    = 0;    // 0 = disabled; 80 = redirect HTTP→HTTPS
    std::string  default_tcp_backend;  // fallback if no SNI match
    std::string  default_udp_backend;
    int          udp_session_timeout_sec = 120;
    std::vector<RouteEntry> routes;
    // SHM egress channel name (empty = disabled).
    // When set, npquicrouter creates a LockFreeRingBuffer named
    // /nprpc_<shm_egress_channel>_s2c that Http3Server writes GSO batches
    // into.  npquicrouter drains the ring and calls sendmsg(GSO) to preserve
    // batching across the double-UDP-hop.
    std::string  shm_egress_channel;
    int          num_workers          = 1;     // UDP worker threads (1 = single-threaded)
};

// Glaze reflection
template <>
struct glz::meta<RouteEntry> {
    using T = RouteEntry;
    static constexpr auto value = glz::object(
        "sni",         &T::sni,
        "tcp_backend", &T::tcp_backend,
        "udp_backend", &T::udp_backend);
};

template <>
struct glz::meta<Config> {
    using T = Config;
    static constexpr auto value = glz::object(
        "listen_address",          &T::listen_address,
        "listen_tcp_port",         &T::listen_tcp_port,
        "listen_udp_port",         &T::listen_udp_port,
        "http_redirect_port",      &T::http_redirect_port,
        "default_tcp_backend",     &T::default_tcp_backend,
        "default_udp_backend",     &T::default_udp_backend,
        "udp_session_timeout_sec", &T::udp_session_timeout_sec,
        "routes",                  &T::routes,
        "shm_egress_channel",      &T::shm_egress_channel,
        "num_workers",             &T::num_workers);
};

//==============================================================================
// Helpers
//==============================================================================

// Parse "host:port" into an endpoint. Throws on error.
static tcp::endpoint parse_tcp_endpoint(const std::string& s,
                                        asio::io_context& ioc)
{
    const auto colon = s.rfind(':');
    if (colon == std::string::npos)
        throw std::runtime_error("invalid backend address (no port): " + s);
    const std::string host = s.substr(0, colon);
    const std::string port = s.substr(colon + 1);
    tcp::resolver resolver(ioc);
    auto results = resolver.resolve(host, port);
    return results.begin()->endpoint();
}

static udp::endpoint parse_udp_endpoint(const std::string& s,
                                        asio::io_context& ioc)
{
    const auto colon = s.rfind(':');
    if (colon == std::string::npos)
        throw std::runtime_error("invalid backend address (no port): " + s);
    const std::string host = s.substr(0, colon);
    const std::string port = s.substr(colon + 1);
    udp::resolver resolver(ioc);
    auto results = resolver.resolve(host, port);
    return results.begin()->endpoint();
}

//==============================================================================
// Route table
//==============================================================================

struct ResolvedRoute {
    std::string     sni;
    tcp::endpoint   tcp_ep;
    udp::endpoint   udp_ep;
    bool            has_tcp = false;
    bool            has_udp = false;
};

struct RouteTable {
    std::vector<ResolvedRoute> routes;
    tcp::endpoint              default_tcp;
    udp::endpoint              default_udp;
    bool                       has_default_tcp = false;
    bool                       has_default_udp = false;

    const tcp::endpoint* lookup_tcp(std::string_view sni) const noexcept {
        for (const auto& r : routes)
            if (r.has_tcp && r.sni == sni) return &r.tcp_ep;
        return has_default_tcp ? &default_tcp : nullptr;
    }

    const udp::endpoint* lookup_udp(std::string_view sni) const noexcept {
        for (const auto& r : routes)
            if (r.has_udp && r.sni == sni) return &r.udp_ep;
        return has_default_udp ? &default_udp : nullptr;
    }
};

static RouteTable build_route_table(const Config& cfg, asio::io_context& ioc)
{
    RouteTable rt;
    for (const auto& r : cfg.routes) {
        ResolvedRoute rr;
        rr.sni = r.sni;
        if (!r.tcp_backend.empty()) {
            rr.tcp_ep  = parse_tcp_endpoint(r.tcp_backend, ioc);
            rr.has_tcp = true;
        }
        if (!r.udp_backend.empty()) {
            rr.udp_ep  = parse_udp_endpoint(r.udp_backend, ioc);
            rr.has_udp = true;
        }
        rt.routes.push_back(std::move(rr));
    }
    if (!cfg.default_tcp_backend.empty()) {
        rt.default_tcp     = parse_tcp_endpoint(cfg.default_tcp_backend, ioc);
        rt.has_default_tcp = true;
    }
    if (!cfg.default_udp_backend.empty()) {
        rt.default_udp     = parse_udp_endpoint(cfg.default_udp_backend, ioc);
        rt.has_default_udp = true;
    }
    return rt;
}

//==============================================================================
// QuicRouterReusePortBpf — optional SO_REUSEPORT eBPF dispatcher
//==============================================================================

#if defined(NPRPC_ROUTER_REUSEPORT_BPF_ENABLED) && defined(SO_ATTACH_REUSEPORT_EBPF)

class QuicRouterReusePortBpf
{
public:
  // Load and configure the BPF program for worker_count sockets.
  // scid_len must match the server's SCID_LEN (18); controls short-header routing.
  bool load(size_t worker_count, size_t scid_len)
  {
    const uint32_t key = 0;
    struct { uint32_t worker_count; uint32_t scid_len; }
        cfg_val{ static_cast<uint32_t>(worker_count),
                 static_cast<uint32_t>(scid_len) };

    object_.reset(bpf_object__open_mem(nprpc_quic_reuseport_router_bpf_obj,
                                       nprpc_quic_reuseport_router_bpf_obj_len,
                                       nullptr));
    if (!object_) {
      std::cerr << "[BPF] Failed to open router reuseport BPF object\n";
      return false;
    }

    if (bpf_object__load(object_.get()) != 0) {
      std::cerr << "[BPF] Failed to load router reuseport BPF object\n";
      return false;
    }

    auto* prog = bpf_object__find_program_by_name(object_.get(),
                                                  "quic_select_reuseport");
    if (!prog) {
      std::cerr << "[BPF] Program 'quic_select_reuseport' not found\n";
      return false;
    }

    prog_fd_ = bpf_program__fd(prog);
    if (prog_fd_ < 0) {
      std::cerr << "[BPF] Invalid program fd\n";
      return false;
    }

    auto* config_map = bpf_object__find_map_by_name(object_.get(), "config_map");
    if (!config_map) {
      std::cerr << "[BPF] config_map not found\n";
      return false;
    }

    if (bpf_map_update_elem(bpf_map__fd(config_map), &key, &cfg_val, BPF_ANY) != 0) {
      std::cerr << "[BPF] Failed to set config: "
                << std::strerror(errno) << "\n";
      return false;
    }

    auto* sockarray = bpf_object__find_map_by_name(object_.get(),
                                                   "reuseport_array");
    if (!sockarray) {
      std::cerr << "[BPF] reuseport_array not found\n";
      return false;
    }

    sockarray_fd_ = bpf_map__fd(sockarray);
    if (sockarray_fd_ < 0) {
      std::cerr << "[BPF] Invalid sockarray fd\n";
      return false;
    }

    auto* wc_map = bpf_object__find_map_by_name(object_.get(), "worker_counters");
    if (wc_map) worker_counters_fd_ = bpf_map__fd(wc_map);

    auto* dc_map = bpf_object__find_map_by_name(object_.get(), "drop_counter");
    if (dc_map) drop_counter_fd_ = bpf_map__fd(dc_map);

    return true;
  }

  // Print per-worker packet distribution and drop count to stdout.
  // Reads PERCPU_ARRAY maps and sums across all CPUs.
  void print_metrics(size_t worker_count) const
  {
    int ncpus = libbpf_num_possible_cpus();
    if (ncpus <= 0 || worker_counters_fd_ < 0) return;

    std::vector<uint64_t> per_cpu(static_cast<size_t>(ncpus));
    uint64_t total = 0;

    std::cout << "[BPF] Router packet distribution:\n";
    for (uint32_t i = 0; i < static_cast<uint32_t>(worker_count); ++i) {
      if (bpf_map_lookup_elem(worker_counters_fd_, &i, per_cpu.data()) != 0)
        continue;
      uint64_t sum = 0;
      for (int c = 0; c < ncpus; ++c) sum += per_cpu[c];
      total += sum;
      std::cout << "  worker[" << i << "]: " << sum << "\n";
    }
    std::cout << "  total routed: " << total << "\n";

    if (drop_counter_fd_ >= 0) {
      const uint32_t key = 0;
      if (bpf_map_lookup_elem(drop_counter_fd_, &key, per_cpu.data()) == 0) {
        uint64_t drops = 0;
        for (int c = 0; c < ncpus; ++c) drops += per_cpu[c];
        std::cout << "  dropped:      " << drops << "\n";
      }
    }
  }

  // Register a worker socket fd at the given index in the reuseport array.
  bool register_socket(uint32_t index, int socket_fd)
  {
    if (sockarray_fd_ < 0) return false;
    uint64_t fd_val = static_cast<uint64_t>(socket_fd);
    if (bpf_map_update_elem(sockarray_fd_, &index, &fd_val, BPF_ANY) != 0) {
      std::cerr << "[BPF] Failed to register socket " << socket_fd
                << " at index " << index << ": "
                << std::strerror(errno) << "\n";
      return false;
    }
    return true;
  }

  // Attach the BPF program to the first socket in the reuseport group.
  // Only needs to be called once; the kernel applies it to the whole group.
  bool attach(int socket_fd) const
  {
    if (prog_fd_ < 0) return false;
    if (::setsockopt(socket_fd, SOL_SOCKET, SO_ATTACH_REUSEPORT_EBPF,
                     &prog_fd_, sizeof(prog_fd_)) != 0) {
      std::cerr << "[BPF] Failed to attach: " << std::strerror(errno) << "\n";
      return false;
    }
    return true;
  }

private:
  struct BpfObjectDeleter {
    void operator()(bpf_object* o) const { bpf_object__close(o); }
  };

  std::unique_ptr<bpf_object, BpfObjectDeleter> object_;
  int prog_fd_          = -1;
  int sockarray_fd_     = -1;
  int worker_counters_fd_ = -1;
  int drop_counter_fd_  = -1;
};

#endif // NPRPC_ROUTER_REUSEPORT_BPF_ENABLED

//==============================================================================
// TCP proxy — one object per accepted connection
//==============================================================================

class TcpSession : public std::enable_shared_from_this<TcpSession> {
    static constexpr size_t kPeekBufSize = 2048; // covers TLS 1.3 ClientHellos up to ~900 bytes
    static constexpr size_t kSpliceBufSize = 32768;

    tcp::socket client_;
    tcp::socket backend_;
    const RouteTable& routes_;

    std::array<uint8_t, kPeekBufSize>   peek_buf_{};
    size_t                               peek_len_ = 0;

    // Splice buffers — allocated only after handshake
    std::vector<uint8_t> c2b_buf_; // client → backend
    std::vector<uint8_t> b2c_buf_; // backend → client

public:
    TcpSession(tcp::socket client, const RouteTable& routes)
        : client_(std::move(client))
        , backend_(client_.get_executor())
        , routes_(routes)
    {}

    void start() {
        // Read enough to capture a TLS ClientHello (typically < 300 bytes)
        client_.async_read_some(
            asio::buffer(peek_buf_),
            [self = shared_from_this()](boost::system::error_code ec, size_t n) {
                if (ec) return;
                self->peek_len_ = n;
                self->on_peek();
            });
    }

private:
    void on_peek() {
        const auto span = std::span<const uint8_t>(peek_buf_.data(), peek_len_);
        const std::string_view sni = sni_from_tls_record(span);

        const tcp::endpoint* ep = routes_.lookup_tcp(sni);
        if (!ep) {
            std::cerr << "[TCP] No route for SNI '" << sni
                      << "' — dropping connection\n";
            return;
        }
        if (!sni.empty())
            std::clog << "[TCP] SNI='" << sni << "' → " << *ep << "\n";

        backend_.async_connect(
            *ep,
            [self = shared_from_this(), ep_str = ep->address().to_string()]
            (boost::system::error_code ec) {
                if (ec) {
                    std::cerr << "[TCP] connect to backend failed: "
                              << ec.message() << "\n";
                    return;
                }
                self->splice_start();
            });
    }

    void splice_start() {
        c2b_buf_.resize(kSpliceBufSize);
        b2c_buf_.resize(kSpliceBufSize);

        // Send peeked bytes to backend first, then enter bidirectional splice
        asio::async_write(
            backend_,
            asio::buffer(peek_buf_.data(), peek_len_),
            [self = shared_from_this()](boost::system::error_code ec, size_t) {
                if (ec) return;
                self->splice_c2b();
                self->splice_b2c();
            });
    }

    void splice_c2b() {
        client_.async_read_some(
            asio::buffer(c2b_buf_),
            [self = shared_from_this()](boost::system::error_code ec, size_t n) {
                if (ec) { self->backend_.close(); return; }
                asio::async_write(
                    self->backend_,
                    asio::buffer(self->c2b_buf_.data(), n),
                    [self](boost::system::error_code ec2, size_t) {
                        if (ec2) { self->client_.close(); return; }
                        self->splice_c2b();
                    });
            });
    }

    void splice_b2c() {
        backend_.async_read_some(
            asio::buffer(b2c_buf_),
            [self = shared_from_this()](boost::system::error_code ec, size_t n) {
                if (ec) { self->client_.close(); return; }
                asio::async_write(
                    self->client_,
                    asio::buffer(self->b2c_buf_.data(), n),
                    [self](boost::system::error_code ec2, size_t) {
                        if (ec2) { self->backend_.close(); return; }
                        self->splice_b2c();
                    });
            });
    }
};

//==============================================================================
// HTTP→HTTPS redirect (plain port 80)
//==============================================================================

// Reads the first line of the HTTP request to extract the Host header,
// then sends a 301 redirect to https://<host><path>.
class HttpRedirectSession : public std::enable_shared_from_this<HttpRedirectSession> {
    static constexpr size_t kBufSize = 4096;
    tcp::socket sock_;
    std::array<char, kBufSize> buf_{};
    size_t len_ = 0;

public:
    explicit HttpRedirectSession(tcp::socket s) : sock_(std::move(s)) {}

    void start() { read_more(); }

private:
    void read_more() {
        sock_.async_read_some(
            asio::buffer(buf_.data() + len_, buf_.size() - len_),
            [self = shared_from_this()](boost::system::error_code ec, size_t n) {
                if (ec) return;
                self->len_ += n;
                self->try_respond();
            });
    }

    void try_respond() {
        const std::string_view data(buf_.data(), len_);

        // Need at least the request line and headers (double CRLF)
        const auto header_end = data.find("\r\n\r\n");
        if (header_end == std::string_view::npos) {
            if (len_ < buf_.size()) { read_more(); return; } // need more data
            // Buffer full with no complete headers — just redirect to /
        }

        // Extract path from first request line: "GET /path HTTP/1.x"
        std::string path = "/";
        const auto line_end = data.find("\r\n");
        if (line_end != std::string_view::npos) {
            const auto first_line = data.substr(0, line_end);
            const auto sp1 = first_line.find(' ');
            if (sp1 != std::string_view::npos) {
                const auto sp2 = first_line.find(' ', sp1 + 1);
                if (sp2 != std::string_view::npos)
                    path = std::string(first_line.substr(sp1 + 1, sp2 - sp1 - 1));
            }
        }

        // Extract Host header
        std::string host;
        const auto search = data.substr(0, header_end == std::string_view::npos ? len_ : header_end);
        for (size_t pos = 0;;) {
            const auto crlf = search.find("\r\n", pos);
            const auto line = search.substr(pos, crlf == std::string_view::npos ? std::string_view::npos : crlf - pos);
            if (line.size() >= 6) {
                std::string lower(line.substr(0, 5));
                for (auto& c : lower) c = (char)std::tolower((unsigned char)c);
                if (lower == "host:") {
                    size_t vs = 5;
                    while (vs < line.size() && line[vs] == ' ') ++vs;
                    host = std::string(line.substr(vs));
                    // strip port if present (we're redirecting to default 443)
                    const auto colon = host.rfind(':');
                    if (colon != std::string::npos) host = host.substr(0, colon);
                    break;
                }
            }
            if (crlf == std::string_view::npos) break;
            pos = crlf + 2;
        }

        const std::string location = "https://" + host + path;
        const std::string resp =
            "HTTP/1.1 301 Moved Permanently\r\n"
            "Location: " + location + "\r\n"
            "Content-Length: 0\r\n"
            "Connection: close\r\n"
            "\r\n";

        auto buf = std::make_shared<std::string>(resp);
        asio::async_write(
            sock_,
            asio::buffer(*buf),
            [self = shared_from_this(), buf](boost::system::error_code, size_t) {
                self->sock_.close();
            });
    }
};

class HttpRedirectRouter {
    tcp::acceptor acceptor_;

public:
    HttpRedirectRouter(asio::io_context& ioc, const Config& cfg)
        : acceptor_(ioc,
                    tcp::endpoint(asio::ip::make_address(cfg.listen_address),
                                  cfg.http_redirect_port))
    {
        acceptor_.set_option(tcp::acceptor::reuse_address(true));
    }

    void start() { accept(); }

private:
    void accept() {
        acceptor_.async_accept(
            [this](boost::system::error_code ec, tcp::socket sock) {
                if (!ec)
                    std::make_shared<HttpRedirectSession>(std::move(sock))->start();
                if (ec != asio::error::operation_aborted)
                    accept();
            });
    }
};

//==============================================================================
// TCP acceptor
//==============================================================================

class TcpRouter {
    tcp::acceptor  acceptor_;
    const RouteTable& routes_;

public:
    TcpRouter(asio::io_context& ioc, const Config& cfg, const RouteTable& rt)
        : acceptor_(ioc,
                    tcp::endpoint(asio::ip::make_address(cfg.listen_address),
                                  cfg.listen_tcp_port))
        , routes_(rt)
    {
        acceptor_.set_option(tcp::acceptor::reuse_address(true));
    }

    void start() { accept(); }

private:
    void accept() {
        acceptor_.async_accept(
            [this](boost::system::error_code ec, tcp::socket sock) {
                if (!ec) {
                    sock.set_option(tcp::no_delay(true));
                    std::make_shared<TcpSession>(std::move(sock), routes_)->start();
                }
                if (ec != asio::error::operation_aborted)
                    accept();
            });
    }
};

//==============================================================================
// UDP router — QUIC SNI routing via per-session ephemeral sockets
//==============================================================================

// Pending QUIC connection: buffer CRYPTO fragments until SNI is extractable.
struct PendingQuicSession {
    udp::endpoint        client_ep;     // who to route back to once SNI is known
    std::vector<uint8_t> initial_dcid;  // client's original DCID (for diagnostics)
    // CRYPTO stream fragments keyed by stream offset.
    std::map<uint64_t, std::vector<uint8_t>> crypto_frags;
    // Raw UDP datagrams queued for replay once we route the session.
    std::vector<std::vector<uint8_t>> queued_pkts;
    std::chrono::steady_clock::time_point created_at = std::chrono::steady_clock::now();
};

// Try to extract SNI by reassembling contiguous CRYPTO fragments from offset 0.
// Returns nullopt if there is still a gap starting at offset 0.
static std::optional<std::string>
try_assemble_sni(const std::map<uint64_t, std::vector<uint8_t>>& frags)
{
    if (frags.empty() || frags.begin()->first != 0) return std::nullopt;

    // Build a contiguous buffer from offset 0, stopping at the first gap.
    std::vector<uint8_t> assembled;
    uint64_t expected = 0;
    for (const auto& [off, data] : frags) {
        if (off > expected) break;                          // gap — need more packets
        const size_t skip = (size_t)(expected - off);      // handle overlaps
        if (skip >= data.size()) continue;
        assembled.insert(assembled.end(), data.begin() + (ptrdiff_t)skip, data.end());
        expected = off + data.size();
    }

    if (assembled.empty()) return std::nullopt;

    if (quic_debug_enabled) {
        std::cerr << "[QUIC] assembled " << assembled.size() << " bytes, hex:\n";
        for (size_t i = 0; i < std::min(assembled.size(), size_t{256}); ++i) {
            std::cerr << std::hex << std::setw(2) << std::setfill('0') << (int)assembled[i];
            if ((i & 15) == 15) std::cerr << '\n'; else std::cerr << ' ';
        }
        std::cerr << std::dec << '\n';
    }

    const auto sv = sni_from_quic_crypto_data(
        std::span<const uint8_t>(assembled.data(), assembled.size()));
    if (sv.empty()) return std::nullopt;                    // not enough data yet
    return std::string(sv);
}

// One per active QUIC client-→-backend mapping.
//
// SHM mode (shm_ingress_ is set in UdpRouter):
//   Packets are written directly to the c2s SHM ring.  No backend_sock
//   is created; back_buf and backend_port are unused.
//
// UDP mode (shm_ingress_ is null):
//   A per-session ephemeral UDP socket forwards packets to the backend.
struct UdpSession {
    udp::endpoint   client_ep;
    udp::endpoint   backend_ep;
    // UDP-mode only — null in SHM mode.
    std::unique_ptr<udp::socket> backend_sock;
    uint16_t        backend_port = 0;
    std::chrono::steady_clock::time_point last_seen;
    std::array<uint8_t, 65536> back_buf{};

    // SHM mode: only client/backend endpoints are tracked.
    UdpSession(udp::endpoint cep, udp::endpoint bep)
        : client_ep(std::move(cep))
        , backend_ep(std::move(bep))
        , last_seen(std::chrono::steady_clock::now())
    {}

    // UDP mode: also create an ephemeral backend socket.
    UdpSession(asio::io_context& ioc,
               udp::endpoint cep,
               udp::endpoint bep)
        : client_ep(std::move(cep))
        , backend_ep(std::move(bep))
        , backend_sock(std::make_unique<udp::socket>(
              ioc, udp::endpoint(udp::v4(), 0)))
        , last_seen(std::chrono::steady_clock::now())
    {}
};

class UdpRouter {
    static constexpr size_t kRecvBufSize = 65536;

    asio::io_context& ioc_;
    udp::socket       listen_sock_;     // bound to 0.0.0.0:port
    const RouteTable& routes_;
    const int         timeout_sec_;

    std::array<uint8_t, kRecvBufSize> recv_buf_{};
    udp::endpoint                     sender_ep_;

    // Sessions keyed by client endpoint (string for hash stability)
    struct EpHash {
        size_t operator()(const udp::endpoint& ep) const noexcept {
            size_t h = std::hash<std::string>{}(ep.address().to_string());
            h ^= std::hash<uint16_t>{}(ep.port()) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };
    std::unordered_map<udp::endpoint, std::shared_ptr<UdpSession>, EpHash> sessions_;
    // Key: DCID bytes as std::string (binary). Each connection attempt has a unique
    // DCID so mixing fragments from different curl retries is impossible.
    std::unordered_map<std::string, PendingQuicSession>   pending_;
    // ep_to_dcid_ removed: DCID-keying is sufficient since each new connection
    // attempt picks a fresh random DCID. ep_to_dcid_ was causing stream_off=0
    // fragments to be stored under stale DCID keys from previous attempts.
    static constexpr size_t kMaxPendingPackets = 32;
    static constexpr auto   kPendingTimeout    = std::chrono::seconds(5);

    asio::steady_timer gc_timer_;

    // SHM channel (present when cfg.shm_egress_channel is non-empty):
    //   shm_ingress_  — c2s ring, writes incoming client packets to Http3Server.
    //   shm_egress_   — s2c ring, reads packets from Http3Server and sendmsg to client.
    // When the channel is empty, per-session UDP backend_sock is used instead.
    std::unique_ptr<ShmIngressWriter> shm_ingress_;
    std::unique_ptr<ShmEgressReader>  shm_egress_;

public:
    // Single-worker constructor: creates and binds the socket internally.
    UdpRouter(asio::io_context& ioc, const Config& cfg, const RouteTable& rt)
        : ioc_(ioc)
        , listen_sock_(ioc, udp::endpoint(asio::ip::make_address(cfg.listen_address),
                                          cfg.listen_udp_port))
        , routes_(rt)
        , timeout_sec_(cfg.udp_session_timeout_sec)
        , gc_timer_(ioc)
    {
        init_shm(cfg, /*worker_index=*/0, /*is_primary=*/true);
    }

    // Multi-worker constructor: accepts a pre-created SO_REUSEPORT socket.
    // @param socket       Already opened and bound UDP socket (SO_REUSEPORT).
    // @param worker_index Worker index (0 = primary, creates/owns the rings).
    // @param is_primary   Only the primary worker starts ShmEgressReader.
    UdpRouter(asio::io_context& ioc, const Config& cfg, const RouteTable& rt,
              udp::socket socket, size_t worker_index, bool is_primary)
        : ioc_(ioc)
        , listen_sock_(std::move(socket))
        , routes_(rt)
        , timeout_sec_(cfg.udp_session_timeout_sec)
        , gc_timer_(ioc)
    {
        init_shm(cfg, worker_index, is_primary);
    }

    int socket_fd() noexcept { return listen_sock_.native_handle(); }

    void start() {
        recv();
        schedule_gc();
        if (shm_ingress_) {
            if (shm_egress_) {
                shm_egress_->start();
                std::clog << "  SHM egress  channel: " << shm_egress_->ring_name() << "\n";
            }
            std::clog << "  SHM ingress channel: " << shm_ingress_->ring_name() << "\n";
        }
    }

private:
    void init_shm(const Config& cfg, size_t worker_index, bool is_primary) {
        if (!cfg.shm_egress_channel.empty()) {
            // Worker 0 creates both SHM rings; workers 1..N-1 open the existing
            // c2s ring (MPSC: multiple producers are safe).
            const bool creates_rings = (worker_index == 0);
            shm_ingress_ = std::make_unique<ShmIngressWriter>(
                cfg.shm_egress_channel, creates_rings);
            // Only the primary (worker 0) drains the s2c ring.
            if (is_primary) {
                shm_egress_ = std::make_unique<ShmEgressReader>(
                    cfg.shm_egress_channel, listen_sock_.native_handle());
            }
        }
    }

    void recv() {
        listen_sock_.async_receive_from(
            asio::buffer(recv_buf_),
            sender_ep_,
            [this](boost::system::error_code ec, size_t n) {
                if (!ec) on_packet(n);
                if (ec != asio::error::operation_aborted) recv();
            });
    }

    void on_packet(size_t n) {
        const auto datagram = std::span<const uint8_t>(recv_buf_.data(), n);
        const auto it       = sessions_.find(sender_ep_);

        if (quic_debug_enabled && it == sessions_.end()) {
            // Log every new-session datagram: size and first few bytes.
            std::cerr << "[QUIC] datagram n=" << n << " from " << sender_ep_
                      << " bytes[0..3]=";
            for (size_t i = 0; i < std::min(n, size_t{4}); ++i)
                std::cerr << std::hex << std::setw(2) << std::setfill('0')
                          << (int)recv_buf_[i] << ' ';
            std::cerr << std::dec << '\n';
        }

        if (it != sessions_.end()) {
            // Existing session — forward to backend.
            it->second->last_seen = std::chrono::steady_clock::now();
            if (shm_ingress_) {
                // SHM mode: write to ingress ring with real client endpoint.
                const auto ep_data = sender_ep_.data();
                shm_ingress_->write_packet(
                    static_cast<const sockaddr*>(static_cast<const void*>(ep_data)),
                    static_cast<socklen_t>(sender_ep_.size()),
                    datagram.data(), datagram.size());
            } else {
                it->second->backend_sock->async_send(
                    asio::buffer(datagram.data(), datagram.size()),
                    [](boost::system::error_code, size_t) {});
            }
            return;
        }

        // Walk all coalesced QUIC Initial packets in this UDP datagram.
        // RFC 9000 §12.2: multiple QUIC packets may be concatenated in one UDP datagram.
        // The fragment containing stream_off=0 may be in the 2nd or later packet.
        size_t pkt_off = 0;
        bool   any_frag = false;
        std::string route_dcid_key;

        while (pkt_off < n) {
            const auto pkt_span = std::span<const uint8_t>(recv_buf_.data() + pkt_off, n - pkt_off);

            // Compute packet boundary without decrypting (for coalesced-packet skipping).
            const size_t pkt_len = quic_initial_packet_len(pkt_span);

            QUIC_DBG_PLAIN("  [pkt] off=" << pkt_off << " pkt_len=" << pkt_len
                           << " first_byte=0x" << std::hex << (pkt_span.empty() ? 0 : (int)pkt_span[0]) << std::dec);

            auto frags = quic_extract_crypto(pkt_span);
            for (auto& frag : frags) {
                const std::string dcid_key(frag.dcid.begin(), frag.dcid.end());
                if (!any_frag) route_dcid_key = dcid_key;
                any_frag = true;

                auto& pend = pending_[dcid_key];
                if (pend.client_ep == udp::endpoint{}) {
                    pend.client_ep    = sender_ep_;
                    pend.initial_dcid = frag.dcid;
                }
                if (quic_debug_enabled) {
                    std::cerr << "[QUIC] dcid=";
                    for (auto b : frag.dcid)
                        std::cerr << std::hex << std::setw(2) << std::setfill('0') << (int)b;
                    std::cerr << std::dec
                        << " off=" << frag.stream_offset
                        << " frags=" << (pend.crypto_frags.size()+1) << "\n";
                }
                pend.crypto_frags[frag.stream_offset] = std::move(frag.data);
            }

            if (pkt_len == 0 || pkt_off + pkt_len > n) break; // not QUIC Initial or malformed
            pkt_off += pkt_len;
        }

        if (!any_frag) return; // no QUIC Initial packets in this datagram

        // Queue the whole datagram under the first DCID found.
        auto& pend = pending_[route_dcid_key];
        if (pend.queued_pkts.size() < kMaxPendingPackets)
            pend.queued_pkts.emplace_back(recv_buf_.data(), recv_buf_.data() + n);

        // Try to assemble a contiguous CRYPTO buffer starting at offset 0.
        auto sni_opt = try_assemble_sni(pend.crypto_frags);
        if (!sni_opt) return; // need more fragments

        const std::string& sni = *sni_opt;
        const udp::endpoint* ep = routes_.lookup_udp(sni);
        if (!ep) {
            std::cerr << "[UDP] No route for SNI '" << sni << "' — dropping\n";

            pending_.erase(route_dcid_key);
            return;
        }
        std::clog << "[UDP] SNI='" << sni << "' → " << *ep << "\n";

        // Create the session and replay all queued packets.
        std::shared_ptr<UdpSession> sess;
        if (shm_ingress_) {
            // SHM mode: no backend socket needed.
            sess = std::make_shared<UdpSession>(pend.client_ep, *ep);
        } else {
            // UDP mode: create ephemeral socket and connect to backend.
            sess = std::make_shared<UdpSession>(ioc_, pend.client_ep, *ep);
            sess->backend_sock->connect(*ep);
            sess->backend_port = sess->backend_sock->local_endpoint().port();
            std::clog << "[UDP] Session backend_port=" << sess->backend_port
                      << " client=" << sess->client_ep << "\n";
        }
        sessions_.emplace(pend.client_ep, sess);

        auto queued = std::move(pend.queued_pkts);
        pending_.erase(route_dcid_key);

        if (shm_ingress_) {
            // Write all queued packets to the ingress ring.
            const auto& cep      = sess->client_ep;
            const auto  ep_data  = cep.data();
            const auto  ep_sa    = static_cast<const sockaddr*>(
                static_cast<const void*>(ep_data));
            const auto  ep_len   = static_cast<socklen_t>(cep.size());
            for (auto& qpkt : queued)
                shm_ingress_->write_packet(ep_sa, ep_len, qpkt.data(), qpkt.size());
        } else {
            for (auto& qpkt : queued) {
                auto buf = std::make_shared<std::vector<uint8_t>>(std::move(qpkt));
                sess->backend_sock->async_send(
                    asio::buffer(*buf),
                    [buf](boost::system::error_code, size_t) {});
            }
            recv_from_backend(sess);
        }
    }

    void recv_from_backend(std::shared_ptr<UdpSession> sess) {
        sess->backend_sock->async_receive(
            asio::buffer(sess->back_buf),
            [this, sess](boost::system::error_code ec, size_t n) {
                if (ec) return; // session torn down
                sess->last_seen = std::chrono::steady_clock::now();
                listen_sock_.async_send_to(
                    asio::buffer(sess->back_buf.data(), n),
                    sess->client_ep,
                    [](boost::system::error_code, size_t) {});
                recv_from_backend(sess);
            });
    }

    void schedule_gc() {
        gc_timer_.expires_after(std::chrono::seconds(timeout_sec_ / 2 + 1));
        gc_timer_.async_wait([this](boost::system::error_code ec) {
            if (ec) return;
            const auto now     = std::chrono::steady_clock::now();
            const auto timeout = std::chrono::seconds(timeout_sec_);
            for (auto it = sessions_.begin(); it != sessions_.end(); ) {
                if (now - it->second->last_seen > timeout)
                    it = sessions_.erase(it);
                else
                    ++it;
            }
            // Evict stale pending reassembly entries (typically < 5 s old)
            for (auto it = pending_.begin(); it != pending_.end(); ) {
                if (now - it->second.created_at > kPendingTimeout)
                    it = pending_.erase(it);
                else
                    ++it;
            }
            schedule_gc();
        });
    }
};

//==============================================================================
// main
//==============================================================================

int main(int argc, char* argv[])
{
    // Parse flags: optional --debug / -d before the config path
    bool debug = false;
    int  config_arg = 1;
    for (int i = 1; i < argc; ++i) {
        std::string_view a(argv[i]);
        if (a == "--debug" || a == "-d") { debug = true; config_arg = i + 1; }
        else { config_arg = i; break; }
    }
    quic_debug_enabled = debug;

    if (config_arg >= argc) {
        std::cerr << "Usage: npquicrouter [--debug] <config.json>\n"
                  << "\nExample config.json:\n"
                  << R"({
  "listen_address": "0.0.0.0",
  "listen_tcp_port": 443,
  "listen_udp_port": 443,
  "http_redirect_port": 80,
  "default_tcp_backend": "127.0.0.1:8443",
  "default_udp_backend": "127.0.0.1:4433",
  "udp_session_timeout_sec": 120,
  "shm_egress_channel": "",
  "num_workers": 4,
  "routes": [
    { "sni": "app.example.com",  "tcp_backend": "127.0.0.1:8443", "udp_backend": "127.0.0.1:4433" },
    { "sni": "blog.example.com", "tcp_backend": "127.0.0.1:9443", "udp_backend": "127.0.0.1:4434" }
  ]
})" << "\n";
        return 1;
    }

    // Load config
    std::ifstream f(argv[config_arg]);
    if (!f) {
        std::cerr << "Cannot open config: " << argv[1] << "\n";
        return 1;
    }
    const std::string json{ std::istreambuf_iterator<char>(f),
                            std::istreambuf_iterator<char>() };

    Config cfg;
    if (const auto err = glz::read_json(cfg, json); err) {
        std::cerr << "Config parse error: " << glz::format_error(err, json) << "\n";
        return 1;
    }

    asio::io_context ioc;

    // Resolve all backend addresses up front
    RouteTable routes;
    try {
        routes = build_route_table(cfg, ioc);
    } catch (const std::exception& e) {
        std::cerr << "Route table error: " << e.what() << "\n";
        return 1;
    }

    // TCP router + HTTP redirect always run on the main io_context.
    TcpRouter tcp_router(ioc, cfg, routes);
    tcp_router.start();

    std::unique_ptr<HttpRedirectRouter> http_redirect;
    if (cfg.http_redirect_port != 0) {
        http_redirect = std::make_unique<HttpRedirectRouter>(ioc, cfg);
        http_redirect->start();
    }

    // ── UDP router setup ──────────────────────────────────────────────────────
    const size_t num_workers = static_cast<size_t>(std::max(1, cfg.num_workers));

    // Single-worker path: reuse main ioc (backward-compatible, zero overhead).
    std::unique_ptr<UdpRouter> single_udp_router;

    // Multi-worker path: each worker gets its own io_context + socket.
    struct UdpWorker {
        asio::io_context                                           ioc;
        asio::executor_work_guard<asio::io_context::executor_type> work_guard{
            asio::make_work_guard(ioc)};
        std::unique_ptr<UdpRouter>                                 router;
        std::thread                                                thread;
    };
    std::vector<std::unique_ptr<UdpWorker>> udp_workers;

#if defined(NPRPC_ROUTER_REUSEPORT_BPF_ENABLED) && defined(SO_ATTACH_REUSEPORT_EBPF)
    std::unique_ptr<QuicRouterReusePortBpf> reuseport_bpf;
#endif

    if (num_workers == 1) {
        single_udp_router = std::make_unique<UdpRouter>(ioc, cfg, routes);
        single_udp_router->start();
    } else {
#if !defined(SO_REUSEPORT)
        std::cerr << "[UDP] SO_REUSEPORT not available on this platform; "
                     "falling back to single worker\n";
        single_udp_router = std::make_unique<UdpRouter>(ioc, cfg, routes);
        single_udp_router->start();
#else
        using reuse_port_opt =
            boost::asio::detail::socket_option::boolean<SOL_SOCKET, SO_REUSEPORT>;

        const auto ep = udp::endpoint(asio::ip::make_address(cfg.listen_address),
                                      cfg.listen_udp_port);

        for (size_t i = 0; i < num_workers; ++i) {
            auto w = std::make_unique<UdpWorker>();
            udp::socket sock(w->ioc);
            sock.open(ep.protocol());
            sock.set_option(reuse_port_opt{true});
            sock.bind(ep);
            w->router = std::make_unique<UdpRouter>(
                w->ioc, cfg, routes, std::move(sock), i, /*is_primary=*/i == 0);
            udp_workers.push_back(std::move(w));
        }

#if defined(NPRPC_ROUTER_REUSEPORT_BPF_ENABLED) && defined(SO_ATTACH_REUSEPORT_EBPF)
        reuseport_bpf = std::make_unique<QuicRouterReusePortBpf>();
        if (reuseport_bpf->load(num_workers, kQuicServerScidLen)) {
            bool bpf_ok = true;
            for (size_t i = 0; i < num_workers; ++i)
                bpf_ok &= reuseport_bpf->register_socket(
                    static_cast<uint32_t>(i), udp_workers[i]->router->socket_fd());
            if (bpf_ok)
                bpf_ok = reuseport_bpf->attach(udp_workers[0]->router->socket_fd());
            if (!bpf_ok) {
                std::cerr << "[UDP] Warning: eBPF attach failed — "
                             "packet distribution across workers not guaranteed\n";
                reuseport_bpf.reset();
            }
        } else {
            std::cerr << "[UDP] Warning: eBPF load failed — "
                         "packet distribution across workers not guaranteed\n";
            reuseport_bpf.reset();
        }
#endif // NPRPC_ROUTER_REUSEPORT_BPF_ENABLED

        for (auto& w : udp_workers) {
            w->router->start();
            w->thread = std::thread([ioc = &w->ioc]() { ioc->run(); });
        }
#endif // SO_REUSEPORT
    }

    std::clog << "npquicrouter listening — TCP "
              << cfg.listen_address << ":" << cfg.listen_tcp_port
              << "  UDP " << cfg.listen_address << ":" << cfg.listen_udp_port;
    if (cfg.http_redirect_port != 0)
        std::clog << "  HTTP(redirect) " << cfg.listen_address << ":" << cfg.http_redirect_port;
    if (num_workers > 1) {
        std::clog << "  UDP-workers=" << num_workers;
#if defined(NPRPC_ROUTER_REUSEPORT_BPF_ENABLED) && defined(SO_ATTACH_REUSEPORT_EBPF)
        std::clog << (reuseport_bpf ? " (eBPF routing)" : " (no eBPF)");
#endif
    }
    std::clog << "\n";

    // SIGINT/SIGTERM → stop all io_contexts
    asio::signal_set signals(ioc, SIGINT, SIGTERM);
    signals.async_wait([&](boost::system::error_code, int sig) {
        std::clog << "\nSignal " << sig << " received — shutting down\n";
        for (auto& w : udp_workers) {
            w->work_guard.reset();
            w->ioc.stop();
        }
        ioc.stop();
    });

    ioc.run();  // main thread: TCP + signals (+ UDP in single-worker mode)

    for (auto& w : udp_workers) {
        if (w->thread.joinable()) w->thread.join();
    }

#if defined(NPRPC_ROUTER_REUSEPORT_BPF_ENABLED) && defined(SO_ATTACH_REUSEPORT_EBPF)
    if (reuseport_bpf) reuseport_bpf->print_metrics(num_workers);
#endif

    return 0;
}

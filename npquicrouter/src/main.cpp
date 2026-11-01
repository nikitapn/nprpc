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
#include <condition_variable>
#include <cstring>
#include <filesystem>
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
#include <functional>
#include <string_view>
#include <optional>
#include <mutex>
#include <vector>

#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>

#include <glaze/json.hpp>

#include "country_table.hpp"
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
    std::string tcp_backend;  // "ip:port"
    std::string udp_backend;  // "ip:port"
    std::string shm_ingress_channel;  // ingress SHM channel name for this UDP backend (empty = UDP mode)
    // Egress ring for this backend; empty = the top-level shm_egress_channel.
    // A ring of its own keeps backends' fates apart: a writer killed between
    // reserving a slot and committing it stalls the ring's reader for good,
    // and on a shared ring that is every backend's HTTP/3.
    std::string shm_egress_channel;
    // Ring sizes in KiB; 0 = the defaults in quic_shm_channel.hpp (32 MiB).
    // A site that answers GETs needs far less than a busy RPC backend.
    uint32_t    shm_ingress_ring_kib = 0;
    uint32_t    shm_egress_ring_kib  = 0;
};

struct Config {
    std::string  listen_address        = "0.0.0.0";
    uint16_t     listen_tcp_port       = 443;
    uint16_t     listen_udp_port       = 443;
    uint16_t     http_redirect_port    = 0;    // 0 = disabled; 80 = redirect HTTP→HTTPS
    // Webroot for ACME HTTP-01 challenges. When non-empty, the redirect
    // listener serves GET /.well-known/acme-challenge/<token> from
    // <acme_webroot>/.well-known/acme-challenge/<token> instead of issuing a
    // 301.  Matches certbot's `--webroot -w <acme_webroot>`.
    std::string  acme_webroot;
    std::string  default_tcp_backend;  // fallback if no SNI match
    std::string  default_udp_backend;
    int          udp_session_timeout_sec = 120;
    std::vector<RouteEntry> routes;
    // Egress SHM channel name (empty = disabled).
    // When set, npquicrouter creates a LockFreeRingBuffer named
    // /nprpc_<shm_egress_channel>_s2c that Http3Server writes GSO batches
    // into.  npquicrouter drains the ring and calls sendmsg(GSO) to preserve
    // batching across the double-UDP-hop.
    // Per-backend ingress channels are configured per-route via RouteEntry::shm_ingress_channel.
    std::string  shm_egress_channel;
    uint32_t     shm_egress_ring_kib  = 0;     // 0 = kShmEgressRingSize
    int          num_workers          = 1;     // UDP worker threads (1 = single-threaded)

    // Limits on what a client can make this process hold or do.  0 = unlimited.
    // "Per IP" means per IPv4 address or per IPv6 /64.  The per-IP defaults are
    // high because a carrier NAT puts many real clients behind one address.
    int          udp_handshake_timeout_sec = 10; // Sessions that never finish a handshake.
    uint32_t     udp_max_sessions          = 100000;
    uint32_t     udp_max_sessions_per_ip   = 2000;
    uint32_t     udp_max_pending           = 20000;
    uint32_t     udp_max_pending_per_ip    = 256;
    // Packets from unknown clients, checked before spending CPU decrypting them.
    uint32_t     udp_new_initials_per_second        = 100000;
    uint32_t     udp_new_initials_per_ip_per_second = 500;
    uint32_t     tcp_max_connections        = 50000;
    uint32_t     tcp_max_connections_per_ip = 2000;
    int          tcp_peek_timeout_sec       = 10; // Connections that send no ClientHello.
    int          stats_interval_sec         = 60; // Period of the [stats] log line; 0 = never.
    std::string  geoip_csv;                       // DB-IP country CSV; empty = no country counts.
    std::string  stats_file;                      // JSON copy of the stats; empty = none.
    int          stats_file_interval_sec    = 5;
};

// Glaze reflection
template <>
struct glz::meta<RouteEntry> {
    using T = RouteEntry;
    static constexpr auto value = glz::object(
        "sni",         &T::sni,
        "tcp_backend", &T::tcp_backend,
        "udp_backend", &T::udp_backend,
        "shm_ingress_channel", &T::shm_ingress_channel,
        "shm_egress_channel",  &T::shm_egress_channel,
        "shm_ingress_ring_kib", &T::shm_ingress_ring_kib,
        "shm_egress_ring_kib",  &T::shm_egress_ring_kib);
};

template <>
struct glz::meta<Config> {
    using T = Config;
    static constexpr auto value = glz::object(
        "listen_address",          &T::listen_address,
        "listen_tcp_port",         &T::listen_tcp_port,
        "listen_udp_port",         &T::listen_udp_port,
        "http_redirect_port",      &T::http_redirect_port,
        "acme_webroot",            &T::acme_webroot,
        "default_tcp_backend",     &T::default_tcp_backend,
        "default_udp_backend",     &T::default_udp_backend,
        "udp_session_timeout_sec", &T::udp_session_timeout_sec,
        "routes",                  &T::routes,
        "shm_egress_channel",      &T::shm_egress_channel,
        "shm_egress_ring_kib",     &T::shm_egress_ring_kib,
        "num_workers",             &T::num_workers,
        "udp_handshake_timeout_sec", &T::udp_handshake_timeout_sec,
        "udp_max_sessions",        &T::udp_max_sessions,
        "udp_max_sessions_per_ip", &T::udp_max_sessions_per_ip,
        "udp_max_pending",         &T::udp_max_pending,
        "udp_max_pending_per_ip",  &T::udp_max_pending_per_ip,
        "udp_new_initials_per_second", &T::udp_new_initials_per_second,
        "udp_new_initials_per_ip_per_second", &T::udp_new_initials_per_ip_per_second,
        "tcp_max_connections",     &T::tcp_max_connections,
        "tcp_max_connections_per_ip", &T::tcp_max_connections_per_ip,
        "tcp_peek_timeout_sec",    &T::tcp_peek_timeout_sec,
        "stats_interval_sec",      &T::stats_interval_sec,
        "geoip_csv",               &T::geoip_csv,
        "stats_file",              &T::stats_file,
        "stats_file_interval_sec", &T::stats_file_interval_sec);
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
// Per-site counters
//==============================================================================

// What the stats file holds. Readers ignore fields they don't know, so adding
// one here needs no change anywhere else.
struct SiteStatsJson {
    std::string name;
    uint64_t active_sessions, sessions, packets, bytes, ring_full_drops, reply_packets, reply_bytes;
    uint64_t active_tcp, tcp, tcp_in_bytes, tcp_out_bytes;
    std::map<std::string, uint64_t> quic_countries, tcp_countries;
};

struct RingStatsJson {
    std::string name;
    uint64_t used_bytes, capacity_bytes;
    uint64_t packets = 0, bytes = 0, send_errors = 0; // Egress rings only.
};

struct RouterStatsJson {
    uint64_t uptime_ms = 0;
    uint64_t sessions = 0, pending = 0, tcp = 0;
    std::map<std::string, uint64_t> counters;
    std::vector<SiteStatsJson> sites;
    std::vector<RingStatsJson> ingress, egress;
};

struct SiteStats {
    using Counter = std::atomic<uint64_t>;
    using CountryCounters = std::array<Counter, kCountryCodes>;
    Counter active_sessions{0};
    Counter sessions{0};
    Counter packets{0};
    Counter bytes{0};
    Counter ring_full_drops{0};
    // Replies seen here, which is UDP mode only. SHM replies are under egress[].
    Counter reply_packets{0};
    Counter reply_bytes{0};
    Counter active_tcp{0};
    Counter tcp{0};
    Counter tcp_in_bytes{0};
    Counter tcp_out_bytes{0};
    // Running totals only: an interval's count could single out one visitor.
    CountryCounters quic_countries{}; // Clients that finished a handshake.
    CountryCounters tcp_countries{};  // Connections with a routed SNI.
};

// The country table, replaced from a background thread when its file changes.
// Lookups happen once per connection and only a country count is kept.
class GeoIp {
public:
    void start(const std::string& path) {
        if (path.empty()) return;
        enabled_ = true;
        path_ = path;
        reload();
        if (!table_.load()) std::cerr << "[geoip] Can't load " << path_ << ", counting nothing until it can\n";
        watcher_ = std::jthread([this](std::stop_token stop) { watch(stop); });
    }

    bool enabled() const noexcept { return enabled_; }

    void count(SiteStats::CountryCounters& counters, const asio::ip::address& address) const noexcept {
        const auto table = table_.load();
        if (!table) return;
        uint16_t country;
        if (address.is_v4()) {
            country = table->lookup(address.to_v4().to_uint());
        } else if (address.to_v6().is_v4_mapped()) {
            country = table->lookup(asio::ip::make_address_v4(asio::ip::v4_mapped, address.to_v6()).to_uint());
        } else {
            country = table->lookup(address.to_v6().to_bytes());
        }
        counters[country].fetch_add(1, std::memory_order_relaxed);
    }

private:
    void reload() {
        std::error_code ec;
        const auto modified = std::filesystem::last_write_time(path_, ec);
        if (ec || modified == modified_) return;
        // Parsing takes a noticeable fraction of a second, so never on a worker.
        if (auto table = CountryTable::load(path_)) {
            std::clog << "[geoip] Loaded " << table->ranges() << " ranges from " << path_ << "\n";
            table_.store(std::move(table));
            modified_ = modified;
        } else {
            std::cerr << "[geoip] Can't load " << path_ << ", keeping the current table\n";
            modified_ = modified; // Not worth retrying until the file changes again.
        }
    }

    void watch(std::stop_token stop) {
        std::mutex mutex;
        std::condition_variable_any wake;
        std::unique_lock lock(mutex);
        while (!stop.stop_requested()) {
            wake.wait_for(lock, stop, std::chrono::minutes(1), [] { return false; });
            if (!stop.stop_requested()) reload();
        }
    }

    bool enabled_ = false;
    std::string path_;
    std::filesystem::file_time_type modified_{};
    std::atomic<std::shared_ptr<const CountryTable>> table_;
    std::jthread watcher_;
};

static GeoIp g_geoip;

// One entry per configured route, so a client's SNI can't grow the table.
class SiteStatsTable {
public:
    SiteStats* get(const std::string& sni) {
        std::lock_guard lock(mutex_);
        auto& entry = sites_[sni];
        if (!entry) entry = std::make_unique<SiteStats>();
        return entry.get();
    }

    // One line per site with any counts, biggest countries first. These totals
    // repeat every interval, so the length of each line is capped.
    void print_countries(std::ostream& out) {
        std::lock_guard lock(mutex_);
        for (const auto& [name, site] : sites_) {
            const auto quic = top_countries(site->quic_countries);
            const auto tcp = top_countries(site->tcp_countries);
            if (!quic.empty() || !tcp.empty())
                out << "[countries] site[" << name << "] quic=" << quic << " tcp=" << tcp << "\n";
        }
    }

    void fill(std::vector<SiteStatsJson>& out) {
        const auto count = [](const SiteStats::Counter& c) { return c.load(std::memory_order_relaxed); };
        const auto countries = [&](const SiteStats::CountryCounters& counters) {
            std::map<std::string, uint64_t> result;
            for (size_t i = 0; i < counters.size(); ++i)
                if (const auto n = count(counters[i])) result[country_code(i)] = n;
            return result;
        };
        std::lock_guard lock(mutex_);
        for (const auto& [name, s] : sites_)
            out.push_back({name, count(s->active_sessions), count(s->sessions), count(s->packets), count(s->bytes),
                           count(s->ring_full_drops), count(s->reply_packets), count(s->reply_bytes),
                           count(s->active_tcp), count(s->tcp), count(s->tcp_in_bytes), count(s->tcp_out_bytes),
                           countries(s->quic_countries), countries(s->tcp_countries)});
    }

    void print(std::ostream& out) {
        const auto count = [](const SiteStats::Counter& c) { return c.load(std::memory_order_relaxed); };
        std::lock_guard lock(mutex_);
        for (const auto& [name, site] : sites_) {
            out << " site[" << name << "]=" << count(site->active_sessions) << "active/"
                << count(site->sessions) << "sessions/" << count(site->packets) << "pkts/"
                << count(site->bytes) << "B/" << count(site->ring_full_drops) << "ring_full/"
                << count(site->reply_packets) << "reply_pkts/" << count(site->reply_bytes) << "reply_B/"
                << count(site->active_tcp) << "tcp_active/" << count(site->tcp) << "tcp/"
                << count(site->tcp_in_bytes) << "tcp_in_B/" << count(site->tcp_out_bytes) << "tcp_out_B";
        }
    }

private:
    static std::string top_countries(const SiteStats::CountryCounters& counters) {
        static constexpr size_t kShown = 20;
        std::vector<std::pair<uint64_t, size_t>> nonzero;
        for (size_t i = 0; i < counters.size(); ++i)
            if (const auto n = counters[i].load(std::memory_order_relaxed)) nonzero.emplace_back(n, i);
        std::ranges::sort(nonzero, std::greater{});
        std::ostringstream out;
        uint64_t other = 0;
        for (size_t i = 0; i < nonzero.size(); ++i) {
            if (i >= kShown) { other += nonzero[i].first; continue; }
            out << (i ? "," : "") << country_code(nonzero[i].second) << ":" << nonzero[i].first;
        }
        if (other) out << ",other:" << other;
        return out.str();
    }

    std::mutex mutex_;
    std::map<std::string, std::unique_ptr<SiteStats>> sites_;
};

static SiteStatsTable g_site_stats;

//==============================================================================
// Route table
//==============================================================================

struct ResolvedRoute {
    std::string     sni;
    tcp::endpoint   tcp_ep;
    udp::endpoint   udp_ep;
    bool            has_tcp = false;
    bool            has_udp = false;
    std::string     shm_ingress_channel; // empty = UDP mode for this route
    SiteStats*      stats = nullptr;
};

struct RouteTable {
    std::vector<ResolvedRoute> routes;
    ResolvedRoute              default_tcp_route; // has_tcp indicates if set
    ResolvedRoute              default_udp_route; // has_udp indicates if set

    const ResolvedRoute* lookup_tcp(std::string_view sni) const noexcept {
        for (const auto& r : routes)
            if (r.has_tcp && r.sni == sni) return &r;
        return default_tcp_route.has_tcp ? &default_tcp_route : nullptr;
    }

    const ResolvedRoute* lookup_udp(std::string_view sni) const noexcept {
        for (const auto& r : routes)
            if (r.has_udp && r.sni == sni) return &r;
        return default_udp_route.has_udp ? &default_udp_route : nullptr;
    }
};

static RouteTable build_route_table(const Config& cfg, asio::io_context& ioc)
{
    RouteTable rt;
    for (const auto& r : cfg.routes) {
        ResolvedRoute rr;
        rr.sni        = r.sni;
        rr.shm_ingress_channel = r.shm_ingress_channel;
        rr.stats      = g_site_stats.get(r.sni);
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
        rt.default_tcp_route.tcp_ep  = parse_tcp_endpoint(cfg.default_tcp_backend, ioc);
        rt.default_tcp_route.has_tcp = true;
        rt.default_tcp_route.stats   = g_site_stats.get("default");
    }
    if (!cfg.default_udp_backend.empty()) {
        rt.default_udp_route.udp_ep  = parse_udp_endpoint(cfg.default_udp_backend, ioc);
        rt.default_udp_route.has_udp = true;
        rt.default_udp_route.stats   = g_site_stats.get("default");
        // default backend has no shm_ingress_channel — always uses UDP mode
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

  void print_totals(std::ostream& out, size_t worker_count) const
  {
    const auto [routed, drops] = totals(worker_count);
    out << " bpf_routed=" << routed << " bpf_drops=" << drops;
  }

  std::pair<uint64_t, uint64_t> totals(size_t worker_count) const
  {
    const int ncpus = libbpf_num_possible_cpus();
    if (ncpus <= 0) return {0, 0};
    std::vector<uint64_t> per_cpu(static_cast<size_t>(ncpus));
    const auto sum = [&](int fd, uint32_t key) -> uint64_t {
      if (fd < 0 || bpf_map_lookup_elem(fd, &key, per_cpu.data()) != 0) return 0;
      uint64_t total = 0;
      for (const auto value : per_cpu) total += value;
      return total;
    };
    uint64_t routed = 0;
    for (uint32_t i = 0; i < static_cast<uint32_t>(worker_count); ++i)
      routed += sum(worker_counters_fd_, i);
    return {routed, sum(drop_counter_fd_, 0)};
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

//==============================================================================
// Limits and counters
//==============================================================================

struct RouterStats {
    using Counter = std::atomic<uint64_t>;
    Counter initial_too_small{0};     // Under the 1200 bytes QUIC requires of a client.
    Counter initial_rate_limited{0};  // Over the per-IP or overall rate.
    Counter initial_undecryptable{0}; // No readable Initial in it.
    Counter initial_no_sni{0};        // Pending connections that expired with no SNI.
    Counter unknown_short_header{0};  // Not a session, and an unknown connection ID.
    Counter no_route{0};              // Unknown SNI.
    Counter pending_cap_drops{0};
    Counter session_cap_drops{0};
    Counter handshake_timeouts{0};    // Sessions that never finished a handshake.
    Counter idle_timeouts{0};
    Counter moved_clients{0};
    Counter tcp_cap_drops{0};
    Counter tcp_peek_timeouts{0};
};

static RouterStats g_stats;

// The SHM rings, which every router keeps open until the process exits.
class RingStatsTable {
public:
    void add(const ShmIngressWriter& ring) {
        std::lock_guard lock(mutex_);
        ingress_[ring.ring_name()] = &ring; // Workers share a ring; any one can read it.
    }

    void add(const ShmEgressReader& ring) {
        std::lock_guard lock(mutex_);
        egress_[ring.ring_name()] = &ring;
    }

    void fill(RouterStatsJson& out) {
        std::lock_guard lock(mutex_);
        for (const auto& [name, ring] : ingress_)
            out.ingress.push_back({name, ring->used_bytes(), ring->capacity_bytes()});
        for (const auto& [name, ring] : egress_)
            out.egress.push_back({name, ring->used_bytes(), ring->capacity_bytes(),
                                  ring->packets(), ring->bytes(), ring->send_errors()});
    }

    void print(std::ostream& out) {
        std::lock_guard lock(mutex_);
        for (const auto& [name, ring] : ingress_)
            out << " ingress[" << name << "]=" << ring->used_bytes() << "/" << ring->capacity_bytes() << "B";
        for (const auto& [name, ring] : egress_)
            out << " egress[" << name << "]=" << ring->used_bytes() << "/" << ring->capacity_bytes() << "B/"
                << ring->packets() << "pkts/" << ring->bytes() << "B/" << ring->send_errors() << "send_errors";
    }

private:
    std::mutex mutex_;
    std::map<std::string, const ShmIngressWriter*> ingress_;
    std::map<std::string, const ShmEgressReader*> egress_;
};

static RingStatsTable g_ring_stats;

class RouterLimiter {
public:
    enum Kind { Session, Pending, TcpConnection, kKinds };

    void configure(const Config& cfg) {
        max_[Session] = cfg.udp_max_sessions;
        max_per_ip_[Session] = cfg.udp_max_sessions_per_ip;
        max_[Pending] = cfg.udp_max_pending;
        max_per_ip_[Pending] = cfg.udp_max_pending_per_ip;
        max_[TcpConnection] = cfg.tcp_max_connections;
        max_per_ip_[TcpConnection] = cfg.tcp_max_connections_per_ip;
        initials_per_second_ = cfg.udp_new_initials_per_second;
        initials_per_ip_per_second_ = cfg.udp_new_initials_per_ip_per_second;
        overall_tokens_ = static_cast<double>(initials_per_second_) * 2;
    }

    // Takes a place for as long as the slot lives; null when over a limit.
    class Slot {
    public:
        Slot(RouterLimiter& limiter, Kind kind, std::string key)
            : limiter_(limiter), kind_(kind), key_(std::move(key)) {}
        ~Slot() { limiter_.release(kind_, key_); }
        Slot(const Slot&) = delete;
        Slot& operator=(const Slot&) = delete;
    private:
        RouterLimiter& limiter_;
        Kind kind_;
        std::string key_;
    };

    std::unique_ptr<Slot> acquire(Kind kind, const asio::ip::address& ip) {
        if (max_[kind] != 0 && active_[kind].load(std::memory_order_relaxed) >= max_[kind])
            return nullptr;
        auto key = ip_key(ip);
        auto& shard = shard_for(key);
        {
            std::lock_guard lock(shard.mutex);
            auto* state = find_or_add(shard, key);
            if (!state) return nullptr;
            if (max_per_ip_[kind] != 0 && state->active[kind] >= max_per_ip_[kind]) return nullptr;
            ++state->active[kind];
            state->last_used = std::chrono::steady_clock::now();
        }
        active_[kind].fetch_add(1, std::memory_order_relaxed);
        return std::make_unique<Slot>(*this, kind, std::move(key));
    }

    // Spends one token for a packet from an unknown client.
    bool allow_initial(const asio::ip::address& ip) {
        const auto now = std::chrono::steady_clock::now();
        if (initials_per_second_ != 0) {
            std::lock_guard lock(overall_mutex_);
            if (!take_token(overall_tokens_, overall_refilled_, initials_per_second_, now)) return false;
        }
        if (initials_per_ip_per_second_ == 0) return true;
        auto key = ip_key(ip);
        auto& shard = shard_for(key);
        std::lock_guard lock(shard.mutex);
        auto* state = find_or_add(shard, key);
        if (!state) return false;
        if (state->refilled == std::chrono::steady_clock::time_point{}) {
            state->tokens = static_cast<double>(initials_per_ip_per_second_) * 2;
            state->refilled = now;
        }
        state->last_used = now;
        return take_token(state->tokens, state->refilled, initials_per_ip_per_second_, now);
    }

    // Forgets addresses that hold nothing, so spoofed ones don't accumulate.
    void expire() {
        const auto now = std::chrono::steady_clock::now();
        for (auto& shard : shards_) {
            std::lock_guard lock(shard.mutex);
            for (auto it = shard.ips.begin(); it != shard.ips.end(); ) {
                const bool idle = now - it->second.last_used > kForgetAfter;
                const bool holds_nothing = std::all_of(std::begin(it->second.active), std::end(it->second.active),
                                                       [](uint32_t n) { return n == 0; });
                if (idle && holds_nothing) it = shard.ips.erase(it);
                else ++it;
            }
        }
    }

    uint64_t active(Kind kind) const { return active_[kind].load(std::memory_order_relaxed); }

private:
    static constexpr size_t kShards = 64;
    static constexpr size_t kMaxIpsPerShard = 1u << 16;
    static constexpr auto kForgetAfter = std::chrono::seconds(60);

    struct IpState {
        uint32_t active[kKinds]{};
        double tokens = 0;
        std::chrono::steady_clock::time_point refilled{};
        std::chrono::steady_clock::time_point last_used{};
    };
    struct Shard {
        std::mutex mutex;
        std::unordered_map<std::string, IpState> ips;
    };

    static std::string ip_key(const asio::ip::address& ip) {
        if (ip.is_v4()) {
            const auto bytes = ip.to_v4().to_bytes();
            return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        }
        const auto v6 = ip.to_v6();
        if (v6.is_v4_mapped()) return ip_key(asio::ip::make_address_v4(asio::ip::v4_mapped, v6));
        // One customer usually holds a whole /64, so single addresses cost nothing to change.
        const auto bytes = v6.to_bytes();
        return std::string(reinterpret_cast<const char*>(bytes.data()), 8);
    }

    Shard& shard_for(const std::string& key) { return shards_[std::hash<std::string>{}(key) % kShards]; }

    static IpState* find_or_add(Shard& shard, const std::string& key) {
        auto it = shard.ips.find(key);
        if (it != shard.ips.end()) return &it->second;
        if (shard.ips.size() >= kMaxIpsPerShard) return nullptr;
        return &shard.ips[key];
    }

    static bool take_token(double& tokens, std::chrono::steady_clock::time_point& refilled,
                           uint32_t rate, std::chrono::steady_clock::time_point now) {
        const double elapsed = std::chrono::duration<double>(now - refilled).count();
        tokens = std::min(tokens + elapsed * rate, static_cast<double>(rate) * 2);
        refilled = now;
        if (tokens < 1) return false;
        tokens -= 1;
        return true;
    }

    void release(Kind kind, const std::string& key) {
        active_[kind].fetch_sub(1, std::memory_order_relaxed);
        auto& shard = shard_for(key);
        std::lock_guard lock(shard.mutex);
        auto it = shard.ips.find(key);
        if (it != shard.ips.end() && it->second.active[kind] > 0) --it->second.active[kind];
    }

    uint32_t max_[kKinds]{};
    uint32_t max_per_ip_[kKinds]{};
    std::atomic<uint64_t> active_[kKinds]{};
    uint32_t initials_per_second_ = 0;
    uint32_t initials_per_ip_per_second_ = 0;
    std::mutex overall_mutex_;
    double overall_tokens_ = 0;
    std::chrono::steady_clock::time_point overall_refilled_ = std::chrono::steady_clock::now();
    std::array<Shard, kShards> shards_;
};

static RouterLimiter g_limiter;

static std::vector<std::pair<const char*, uint64_t>> router_counters() {
    const auto count = [](const std::atomic<uint64_t>& c) { return c.load(std::memory_order_relaxed); };
    return {{"initial_too_small", count(g_stats.initial_too_small)},
            {"initial_rate_limited", count(g_stats.initial_rate_limited)},
            {"initial_undecryptable", count(g_stats.initial_undecryptable)},
            {"initial_no_sni", count(g_stats.initial_no_sni)},
            {"unknown_short_header", count(g_stats.unknown_short_header)},
            {"no_route", count(g_stats.no_route)},
            {"pending_cap_drops", count(g_stats.pending_cap_drops)},
            {"session_cap_drops", count(g_stats.session_cap_drops)},
            {"handshake_timeouts", count(g_stats.handshake_timeouts)},
            {"idle_timeouts", count(g_stats.idle_timeouts)},
            {"moved_clients", count(g_stats.moved_clients)},
            {"tcp_cap_drops", count(g_stats.tcp_cap_drops)},
            {"tcp_peek_timeouts", count(g_stats.tcp_peek_timeouts)}};
}

// Replaces the file in one step, so a reader never sees half of it. The group
// is what lets a backend container read it, as it does the rings beside it.
static void write_stats_file(const std::string& path, RouterStatsJson stats) {
    stats.sessions = g_limiter.active(RouterLimiter::Session);
    stats.pending = g_limiter.active(RouterLimiter::Pending);
    stats.tcp = g_limiter.active(RouterLimiter::TcpConnection);
    for (const auto& [name, value] : router_counters()) stats.counters[name] = value;
    g_site_stats.fill(stats.sites);
    g_ring_stats.fill(stats);

    static bool reported = false;
    const auto failed = [&](const std::string& what) {
        if (!reported) std::cerr << "[stats] Can't write " << path << ": " << what << "\n";
        reported = true;
    };
    std::string json;
    if (const auto ec = glz::write_json(stats, json)) return failed(glz::format_error(ec, json));
    const auto temporary = path + ".tmp";
    std::ofstream(temporary, std::ios::binary | std::ios::trunc) << json;
    std::error_code ec;
    if (std::filesystem::file_size(temporary, ec) != json.size() || ec) return failed("short write");
    namespace fs = std::filesystem;
    fs::permissions(temporary, fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read, ec);
    if (!ec) fs::rename(temporary, path, ec);
    if (ec) return failed(ec.message());
    reported = false;
}

static void print_stats(std::ostream& out, const std::string& extra = {}) {
    out << "[stats] sessions=" << g_limiter.active(RouterLimiter::Session)
        << " pending=" << g_limiter.active(RouterLimiter::Pending)
        << " tcp=" << g_limiter.active(RouterLimiter::TcpConnection);
    for (const auto& [name, value] : router_counters()) out << " " << name << "=" << value;
    out << extra;
    g_site_stats.print(out);
    g_ring_stats.print(out);
    out << std::endl;
}

class TcpSession : public std::enable_shared_from_this<TcpSession> {
    static constexpr size_t kPeekBufSize = 2048; // covers TLS 1.3 ClientHellos up to ~900 bytes
    static constexpr size_t kSpliceBufSize = 32768;

    tcp::socket client_;
    tcp::socket backend_;
    const RouteTable& routes_;
    std::unique_ptr<RouterLimiter::Slot> slot_;
    asio::steady_timer peek_timer_;
    const int peek_timeout_sec_;

    std::array<uint8_t, kPeekBufSize>   peek_buf_{};
    size_t                               peek_len_ = 0;

    // Splice buffers — allocated only after handshake
    std::vector<uint8_t> c2b_buf_; // client → backend
    std::vector<uint8_t> b2c_buf_; // backend → client
    SiteStats* stats_ = nullptr;   // Set once the SNI is known.

public:
    TcpSession(tcp::socket client, const RouteTable& routes,
               std::unique_ptr<RouterLimiter::Slot> slot, int peek_timeout_sec)
        : client_(std::move(client))
        , backend_(client_.get_executor())
        , routes_(routes)
        , slot_(std::move(slot))
        , peek_timer_(client_.get_executor())
        , peek_timeout_sec_(peek_timeout_sec)
    {}

    ~TcpSession() {
        if (stats_) stats_->active_tcp.fetch_sub(1, std::memory_order_relaxed);
    }

    void start() {
        if (peek_timeout_sec_ > 0) {
            peek_timer_.expires_after(std::chrono::seconds(peek_timeout_sec_));
            peek_timer_.async_wait([self = shared_from_this()](boost::system::error_code ec) {
                if (ec) return;
                g_stats.tcp_peek_timeouts.fetch_add(1, std::memory_order_relaxed);
                boost::system::error_code ignored;
                self->client_.close(ignored);
            });
        }
        // Read enough to capture a TLS ClientHello (typically < 300 bytes)
        client_.async_read_some(
            asio::buffer(peek_buf_),
            [self = shared_from_this()](boost::system::error_code ec, size_t n) {
                self->peek_timer_.cancel();
                if (ec) return;
                self->peek_len_ = n;
                self->on_peek();
            });
    }

private:
    void on_peek() {
        const auto span = std::span<const uint8_t>(peek_buf_.data(), peek_len_);
        const std::string_view sni = sni_from_tls_record(span);

        const ResolvedRoute* route = routes_.lookup_tcp(sni);
        if (!route) {
            std::cerr << "[TCP] No route for SNI '" << sni
                      << "' — dropping connection\n";
            return;
        }
        if (!sni.empty())
            std::clog << "[TCP] SNI='" << sni << "' → " << route->tcp_ep << "\n";

        stats_ = route->stats;
        stats_->active_tcp.fetch_add(1, std::memory_order_relaxed);
        stats_->tcp.fetch_add(1, std::memory_order_relaxed);
        stats_->tcp_in_bytes.fetch_add(peek_len_, std::memory_order_relaxed);
        boost::system::error_code peer_ec;
        if (const auto peer = client_.remote_endpoint(peer_ec); !peer_ec)
            g_geoip.count(stats_->tcp_countries, peer.address());

        backend_.async_connect(
            route->tcp_ep,
            [self = shared_from_this(), ep_str = route->tcp_ep.address().to_string()]
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
                self->stats_->tcp_in_bytes.fetch_add(n, std::memory_order_relaxed);
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
                self->stats_->tcp_out_bytes.fetch_add(n, std::memory_order_relaxed);
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

// ACME HTTP-01 challenge path prefix (RFC 8555 §8.3).
static constexpr std::string_view kAcmePrefix = "/.well-known/acme-challenge/";

// ACME tokens are base64url (RFC 8555 §8.1), so a valid token can never
// contain '/', '.' or a NUL — rejecting everything else makes path
// traversal out of the webroot impossible by construction.
static bool is_valid_acme_token(std::string_view tok)
{
    if (tok.empty() || tok.size() > 255) return false;
    return std::all_of(tok.begin(), tok.end(), [](unsigned char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
               (c >= '0' && c <= '9') || c == '-' || c == '_';
    });
}

// Reads the first line of the HTTP request to extract the Host header, then
// either serves an ACME HTTP-01 challenge (when a webroot is configured and
// the path matches) or sends a 301 redirect to https://<host><path>.
class HttpRedirectSession : public std::enable_shared_from_this<HttpRedirectSession> {
    static constexpr size_t kBufSize = 4096;
    tcp::socket sock_;
    std::shared_ptr<const std::string> acme_webroot_;
    std::array<char, kBufSize> buf_{};
    size_t len_ = 0;

public:
    HttpRedirectSession(tcp::socket s,
                        std::shared_ptr<const std::string> acme_webroot)
        : sock_(std::move(s)), acme_webroot_(std::move(acme_webroot)) {}

    void start() { read_more(); }

private:
    // Writes `resp` and closes. Held by shared_ptr so the buffer outlives the
    // async_write.
    void respond(std::string resp) {
        auto buf = std::make_shared<std::string>(std::move(resp));
        asio::async_write(
            sock_,
            asio::buffer(*buf),
            [self = shared_from_this(), buf](boost::system::error_code, size_t) {
                self->sock_.close();
            });
    }

    // Returns true if the request was handled as an ACME challenge.
    bool try_acme(std::string_view method, const std::string& path) {
        if (!acme_webroot_ || acme_webroot_->empty()) return false;
        if (method != "GET" && method != "HEAD") return false;
        if (!path.starts_with(kAcmePrefix)) return false;

        const std::string_view token(path.data() + kAcmePrefix.size(),
                                     path.size() - kAcmePrefix.size());
        std::string body;
        bool found = false;
        if (is_valid_acme_token(token)) {
            const std::string file =
                *acme_webroot_ + std::string(kAcmePrefix) + std::string(token);
            std::ifstream is(file, std::ios::binary);
            if (is) {
                body.assign(std::istreambuf_iterator<char>(is),
                            std::istreambuf_iterator<char>());
                found = true;
            }
        }

        if (found) {
            respond("HTTP/1.1 200 OK\r\n"
                    "Content-Type: application/octet-stream\r\n"
                    "Content-Length: " + std::to_string(body.size()) + "\r\n"
                    "Connection: close\r\n"
                    "\r\n" + (method == "HEAD" ? std::string() : body));
        } else {
            respond("HTTP/1.1 404 Not Found\r\n"
                    "Content-Length: 0\r\n"
                    "Connection: close\r\n"
                    "\r\n");
        }
        return true;
    }

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

        // Extract method and path from first request line: "GET /path HTTP/1.x"
        std::string_view method;
        std::string path = "/";
        const auto line_end = data.find("\r\n");
        if (line_end != std::string_view::npos) {
            const auto first_line = data.substr(0, line_end);
            const auto sp1 = first_line.find(' ');
            if (sp1 != std::string_view::npos) {
                method = first_line.substr(0, sp1);
                const auto sp2 = first_line.find(' ', sp1 + 1);
                if (sp2 != std::string_view::npos)
                    path = std::string(first_line.substr(sp1 + 1, sp2 - sp1 - 1));
            }
        }

        // Answer ACME HTTP-01 here rather than redirecting, so the challenge
        // token only ever has to exist on the router and never has to be
        // distributed to the backends.
        if (try_acme(method, path))
            return;

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

        respond("HTTP/1.1 301 Moved Permanently\r\n"
                "Location: https://" + host + path + "\r\n"
                "Content-Length: 0\r\n"
                "Connection: close\r\n"
                "\r\n");
    }
};

class HttpRedirectRouter {
    tcp::acceptor acceptor_;
    std::shared_ptr<const std::string> acme_webroot_;

public:
    HttpRedirectRouter(asio::io_context& ioc, const Config& cfg)
        : acceptor_(ioc,
                    tcp::endpoint(asio::ip::make_address(cfg.listen_address),
                                  cfg.http_redirect_port))
        , acme_webroot_(std::make_shared<const std::string>(cfg.acme_webroot))
    {
        acceptor_.set_option(tcp::acceptor::reuse_address(true));
    }

    void start() { accept(); }

private:
    void accept() {
        acceptor_.async_accept(
            [this](boost::system::error_code ec, tcp::socket sock) {
                if (!ec)
                    std::make_shared<HttpRedirectSession>(std::move(sock),
                                                          acme_webroot_)->start();
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
    const int peek_timeout_sec_;

public:
    TcpRouter(asio::io_context& ioc, const Config& cfg, const RouteTable& rt)
        : acceptor_(ioc,
                    tcp::endpoint(asio::ip::make_address(cfg.listen_address),
                                  cfg.listen_tcp_port))
        , routes_(rt)
        , peek_timeout_sec_(cfg.tcp_peek_timeout_sec)
    {
        acceptor_.set_option(tcp::acceptor::reuse_address(true));
    }

    void start() { accept(); }

private:
    void accept() {
        acceptor_.async_accept(
            [this](boost::system::error_code ec, tcp::socket sock) {
                if (!ec) {
                    boost::system::error_code peer_ec;
                    const auto peer = sock.remote_endpoint(peer_ec);
                    auto slot = peer_ec ? nullptr
                        : g_limiter.acquire(RouterLimiter::TcpConnection, peer.address());
                    if (slot) {
                        sock.set_option(tcp::no_delay(true));
                        std::make_shared<TcpSession>(std::move(sock), routes_, std::move(slot),
                                                     peek_timeout_sec_)->start();
                    } else {
                        g_stats.tcp_cap_drops.fetch_add(1, std::memory_order_relaxed);
                    }
                }
                if (ec != asio::error::operation_aborted)
                    accept();
            });
    }
};

//==============================================================================
// UDP router — QUIC SNI routing via per-session ephemeral sockets
//==============================================================================

// Server connection IDs seen on SHM routes, so a client that shows up from a new
// address can still be routed: the ID stays the same when a NAT rebinds it, and
// it is all a short header carries.  Shared because the new address may land on
// another worker.  Nothing here is trusted: the backend gets the real sender and
// validates the new path before replying to it.
class ShmCidRoutes {
public:
    struct Route {
        ShmIngressWriter* writer;
        udp::endpoint backend_ep;
        SiteStats* stats;
    };

    void learn(std::string_view cid, const Route& route) {
        const auto now = std::chrono::steady_clock::now();
        std::lock_guard lock(mutex_);
        if (routes_.size() < kMaxEntries || routes_.contains(std::string(cid)))
            routes_[std::string(cid)] = Entry{route, now};
    }

    std::optional<Route> find(std::string_view cid) {
        std::lock_guard lock(mutex_);
        auto it = routes_.find(std::string(cid));
        if (it == routes_.end()) return std::nullopt;
        return it->second.route;
    }

    void expire(std::chrono::seconds timeout) {
        const auto now = std::chrono::steady_clock::now();
        std::lock_guard lock(mutex_);
        for (auto it = routes_.begin(); it != routes_.end(); ) {
            if (now - it->second.last_seen > timeout) it = routes_.erase(it);
            else ++it;
        }
    }

private:
    static constexpr size_t kMaxEntries = 1u << 20;
    struct Entry {
        Route route;
        std::chrono::steady_clock::time_point last_seen;
    };
    std::mutex mutex_;
    std::unordered_map<std::string, Entry> routes_;
};

static ShmCidRoutes g_shm_cid_routes;

// The destination connection ID of a short-header packet, if this is one.
static std::optional<std::string_view> short_header_cid(std::span<const uint8_t> datagram) {
    if (datagram.size() < 1 + kQuicServerScidLen || (datagram[0] & 0x80u)) return std::nullopt;
    return std::string_view(reinterpret_cast<const char*>(datagram.data() + 1), kQuicServerScidLen);
}

// Pending QUIC connection: buffer CRYPTO fragments until SNI is extractable.
struct PendingQuicSession {
    udp::endpoint        client_ep;     // who to route back to once SNI is known
    std::vector<uint8_t> initial_dcid;  // client's original DCID (for diagnostics)
    // CRYPTO stream fragments keyed by stream offset.
    std::map<uint64_t, std::vector<uint8_t>> crypto_frags;
    // Raw UDP datagrams queued for replay once we route the session.
    std::vector<std::vector<uint8_t>> queued_pkts;
    std::chrono::steady_clock::time_point created_at = std::chrono::steady_clock::now();
    std::unique_ptr<RouterLimiter::Slot> slot;
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
// SHM mode (UdpSession::shm_ingress is non-null):
//   Packets are written directly to the per-backend c2s SHM ring.  No
//   backend_sock is created; back_buf and backend_port are unused.
//
// UDP mode (UdpSession::shm_ingress is null):
//   A per-session ephemeral UDP socket forwards packets to the backend.
struct UdpSession {
    udp::endpoint   client_ep;
    udp::endpoint   backend_ep;
    // UDP-mode only — null in SHM mode.
    std::unique_ptr<udp::socket> backend_sock;
    uint16_t        backend_port = 0;
    std::chrono::steady_clock::time_point last_seen;
    std::vector<uint8_t> back_buf; // UDP mode only: a session can be a spoofed packet's cost.
    SiteStats* stats = nullptr;
    std::unique_ptr<RouterLimiter::Slot> slot;
    // Has sent a short header, which a client can't before finishing the handshake.
    bool established = false;
    // SHM mode only — non-owning pointer into UdpRouter::shm_ingress_map_.
    ShmIngressWriter* shm_ingress = nullptr;
    // The connection ID last published to g_shm_cid_routes, and when.  Comparing
    // first keeps the shared table's lock off the per-packet path.
    std::string learned_cid;
    std::chrono::steady_clock::time_point learned_at{};

    ~UdpSession() {
        if (stats) stats->active_sessions.fetch_sub(1, std::memory_order_relaxed);
    }

    void set_stats(SiteStats* site) {
        stats = site;
        stats->active_sessions.fetch_add(1, std::memory_order_relaxed);
    }

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
        , back_buf(65536)
    {}
};

class UdpRouter {
    static constexpr size_t kRecvBufSize = 65536;

    asio::io_context& ioc_;
    udp::socket       listen_sock_;     // bound to 0.0.0.0:port
    const RouteTable& routes_;
    const int         timeout_sec_;
    const int         handshake_timeout_sec_;

    // Used only by the non-Linux single-datagram fallback in recv().
    [[maybe_unused]] std::array<uint8_t, kRecvBufSize> recv_buf_{};
    [[maybe_unused]] udp::endpoint                     sender_ep_;

#if defined(__linux__)
    // Batched ingress via recvmmsg (+ UDP_GRO): one wakeup drains many client
    // datagrams instead of one per async op. GRO-coalesced buffers are re-split
    // per segment before routing, since segments may belong to different
    // sessions' coalesced-QUIC-packet streams.
    static constexpr int kRecvBatch = 32;
    struct alignas(16) RecvCtrlBuf { uint8_t bytes[CMSG_SPACE(sizeof(int))]; };
    std::array<std::array<uint8_t, kRecvBufSize>, kRecvBatch> batch_bufs_{};
    std::array<sockaddr_storage, kRecvBatch>                  batch_addrs_{};
    std::array<RecvCtrlBuf, kRecvBatch>                       batch_ctrl_{};
    std::array<iovec, kRecvBatch>                             batch_iov_{};
    std::array<mmsghdr, kRecvBatch>                           batch_msgs_{};
#endif

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
    static constexpr size_t kMinInitialDatagram = 1200; // RFC 9000 §14.1
    static constexpr auto   kPendingTimeout    = std::chrono::seconds(5);

    asio::steady_timer gc_timer_;

    // SHM channels (populated from cfg.routes entries with non-empty shm_ingress_channel):
    //   shm_ingress_map_  — one c2s ring per backend, keyed by channel name.
    //   shm_egress_map_   — one s2c ring per distinct egress channel: the
    //                       top-level cfg.shm_egress_channel and any a route
    //                       names for itself.  Each reader has its own thread,
    //                       reads packets from Http3Server and sendmsg to client.
    // When a route has no shm_ingress_channel, a per-session UDP backend_sock is used.
    std::unordered_map<std::string, std::unique_ptr<ShmIngressWriter>> shm_ingress_map_;
    std::unordered_map<std::string, std::unique_ptr<ShmEgressReader>> shm_egress_map_;

public:
    // Single-worker constructor: creates and binds the socket internally.
    UdpRouter(asio::io_context& ioc, const Config& cfg, const RouteTable& rt)
        : ioc_(ioc)
        , listen_sock_(ioc, udp::endpoint(asio::ip::make_address(cfg.listen_address),
                                          cfg.listen_udp_port))
        , routes_(rt)
        , timeout_sec_(cfg.udp_session_timeout_sec)
        , handshake_timeout_sec_(cfg.udp_handshake_timeout_sec)
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
        , handshake_timeout_sec_(cfg.udp_handshake_timeout_sec)
        , gc_timer_(ioc)
    {
        init_shm(cfg, worker_index, is_primary);
    }

    int socket_fd() noexcept { return listen_sock_.native_handle(); }

    void start() {
#if defined(__linux__) && defined(UDP_GRO)
        // Coalesce inbound client datagrams; drain_recv_batch() reads the
        // per-message UDP_GRO segment size and re-splits before routing.
        {
            int gro_on = 1;
            if (::setsockopt(listen_sock_.native_handle(), SOL_UDP, UDP_GRO,
                             &gro_on, sizeof(gro_on)) != 0) {
                std::cerr << "[UDP] UDP_GRO not enabled: "
                          << std::strerror(errno) << "\n";
            }
        }
#endif
        recv();
        schedule_gc();
        for (const auto& [name, writer] : shm_ingress_map_) {
            g_ring_stats.add(*writer);
            std::clog << "  SHM ingress channel: " << writer->ring_name() << "\n";
        }
        for (const auto& [name, reader] : shm_egress_map_) {
            g_ring_stats.add(*reader);
            reader->start();
            std::clog << "  SHM egress  channel: " << reader->ring_name() << "\n";
        }
    }

private:
    static size_t ring_bytes(uint32_t kib, size_t fallback) {
        return kib ? size_t(kib) * 1024 : fallback;
    }

    void init_shm(const Config& cfg, size_t worker_index, bool is_primary) {
        const bool creates_rings = (worker_index == 0);
        // Create one ingress ring per route that names a SHM channel.
        // Worker 0 creates each ring; workers 1..N-1 open the existing ring
        // (MPSC: multiple UDP worker threads are safe producers).
        for (const auto& r : cfg.routes) {
            if (!r.shm_ingress_channel.empty() && !r.udp_backend.empty()) {
                if (shm_ingress_map_.find(r.shm_ingress_channel) == shm_ingress_map_.end()) {
                    shm_ingress_map_.emplace(r.shm_ingress_channel,
                        std::make_unique<ShmIngressWriter>(
                            r.shm_ingress_channel, creates_rings,
                            ring_bytes(r.shm_ingress_ring_kib, kShmIngressRingSize)));
                }
            }
        }
        // Create the egress rings (s2c): the top-level one, and one per route
        // that names its own.  Only the primary worker (worker 0) drains them.
        // Two routes naming one channel share it, sized by the first.
        if (is_primary) {
            auto add_egress = [&](const std::string& channel, uint32_t kib) {
                if (channel.empty() || shm_egress_map_.count(channel))
                    return;
                shm_egress_map_.emplace(channel, std::make_unique<ShmEgressReader>(
                    channel, listen_sock_.native_handle(),
                    ring_bytes(kib, kShmEgressRingSize)));
            };
            add_egress(cfg.shm_egress_channel, cfg.shm_egress_ring_kib);
            for (const auto& r : cfg.routes)
                if (!r.shm_ingress_channel.empty() && !r.udp_backend.empty())
                    add_egress(r.shm_egress_channel, r.shm_egress_ring_kib);
        }
    }

    void recv() {
#if defined(__linux__)
        listen_sock_.async_wait(
            asio::socket_base::wait_read,
            [this](boost::system::error_code ec) {
                if (!ec) {
                    // asio's epoll reactor is edge-triggered: drain the socket
                    // fully before re-arming, or the remainder stalls until the
                    // next datagram. recvmmsg returning < kRecvBatch means empty.
                    while (drain_recv_batch() >= kRecvBatch) {
                    }
                }
                if (ec != asio::error::operation_aborted) recv();
            });
#else
        listen_sock_.async_receive_from(
            asio::buffer(recv_buf_),
            sender_ep_,
            [this](boost::system::error_code ec, size_t n) {
                if (!ec) on_packet(recv_buf_.data(), n, sender_ep_);
                if (ec != asio::error::operation_aborted) recv();
            });
#endif
    }

#if defined(__linux__)
    // Drains up to kRecvBatch client datagrams in one recvmmsg. Returns the
    // recvmmsg result (n>=1 on success, <0 on EAGAIN/error). Each message may be
    // a UDP_GRO super-buffer, re-split into individual datagrams before routing.
    int drain_recv_batch() {
        for (int i = 0; i < kRecvBatch; ++i) {
            batch_iov_[i].iov_base = batch_bufs_[i].data();
            batch_iov_[i].iov_len  = batch_bufs_[i].size();
            msghdr& mh = batch_msgs_[i].msg_hdr;
            mh.msg_name       = &batch_addrs_[i];
            mh.msg_namelen    = sizeof(sockaddr_storage);
            mh.msg_iov        = &batch_iov_[i];
            mh.msg_iovlen     = 1;
            mh.msg_control    = batch_ctrl_[i].bytes;
            mh.msg_controllen = sizeof(batch_ctrl_[i].bytes);
            mh.msg_flags      = 0;
            batch_msgs_[i].msg_len = 0;
        }

        int n = ::recvmmsg(listen_sock_.native_handle(), batch_msgs_.data(),
                           kRecvBatch, MSG_DONTWAIT, nullptr);
        if (n <= 0) {
            return n; // <0 => EAGAIN/error (socket drained)
        }

        for (int i = 0; i < n; ++i) {
            msghdr& mh = batch_msgs_[i].msg_hdr;
            const size_t len = batch_msgs_[i].msg_len;
            if (len == 0) {
                continue;
            }

            // Reconstruct the client endpoint from the returned sockaddr.
            udp::endpoint sender;
            if (mh.msg_namelen == 0 || mh.msg_namelen > sender.capacity()) {
                continue;
            }
            std::memcpy(sender.data(), &batch_addrs_[i], mh.msg_namelen);
            sender.resize(mh.msg_namelen);

            // A UDP_GRO cmsg carries the size of each coalesced segment.
            unsigned int gso_size = 0;
            for (cmsghdr* cm = CMSG_FIRSTHDR(&mh); cm; cm = CMSG_NXTHDR(&mh, cm)) {
                if (cm->cmsg_level == SOL_UDP && cm->cmsg_type == UDP_GRO) {
                    int seg = 0;
                    std::memcpy(&seg, CMSG_DATA(cm), sizeof(seg));
                    if (seg > 0) {
                        gso_size = static_cast<unsigned int>(seg);
                    }
                    break;
                }
            }

            const uint8_t* base = batch_bufs_[i].data();
            if (gso_size == 0 || gso_size >= len) {
                on_packet(base, len, sender);
            } else {
                // Re-split the coalesced buffer into individual UDP datagrams.
                for (size_t off = 0; off < len; off += gso_size) {
                    const size_t seg = std::min<size_t>(gso_size, len - off);
                    on_packet(base + off, seg, sender);
                }
            }
        }
        return n;
    }
#endif // __linux__

    void on_packet(const uint8_t* data, size_t n, const udp::endpoint& sender) {
        const auto datagram = std::span<const uint8_t>(data, n);
        const auto it       = sessions_.find(sender);

        if (quic_debug_enabled && it == sessions_.end()) {
            // Log every new-session datagram: size and first few bytes.
            std::cerr << "[QUIC] datagram n=" << n << " from " << sender
                      << " bytes[0..3]=";
            for (size_t i = 0; i < std::min(n, size_t{4}); ++i)
                std::cerr << std::hex << std::setw(2) << std::setfill('0')
                          << (int)data[i] << ' ';
            std::cerr << std::dec << '\n';
        }

        if (it != sessions_.end()) {
            // Existing session — forward to backend.
            it->second->last_seen = std::chrono::steady_clock::now();
            if (n > 0 && !(data[0] & 0x80u) && !it->second->established) {
                it->second->established = true;
                g_geoip.count(it->second->stats->quic_countries, it->second->client_ep.address());
            }
            auto* site = it->second->stats;
            site->packets.fetch_add(1, std::memory_order_relaxed);
            site->bytes.fetch_add(n, std::memory_order_relaxed);
            if (it->second->shm_ingress) {
                learn_connection_id(*it->second, datagram);
                // SHM mode: write to this session's ingress ring.
                const auto ep_data = sender.data();
                if (!it->second->shm_ingress->write_packet(
                        static_cast<const sockaddr*>(static_cast<const void*>(ep_data)),
                        static_cast<socklen_t>(sender.size()),
                        datagram.data(), datagram.size())) {
                    site->ring_full_drops.fetch_add(1, std::memory_order_relaxed);
                }
            } else {
                it->second->backend_sock->async_send(
                    asio::buffer(datagram.data(), datagram.size()),
                    [](boost::system::error_code, size_t) {});
            }
            return;
        }

        if (route_moved_client(datagram, sender)) return;

        // Everything below costs CPU or memory for a client nobody knows yet,
        // so what is obviously not a new connection stops here.
        if (n == 0 || !(data[0] & 0x80u)) {
            g_stats.unknown_short_header.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (n < kMinInitialDatagram) {
            g_stats.initial_too_small.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (!g_limiter.allow_initial(sender.address())) {
            g_stats.initial_rate_limited.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        // Walk all coalesced QUIC Initial packets in this UDP datagram.
        // RFC 9000 §12.2: multiple QUIC packets may be concatenated in one UDP datagram.
        // The fragment containing stream_off=0 may be in the 2nd or later packet.
        size_t pkt_off = 0;
        bool   any_frag = false;
        std::string route_dcid_key;

        while (pkt_off < n) {
            const auto pkt_span = std::span<const uint8_t>(data + pkt_off, n - pkt_off);

            // Compute packet boundary without decrypting (for coalesced-packet skipping).
            const size_t pkt_len = quic_initial_packet_len(pkt_span);

            QUIC_DBG_PLAIN("  [pkt] off=" << pkt_off << " pkt_len=" << pkt_len
                           << " first_byte=0x" << std::hex << (pkt_span.empty() ? 0 : (int)pkt_span[0]) << std::dec);

            auto frags = quic_extract_crypto(pkt_span);
            for (auto& frag : frags) {
                const std::string dcid_key(frag.dcid.begin(), frag.dcid.end());
                if (!any_frag) route_dcid_key = dcid_key;
                any_frag = true;

                auto pending_it = pending_.find(dcid_key);
                if (pending_it == pending_.end()) {
                    auto slot = g_limiter.acquire(RouterLimiter::Pending, sender.address());
                    if (!slot) {
                        g_stats.pending_cap_drops.fetch_add(1, std::memory_order_relaxed);
                        return;
                    }
                    pending_it = pending_.try_emplace(dcid_key).first;
                    pending_it->second.slot = std::move(slot);
                }
                auto& pend = pending_it->second;
                if (pend.client_ep == udp::endpoint{}) {
                    pend.client_ep    = sender;
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
                if (pend.crypto_frags.size() < kMaxPendingPackets || pend.crypto_frags.contains(frag.stream_offset))
                    pend.crypto_frags[frag.stream_offset] = std::move(frag.data);
            }

            if (pkt_len == 0 || pkt_off + pkt_len > n) break; // not QUIC Initial or malformed
            pkt_off += pkt_len;
        }

        if (!any_frag) { // no QUIC Initial packets in this datagram
            g_stats.initial_undecryptable.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        // Queue the whole datagram under the first DCID found.
        auto& pend = pending_[route_dcid_key];
        if (pend.queued_pkts.size() < kMaxPendingPackets)
            pend.queued_pkts.emplace_back(data, data + n);

        // Try to assemble a contiguous CRYPTO buffer starting at offset 0.
        auto sni_opt = try_assemble_sni(pend.crypto_frags);
        if (!sni_opt) return; // need more fragments

        const std::string& sni = *sni_opt;
        const ResolvedRoute* route = routes_.lookup_udp(sni);
        if (!route) {
            std::cerr << "[UDP] No route for SNI '" << sni << "' — dropping\n";
            g_stats.no_route.fetch_add(1, std::memory_order_relaxed);

            pending_.erase(route_dcid_key);
            return;
        }
        std::clog << "[UDP] SNI='" << sni << "' → " << route->udp_ep << "\n";

        // Resolve the SHM ingress writer for this route (null = UDP mode).
        ShmIngressWriter* shm_writer = nullptr;
        if (!route->shm_ingress_channel.empty()) {
            auto sit = shm_ingress_map_.find(route->shm_ingress_channel);
            if (sit != shm_ingress_map_.end())
                shm_writer = sit->second.get();
        }

        auto slot = g_limiter.acquire(RouterLimiter::Session, pend.client_ep.address());
        if (!slot) {
            g_stats.session_cap_drops.fetch_add(1, std::memory_order_relaxed);
            pending_.erase(route_dcid_key);
            return;
        }

        // Create the session and replay all queued packets.
        std::shared_ptr<UdpSession> sess;
        if (shm_writer) {
            // SHM mode: no backend socket needed.
            sess = std::make_shared<UdpSession>(pend.client_ep, route->udp_ep);
            sess->shm_ingress = shm_writer;
        } else {
            // UDP mode: create ephemeral socket and connect to backend.
            sess = std::make_shared<UdpSession>(ioc_, pend.client_ep, route->udp_ep);
            sess->backend_sock->connect(route->udp_ep);
            sess->backend_port = sess->backend_sock->local_endpoint().port();
            std::clog << "[UDP] Session backend_port=" << sess->backend_port
                      << " client=" << sess->client_ep << "\n";
        }
        sess->slot = std::move(slot);
        sess->set_stats(route->stats);
        route->stats->sessions.fetch_add(1, std::memory_order_relaxed);
        sessions_.emplace(pend.client_ep, sess);

        auto queued = std::move(pend.queued_pkts);
        pending_.erase(route_dcid_key);

        if (shm_writer) {
            // Write all queued packets to the backend's ingress ring.
            const auto& cep      = sess->client_ep;
            const auto  ep_data  = cep.data();
            const auto  ep_sa    = static_cast<const sockaddr*>(
                static_cast<const void*>(ep_data));
            const auto  ep_len   = static_cast<socklen_t>(cep.size());
            for (auto& qpkt : queued)
                shm_writer->write_packet(ep_sa, ep_len, qpkt.data(), qpkt.size());
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

    void learn_connection_id(UdpSession& sess, std::span<const uint8_t> datagram) {
        const auto cid = short_header_cid(datagram);
        if (!cid) return;
        const auto now = std::chrono::steady_clock::now();
        // Republished well inside the timeout, or an active ID would expire.
        const auto refresh = std::chrono::seconds(timeout_sec_) / 4;
        if (sess.learned_cid == *cid && now - sess.learned_at < refresh) return;
        sess.learned_cid = *cid;
        sess.learned_at  = now;
        g_shm_cid_routes.learn(*cid, {sess.shm_ingress, sess.backend_ep, sess.stats});
    }

    // A known connection ID from an unknown address is a client that moved.
    bool route_moved_client(std::span<const uint8_t> datagram, const udp::endpoint& sender) {
        const auto cid = short_header_cid(datagram);
        if (!cid) return false;
        const auto route = g_shm_cid_routes.find(*cid);
        if (!route) return false;

        auto slot = g_limiter.acquire(RouterLimiter::Session, sender.address());
        if (!slot) {
            g_stats.session_cap_drops.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
        auto sess = std::make_shared<UdpSession>(sender, route->backend_ep);
        sess->shm_ingress = route->writer;
        sess->slot = std::move(slot);
        sess->set_stats(route->stats); // The same client, so not a new session.
        sess->established = true;
        sessions_.emplace(sender, sess);
        g_stats.moved_clients.fetch_add(1, std::memory_order_relaxed);
        std::clog << "[UDP] Client moved to " << sender << "\n";
        learn_connection_id(*sess, datagram);
        const auto ep_data = sender.data();
        route->writer->write_packet(
            static_cast<const sockaddr*>(static_cast<const void*>(ep_data)),
            static_cast<socklen_t>(sender.size()),
            datagram.data(), datagram.size());
        return true;
    }

    void recv_from_backend(std::shared_ptr<UdpSession> sess) {
        sess->backend_sock->async_receive(
            asio::buffer(sess->back_buf),
            [this, sess](boost::system::error_code ec, size_t n) {
                if (ec) return; // session torn down
                sess->last_seen = std::chrono::steady_clock::now();
                sess->stats->reply_packets.fetch_add(1, std::memory_order_relaxed);
                sess->stats->reply_bytes.fetch_add(n, std::memory_order_relaxed);
                listen_sock_.async_send_to(
                    asio::buffer(sess->back_buf.data(), n),
                    sess->client_ep,
                    [](boost::system::error_code, size_t) {});
                recv_from_backend(sess);
            });
    }

    void schedule_gc() {
        // Often enough for the shortest timeout, but no busier than a second.
        gc_timer_.expires_after(std::chrono::seconds(
            std::max(1, std::min(timeout_sec_, handshake_timeout_sec_ > 0 ? handshake_timeout_sec_ : timeout_sec_) / 2)));
        gc_timer_.async_wait([this](boost::system::error_code ec) {
            if (ec) return;
            const auto now     = std::chrono::steady_clock::now();
            const auto timeout = std::chrono::seconds(timeout_sec_);
            const auto handshake_timeout = std::chrono::seconds(handshake_timeout_sec_);
            for (auto it = sessions_.begin(); it != sessions_.end(); ) {
                auto& sess = *it->second;
                const bool never_finished = !sess.established && handshake_timeout_sec_ > 0;
                if (now - sess.last_seen > (never_finished ? handshake_timeout : timeout)) {
                    (never_finished ? g_stats.handshake_timeouts : g_stats.idle_timeouts)
                        .fetch_add(1, std::memory_order_relaxed);
                    // Its pending receive holds a reference until the socket closes.
                    if (sess.backend_sock) {
                        boost::system::error_code ignored;
                        sess.backend_sock->close(ignored);
                    }
                    it = sessions_.erase(it);
                } else {
                    ++it;
                }
            }
            g_shm_cid_routes.expire(timeout);
            g_limiter.expire();
            // Evict stale pending reassembly entries (typically < 5 s old)
            for (auto it = pending_.begin(); it != pending_.end(); ) {
                if (now - it->second.created_at > kPendingTimeout) {
                    g_stats.initial_no_sni.fetch_add(1, std::memory_order_relaxed);
                    it = pending_.erase(it);
                } else {
                    ++it;
                }
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
  "acme_webroot": "/var/www/acme",
  "default_tcp_backend": "127.0.0.1:8443",
  "default_udp_backend": "127.0.0.1:4433",
  "udp_session_timeout_sec": 120,
  "shm_egress_channel": "",
  "num_workers": 4,
  "routes": [
    { "sni": "app.example.com",  "tcp_backend": "127.0.0.1:8443", "udp_backend": "127.0.0.1:4433", "shm_ingress_channel": "app_quic" },
    { "sni": "blog.example.com", "tcp_backend": "127.0.0.1:9443", "udp_backend": "127.0.0.1:4434", "shm_ingress_channel": "blog_quic" }
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
    g_limiter.configure(cfg);
    g_geoip.start(cfg.geoip_csv);

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
    if (cfg.http_redirect_port != 0) {
        std::clog << "  HTTP(redirect) " << cfg.listen_address << ":" << cfg.http_redirect_port;
        if (!cfg.acme_webroot.empty())
            std::clog << " ACME-webroot=" << cfg.acme_webroot;
    }
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

    const auto report_stats = [&] {
        std::ostringstream extra;
#if defined(NPRPC_ROUTER_REUSEPORT_BPF_ENABLED) && defined(SO_ATTACH_REUSEPORT_EBPF)
        if (reuseport_bpf) reuseport_bpf->print_totals(extra, num_workers);
#endif
        print_stats(std::clog, extra.str());
        if (g_geoip.enabled()) g_site_stats.print_countries(std::clog);
    };

    asio::steady_timer stats_timer(ioc);
    std::function<void()> schedule_stats = [&] {
        stats_timer.expires_after(std::chrono::seconds(cfg.stats_interval_sec));
        stats_timer.async_wait([&](boost::system::error_code ec) {
            if (ec) return;
            report_stats();
            schedule_stats();
        });
    };
    if (cfg.stats_interval_sec > 0) schedule_stats();

    const auto started = std::chrono::steady_clock::now();
    const auto report_stats_file = [&] {
        RouterStatsJson stats;
        stats.uptime_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count();
#if defined(NPRPC_ROUTER_REUSEPORT_BPF_ENABLED) && defined(SO_ATTACH_REUSEPORT_EBPF)
        if (reuseport_bpf) {
            const auto [routed, drops] = reuseport_bpf->totals(num_workers);
            stats.counters["bpf_routed"] = routed;
            stats.counters["bpf_drops"] = drops;
        }
#endif
        write_stats_file(cfg.stats_file, std::move(stats));
    };
    asio::steady_timer stats_file_timer(ioc);
    std::function<void()> schedule_stats_file = [&] {
        stats_file_timer.expires_after(std::chrono::seconds(std::max(1, cfg.stats_file_interval_sec)));
        stats_file_timer.async_wait([&](boost::system::error_code ec) {
            if (ec) return;
            report_stats_file();
            schedule_stats_file();
        });
    };
    if (!cfg.stats_file.empty()) {
        report_stats_file();
        schedule_stats_file();
    }

    ioc.run();  // main thread: TCP + signals (+ UDP in single-worker mode)

    for (auto& w : udp_workers) {
        if (w->thread.joinable()) w->thread.join();
    }
    report_stats();

#if defined(NPRPC_ROUTER_REUSEPORT_BPF_ENABLED) && defined(SO_ATTACH_REUSEPORT_EBPF)
    if (reuseport_bpf) reuseport_bpf->print_metrics(num_workers);
#endif

    return 0;
}

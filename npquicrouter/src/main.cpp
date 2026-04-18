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
#include <fstream>
#include <iostream>
#include <memory>
#include <span>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>

#include <glaze/json.hpp>

#include "quic_initial.hpp"
#include "sni_parser.hpp"

namespace asio = boost::asio;
using tcp = asio::ip::tcp;
using udp = asio::ip::udp;

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
    std::string  default_tcp_backend;  // fallback if no SNI match
    std::string  default_udp_backend;
    int          udp_session_timeout_sec = 120;
    std::vector<RouteEntry> routes;
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
        "default_tcp_backend",     &T::default_tcp_backend,
        "default_udp_backend",     &T::default_udp_backend,
        "udp_session_timeout_sec", &T::udp_session_timeout_sec,
        "routes",                  &T::routes);
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
// TCP proxy — one object per accepted connection
//==============================================================================

class TcpSession : public std::enable_shared_from_this<TcpSession> {
    static constexpr size_t kPeekBufSize = 512;
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

// One per active QUIC client-→-backend mapping.
struct UdpSession {
    udp::endpoint   client_ep;
    udp::endpoint   backend_ep;
    udp::socket     backend_sock;   // unique ephemeral port → backend
    std::chrono::steady_clock::time_point last_seen;

    // Receive buffer for backend → client direction
    std::array<uint8_t, 65536> back_buf{};

    UdpSession(asio::io_context& ioc,
               udp::endpoint cep,
               udp::endpoint bep)
        : client_ep(std::move(cep))
        , backend_ep(std::move(bep))
        , backend_sock(ioc, udp::endpoint(udp::v4(), 0)) // ephemeral port
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

    asio::steady_timer gc_timer_;

public:
    UdpRouter(asio::io_context& ioc, const Config& cfg, const RouteTable& rt)
        : ioc_(ioc)
        , listen_sock_(ioc, udp::endpoint(asio::ip::make_address(cfg.listen_address),
                                          cfg.listen_udp_port))
        , routes_(rt)
        , timeout_sec_(cfg.udp_session_timeout_sec)
        , gc_timer_(ioc)
    {}

    void start() {
        recv();
        schedule_gc();
    }

private:
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
        const auto pkt = std::span<const uint8_t>(recv_buf_.data(), n);
        const auto it  = sessions_.find(sender_ep_);

        if (it != sessions_.end()) {
            // Existing session — forward without decryption
            it->second->last_seen = std::chrono::steady_clock::now();
            it->second->backend_sock.async_send(
                asio::buffer(pkt.data(), pkt.size()),
                [](boost::system::error_code, size_t) {});
            return;
        }

        // New client — try to extract SNI from QUIC v1 Initial
        const std::string sni = sni_from_quic_initial(pkt);
        const udp::endpoint* ep = routes_.lookup_udp(sni);
        if (!ep) {
            std::cerr << "[UDP] No route for SNI '" << sni
                      << "' — dropping\n";
            return;
        }
        if (!sni.empty())
            std::clog << "[UDP] SNI='" << sni << "' → " << *ep << "\n";

        // Create session
        auto sess = std::make_shared<UdpSession>(ioc_, sender_ep_, *ep);
        sess->backend_sock.connect(*ep);
        sessions_.emplace(sender_ep_, sess);

        // Forward the Initial packet to backend
        sess->backend_sock.async_send(
            asio::buffer(pkt.data(), pkt.size()),
            [](boost::system::error_code, size_t) {});

        // Start receiving return traffic from backend
        recv_from_backend(sess);
    }

    void recv_from_backend(std::shared_ptr<UdpSession> sess) {
        sess->backend_sock.async_receive(
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
            schedule_gc();
        });
    }
};

//==============================================================================
// main
//==============================================================================

int main(int argc, char* argv[])
{
    if (argc < 2) {
        std::cerr << "Usage: npquicrouter <config.json>\n"
                  << "\nExample config.json:\n"
                  << R"({
  "listen_address": "0.0.0.0",
  "listen_tcp_port": 443,
  "listen_udp_port": 443,
  "default_tcp_backend": "127.0.0.1:8443",
  "default_udp_backend": "127.0.0.1:4433",
  "udp_session_timeout_sec": 120,
  "routes": [
    { "sni": "app.example.com",  "tcp_backend": "127.0.0.1:8443", "udp_backend": "127.0.0.1:4433" },
    { "sni": "blog.example.com", "tcp_backend": "127.0.0.1:9443", "udp_backend": "127.0.0.1:4434" }
  ]
})" << "\n";
        return 1;
    }

    // Load config
    std::ifstream f(argv[1]);
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

    // Start TCP and UDP routers
    TcpRouter tcp_router(ioc, cfg, routes);
    UdpRouter udp_router(ioc, cfg, routes);

    tcp_router.start();
    udp_router.start();

    std::clog << "npquicrouter listening — TCP "
              << cfg.listen_address << ":" << cfg.listen_tcp_port
              << "  UDP " << cfg.listen_address << ":" << cfg.listen_udp_port
              << "\n";

    // SIGINT/SIGTERM → stop io_context
    asio::signal_set signals(ioc, SIGINT, SIGTERM);
    signals.async_wait([&](boost::system::error_code, int sig) {
        std::clog << "\nSignal " << sig << " received — shutting down\n";
        ioc.stop();
    });

    ioc.run();
    return 0;
}

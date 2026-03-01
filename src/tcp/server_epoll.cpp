// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT
//
// Raw epoll-based TCP server — no Asio per-read dispatch overhead.
//
// Wire format (same as server_session_socket):
//   [uint32_t: body_len][body_len bytes: Header + payload]
//   where Header.size == body_len (the first field of the message is its size)
//
// One epoll fd, one dedicated thread.  All I/O (accept + read + dispatch +
// write) happens on that thread.  Servants are called synchronously.

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstring>
#include <memory>
#include <thread>
#include <unordered_map>

#include <nprpc/common.hpp>
#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/impl/session.hpp>

#include "../logging.hpp"

namespace nprpc::impl {

// ── per-connection state ──────────────────────────────────────────────────────

// Thin Session subclass: inherits handle_request() and ctx_ but never
// starts any Asio timers.  The executor is borrowed from g_rpc so that
// StreamManager (created in Session ctor) has a valid strand to post to
// if ever needed; the timers themselves remain idle.
struct EpollConn : Session {
  int fd_;

  // rx_: accumulates raw bytes from recv(); once a full message is present
  // (4-byte size prefix + size bytes) handle_request is invoked.
  flat_buffer rx_{flat_buffer::default_initial_size()};
  flat_buffer tx_{flat_buffer::default_initial_size()};

  // Session virtuals — server-side only; these will never be called.
  void timeout_action() final {}
  void send_receive(flat_buffer&, uint32_t) override { assert(false); }
  void send_receive_async(
      flat_buffer&&,
      std::optional<std::function<void(const boost::system::error_code&,
                                       flat_buffer&)>>&&,
      uint32_t) override
  {
    assert(false);
  }

  explicit EpollConn(int fd, boost::asio::any_io_executor ex)
      : Session(ex)
      , fd_(fd)
  {
    struct sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    getpeername(fd, reinterpret_cast<sockaddr*>(&addr), &len);
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
    ctx_.remote_endpoint =
        EndPoint(EndPointType::TcpTethered, std::string(ip), ntohs(addr.sin_port));
  }

  ~EpollConn() { ::close(fd_); }
};

// ── epoll acceptor ────────────────────────────────────────────────────────────

class EpollAcceptor {
  int  epfd_     = -1;
  int  listenfd_ = -1;

  std::thread           thread_;
  std::atomic<bool>     running_{false};

  std::unordered_map<int, std::shared_ptr<EpollConn>> conns_;

  // ── helpers ──────────────────────────────────────────────────────────────

  static void setup_conn_socket(int fd)
  {
    // Edge-triggered: we must drain the fd every time.
    int flags = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    int one = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    constexpr int kBuf = 4 * 1024 * 1024;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &kBuf, sizeof(kBuf));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &kBuf, sizeof(kBuf));
  }

  void add_to_epoll(int fd, uint32_t events)
  {
    epoll_event ev{};
    ev.events   = events;
    ev.data.fd  = fd;
    ::epoll_ctl(epfd_, EPOLL_CTL_ADD, fd, &ev);
  }

  void remove_conn(int fd)
  {
    ::epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr);
    conns_.erase(fd);  // ~EpollConn closes fd
  }

  // ── accept loop ──────────────────────────────────────────────────────────

  void do_accepts()
  {
    for (;;) {
      int cfd = ::accept4(listenfd_, nullptr, nullptr,
                          SOCK_NONBLOCK | SOCK_CLOEXEC);
      if (cfd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
        NPRPC_LOG_ERROR("epoll accept4: {}", ::strerror(errno));
        break;
      }
      setup_conn_socket(cfd);
      auto conn = std::make_shared<EpollConn>(cfd, g_rpc->ioc().get_executor());
      conns_.emplace(cfd, std::move(conn));
      // EPOLLRDHUP lets us detect clean peer close without a zero-length read.
      add_to_epoll(cfd, EPOLLIN | EPOLLET | EPOLLRDHUP);
    }
  }

  // ── blocking write helper ─────────────────────────────────────────────────
  // Called after handle_request() on the epoll thread.  Uses poll() for
  // backpressure when the kernel send-buffer is full (rare with 4 MB sndbuf).

  bool blocking_write(int fd, const void* data, size_t len)
  {
    const auto* p = static_cast<const uint8_t*>(data);
    while (len > 0) {
      ssize_t w = ::send(fd, p, len, MSG_NOSIGNAL);
      if (w < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          struct pollfd pfd{fd, POLLOUT, 0};
          if (::poll(&pfd, 1, 5000) <= 0) return false;
          continue;
        }
        return false;
      }
      p   += w;
      len -= static_cast<size_t>(w);
    }
    return true;
  }

  // ── readable: drain socket, process complete messages ─────────────────────
  // Returns false if the connection should be closed.

  bool do_read(EpollConn& c)
  {
    // Drain all available bytes (edge-triggered: must read until EAGAIN).
    for (;;) {
      // Always ask for at least 4 KB to amortise the recv syscall.
      constexpr size_t kChunk = 64 * 1024;
      auto mbuf = c.rx_.prepare(kChunk);
      ssize_t n = ::recv(c.fd_, mbuf.data(), mbuf.size(), 0);
      if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;  // all drained
        NPRPC_LOG_ERROR("epoll recv fd={}: {}", c.fd_, ::strerror(errno));
        return false;
      }
      if (n == 0) return false;  // peer closed

      c.rx_.commit(static_cast<size_t>(n));

      // Process as many complete messages as we can without another recv().
      // Wire frame: [uint32_t body_len][body_len bytes of message]
      while (c.rx_.size() >= 4) {
        auto* raw      = static_cast<const uint8_t*>(c.rx_.data().data());
        uint32_t blen  = *reinterpret_cast<const uint32_t*>(raw);

        if (blen > max_message_size) {
          NPRPC_LOG_ERROR("epoll: oversized msg {} bytes on fd={}", blen, c.fd_);
          return false;
        }

        size_t total = 4u + blen;
        if (c.rx_.size() < total) break;  // incomplete message — keep reading

        bool needs_reply = c.handle_request(c.rx_, c.tx_);

        if (needs_reply) {
          auto cd = c.tx_.cdata();
          if (!blocking_write(c.fd_, cd.data(), cd.size())) return false;
          c.tx_.consume(c.tx_.size());
        }

        c.rx_.consume(total);
      }
    }
    return true;
  }

  // ── epoll thread main loop ────────────────────────────────────────────────

  void run()
  {
    constexpr int kMaxEv = 64;
    epoll_event events[kMaxEv];

    while (running_.load(std::memory_order_relaxed)) {
      int n = ::epoll_wait(epfd_, events, kMaxEv, 200 /*ms*/);
      if (n < 0) {
        if (errno == EINTR) continue;
        NPRPC_LOG_ERROR("epoll_wait: {}", ::strerror(errno));
        break;
      }
      for (int i = 0; i < n; ++i) {
        int fd          = events[i].data.fd;
        uint32_t evmask = events[i].events;

        if (fd == listenfd_) {
          do_accepts();
          continue;
        }

        auto it = conns_.find(fd);
        if (it == conns_.end()) continue;

        bool closed = (evmask & (EPOLLHUP | EPOLLERR | EPOLLRDHUP)) != 0;

        if (!closed && (evmask & EPOLLIN))
          closed = !do_read(*it->second);

        if (closed) remove_conn(fd);
      }
    }

    // Clean up remaining connections
    conns_.clear();
  }

public:
  void start(unsigned short port)
  {
    listenfd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (listenfd_ < 0) {
      NPRPC_LOG_ERROR("epoll: socket(): {}", ::strerror(errno));
      return;
    }

    int one = 1;
    ::setsockopt(listenfd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    ::setsockopt(listenfd_, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);
    if (::bind(listenfd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
      NPRPC_LOG_ERROR("epoll: bind port {}: {}", port, ::strerror(errno));
      ::close(listenfd_);
      listenfd_ = -1;
      return;
    }
    ::listen(listenfd_, SOMAXCONN);

    epfd_ = ::epoll_create1(EPOLL_CLOEXEC);
    // Listen socket is level-triggered so we don't miss connections.
    add_to_epoll(listenfd_, EPOLLIN);

    running_.store(true);
    thread_ = std::thread([this] { run(); });

    NPRPC_LOG_INFO("EpollTCP listener started on port {}", port);
  }

  void stop()
  {
    running_.store(false);
    if (thread_.joinable()) thread_.join();
    // conns_ cleared in run() before thread exits
    if (epfd_ >= 0)     { ::close(epfd_);     epfd_     = -1; }
    if (listenfd_ >= 0) { ::close(listenfd_); listenfd_ = -1; }
  }

  ~EpollAcceptor() { stop(); }
};

// ── module entry points ───────────────────────────────────────────────────────

static std::unique_ptr<EpollAcceptor> g_epoll_acceptor;

void init_socket_epoll(unsigned short port)
{
  g_epoll_acceptor = std::make_unique<EpollAcceptor>();
  g_epoll_acceptor->start(port);
}

void stop_socket_epoll()
{
  if (g_epoll_acceptor) {
    g_epoll_acceptor->stop();
    g_epoll_acceptor.reset();
  }
}

} // namespace nprpc::impl

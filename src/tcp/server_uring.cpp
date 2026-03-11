// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT
//
// io_uring-based TCP server.
//
// Unlike the epoll server which calls recv/send in a tight userspace loop after
// epoll_wait tells us the fd is ready, here we submit the I/O operations
// themselves (IORING_OP_RECV / IORING_OP_SEND) to the kernel ring.  The kernel
// performs the actual data movement and posts a completion queue entry (CQE)
// when done.  This eliminates the epoll_wait → ready → recv syscall round-trip:
// the recv IS the submission; we just wait for its CQE.
//
// Threading: one dedicated thread runs io_uring_wait_cqe_timeout in a loop and
// dispatches all accepted connections on that same thread.  This is optimal for
// request-response RPC because each connection is sequential (recv → dispatch →
// send → recv) — no parallelism to exploit.  Per-connection threads would burn
// 8 MB stack × N and add scheduling latency.  IORING_SETUP_SQPOLL would push
// even further by having a kernel thread spin on the SQ, but that requires
// CAP_SYS_NICE and is left as a commented-out option.
//
// User-data encoding in CQEs:
//   accept op : kAcceptUD  (UINT64_MAX sentinel)
//   recv   op : (fd << 1) | 0
//   send   op : (fd << 1) | 1
// Using the fd instead of a raw pointer avoids any dangling-pointer risk when a
// connection is removed mid-batch.

#include <liburing.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
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

struct IoUringConn : Session {
  int        fd_;
  flat_buffer rx_{flat_buffer::default_initial_size()};
  flat_buffer tx_{flat_buffer::default_initial_size()};
  size_t      tx_offset_ = 0;  // bytes already sent for the current reply

  // Server-side only — these paths are never taken.
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

  explicit IoUringConn(int fd, boost::asio::any_io_executor ex)
      : Session(ex)
      , fd_(fd)
  {
    struct sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    getpeername(fd, reinterpret_cast<sockaddr*>(&addr), &len);
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
    ctx_.remote_endpoint =
        EndPoint(EndPointType::TcpPrivate, std::string(ip), ntohs(addr.sin_port));
  }

  ~IoUringConn() { ::close(fd_); }
};

// ── io_uring acceptor ─────────────────────────────────────────────────────────

class IoUringAcceptor {
  static constexpr unsigned kRingSize  = 256;
  static constexpr size_t   kRecvChunk = 64 * 1024;

  // CQE user-data helpers
  static constexpr uint64_t kAcceptUD = UINT64_MAX;
  static constexpr uint64_t kTagRecv  = 0ULL;
  static constexpr uint64_t kTagSend  = 1ULL;

  static uint64_t make_ud(int fd, uint64_t tag) noexcept
  {
    return (static_cast<uint64_t>(fd) << 1) | tag;
  }
  static int fd_from_ud(uint64_t ud) noexcept
  {
    return static_cast<int>(ud >> 1);
  }
  static uint64_t tag_from_ud(uint64_t ud) noexcept { return ud & 1ULL; }

  io_uring     ring_{};
  int          listenfd_ = -1;
  std::thread  thread_;
  std::atomic<bool> running_{false};

  std::unordered_map<int, std::shared_ptr<IoUringConn>> conns_;

  // ── SQE helpers ────────────────────────────────────────────────────────────

  // Get an SQE, flushing the ring first if it's full.
  io_uring_sqe* get_sqe()
  {
    io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
    if (!sqe) {
      io_uring_submit(&ring_);
      sqe = io_uring_get_sqe(&ring_);
    }
    return sqe;
  }

  static void setup_conn_socket(int fd)
  {
    int one = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    constexpr int kBuf = 4 * 1024 * 1024;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &kBuf, sizeof(kBuf));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &kBuf, sizeof(kBuf));
  }

  void submit_multishot_accept()
  {
    auto* sqe = get_sqe();
    io_uring_prep_multishot_accept(sqe, listenfd_, nullptr, nullptr, SOCK_CLOEXEC);
    io_uring_sqe_set_data64(sqe, kAcceptUD);
  }

  void submit_recv(IoUringConn& c)
  {
    auto  mbuf = c.rx_.prepare(kRecvChunk);
    auto* sqe  = get_sqe();
    io_uring_prep_recv(sqe, c.fd_, mbuf.data(), mbuf.size(), 0);
    io_uring_sqe_set_data64(sqe, make_ud(c.fd_, kTagRecv));
  }

  void submit_send(IoUringConn& c)
  {
    auto  cd        = c.tx_.cdata();
    auto* p         = static_cast<const uint8_t*>(cd.data()) + c.tx_offset_;
    size_t remaining = cd.size() - c.tx_offset_;
    auto* sqe = get_sqe();
    io_uring_prep_send(sqe, c.fd_, p, remaining, MSG_NOSIGNAL);
    io_uring_sqe_set_data64(sqe, make_ud(c.fd_, kTagSend));
  }

  void remove_conn(int fd)
  {
    conns_.erase(fd);  // ~IoUringConn closes fd
  }

  // ── CQE handlers ───────────────────────────────────────────────────────────

  void on_accept(int res, uint32_t flags)
  {
    if (res < 0) {
      if (res != -EAGAIN && res != -ECONNABORTED)
        NPRPC_LOG_ERROR("uring: accept: {}", ::strerror(-res));
      // Multishot re-arms itself unless it's been cancelled.
      if (!(flags & IORING_CQE_F_MORE))
        submit_multishot_accept();
      return;
    }

    int cfd = res;
    setup_conn_socket(cfd);
    auto conn = std::make_shared<IoUringConn>(cfd, g_rpc->ioc().get_executor());
    conns_.emplace(cfd, conn);
    submit_recv(*conn);

    // Multishot accept stays armed as long as IORING_CQE_F_MORE is set.
    if (!(flags & IORING_CQE_F_MORE))
      submit_multishot_accept();
  }

  // Returns false → close the connection.
  bool on_recv(IoUringConn& c, int res)
  {
    if (res <= 0) return false;  // EOF or error

    c.rx_.commit(static_cast<size_t>(res));

    // Process as many complete messages as are buffered.
    while (c.rx_.size() >= 4) {
      auto*    raw  = static_cast<const uint8_t*>(c.rx_.data().data());
      uint32_t blen = *reinterpret_cast<const uint32_t*>(raw);

      if (blen > max_message_size) {
        NPRPC_LOG_ERROR("uring: oversized msg {} bytes fd={}", blen, c.fd_);
        return false;
      }

      size_t total = 4u + blen;
      if (c.rx_.size() < total) {
        // Incomplete message — wait for more data.
        submit_recv(c);
        return true;
      }

      bool needs_reply = c.handle_request(c.rx_, c.tx_);
      c.rx_.consume(total);

      if (needs_reply) {
        c.tx_offset_ = 0;
        submit_send(c);
        // Send completion will resubmit recv.
        return true;
      }
    }

    // No pending or incomplete message — post next recv.
    submit_recv(c);
    return true;
  }

  bool on_send(IoUringConn& c, int res)
  {
    if (res < 0) {
      NPRPC_LOG_ERROR("uring: send error fd={}: {}", c.fd_, ::strerror(-res));
      return false;
    }

    c.tx_offset_ += static_cast<size_t>(res);

    if (c.tx_offset_ < c.tx_.cdata().size()) {
      // Partial send — resubmit remainder.
      submit_send(c);
      return true;
    }

    // Send complete — clean up tx and wait for the next request.
    c.tx_.consume(c.tx_.size());
    c.tx_offset_ = 0;
    submit_recv(c);
    return true;
  }

  // ── main thread loop ────────────────────────────────────────────────────────

  void run()
  {
    submit_multishot_accept();
    io_uring_submit(&ring_);  // flush the initial accept SQE into the kernel

    while (running_.load(std::memory_order_relaxed)) {
      io_uring_cqe*      cqe;
      __kernel_timespec  ts{0, 200'000'000};  // 200 ms  — wake to check running_
      int ret = io_uring_wait_cqe_timeout(&ring_, &cqe, &ts);
      if (ret == -ETIME || ret == -EINTR) continue;
      if (ret < 0) {
        NPRPC_LOG_ERROR("uring: io_uring_wait_cqe_timeout: {}", ::strerror(-ret));
        break;
      }

      // Drain all available CQEs in one pass, then advance the CQ head once.
      unsigned head, count = 0;
      io_uring_for_each_cqe(&ring_, head, cqe) {
        ++count;
        const uint64_t ud    = io_uring_cqe_get_data64(cqe);
        const uint32_t flags = cqe->flags;
        const int      res   = cqe->res;

        if (ud == kAcceptUD) {
          on_accept(res, flags);
          continue;
        }

        const int fd = fd_from_ud(ud);
        auto it = conns_.find(fd);
        if (it == conns_.end()) continue;  // connection already removed

        bool ok = (tag_from_ud(ud) == kTagRecv)
                      ? on_recv(*it->second, res)
                      : on_send(*it->second, res);

        if (!ok) remove_conn(fd);
      }
      io_uring_cq_advance(&ring_, count);

      // Flush any SQEs accumulated while handling CQEs.
      io_uring_submit(&ring_);
    }

    conns_.clear();
  }

public:
  void start(unsigned short port)
  {
    // Plain blocking listen socket — no NONBLOCK needed, accept is async.
    listenfd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listenfd_ < 0) {
      NPRPC_LOG_ERROR("uring: socket(): {}", ::strerror(errno));
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
      NPRPC_LOG_ERROR("uring: bind port {}: {}", port, ::strerror(errno));
      ::close(listenfd_);
      listenfd_ = -1;
      return;
    }
    ::listen(listenfd_, SOMAXCONN);

    io_uring_params params{};
    // Uncomment to enable kernel-side SQ polling (no io_uring_enter syscall).
    // Requires CAP_SYS_NICE or kernel 5.13+ with unprivileged SQPOLL.
    //   params.flags         = IORING_SETUP_SQPOLL;
    //   params.sq_thread_idle = 2000;  // ms before kernel thread sleeps

    if (io_uring_queue_init_params(kRingSize, &ring_, &params) < 0) {
      NPRPC_LOG_ERROR("uring: io_uring_queue_init_params: {}", ::strerror(errno));
      ::close(listenfd_);
      listenfd_ = -1;
      return;
    }

    running_.store(true);
    thread_ = std::thread([this] { run(); });
    NPRPC_LOG_INFO("io_uring TCP listener started on port {}", port);
  }

  void stop()
  {
    running_.store(false);
    if (thread_.joinable()) thread_.join();
    io_uring_queue_exit(&ring_);
    if (listenfd_ >= 0) { ::close(listenfd_); listenfd_ = -1; }
  }

  ~IoUringAcceptor() { stop(); }
};

// ── module entry points ───────────────────────────────────────────────────────

static std::unique_ptr<IoUringAcceptor> g_uring_acceptor;

void init_socket_uring(unsigned short port)
{
  g_uring_acceptor = std::make_unique<IoUringAcceptor>();
  g_uring_acceptor->start(port);
}

void stop_socket_uring()
{
  if (g_uring_acceptor) {
    g_uring_acceptor->stop();
    g_uring_acceptor.reset();
  }
}

} // namespace nprpc::impl

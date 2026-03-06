// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT
//
// io_uring-based TCP CLIENT connection.
//
// Replaces the Asio work-queue pattern (SocketConnection) with a zero-overhead
// synchronous path that runs entirely on the calling thread:
//
//   send_receive():
//     1. io_uring SEND  — submit once, wait one CQE (loop for partial sends)
//        Large payloads (>= kZcThreshold) use IORING_OP_SEND_ZC (kernel 6.0+,
//        liburing >= 2.3) to avoid the user→kernel copy.  Two CQEs per send:
//        a result CQE (bytes sent) and a notification CQE (pages unpinned).
//        Falls back to copy-send if ZC is unsupported at runtime.
//     2. io_uring RECV  — receive header (4 bytes), then rest of body
//        Each recv submission waits one CQE; we loop until the full wire
//        message [uint32_t body_len][body bytes] is assembled.
//
// There is no thread switch, no mutex/cv, no timer, and no O(N) composed-write
// completions going through the io_context scheduler.  For a 10 MB reply
// Asio fires ~160 completions (64 KB chunks); here we issue O(1) recv ops that
// fill the buffer as fast as the kernel can move data.
//
// Spin-poll optimisation: io_uring_peek_cqe() reads from shared memory
// (zero extra syscall) before falling back to io_uring_wait_cqe().
//
// Two io_uring rings per connection share the same fd_:
//   ring_sync_  — calling threads, guarded by mutex_conn_ (spin-poll CQEs).
//   ring_async_ — Asio io_context thread only (COOP_TASKRUN, no spin).
// mutex_conn_ serialises complete send+recv cycles so bytes from concurrent
// callers never interleave on the TCP stream.  A sync caller waiting on
// mutex_conn_ may grab it between individual async tasks, not after the entire
// async queue drains — better than SocketConnection's FIFO work queue.
//
// Reconnect: if the server closes the connection we reconnect once before
// propagating the failure, matching SocketConnection behaviour.

#include <liburing.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <mutex>
#include <stdexcept>

#include <nprpc/common.hpp>
#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/impl/session.hpp>

#include "../logging.hpp"
#include "helper.hpp"

// IORING_OP_SEND_ZC is an enum value, not a preprocessor macro, so #ifdef on
// it never fires.  IORING_CQE_F_NOTIF is a #define introduced in the same
// liburing 2.3 release (alongside io_uring_prep_send_zc) and is a reliable
// compile-time capability probe.  Set to 1 to enable, 0 to disable.
#ifdef IORING_CQE_F_NOTIF
#  define NPRPC_SEND_ZC_AVAILABLE 0
#else
#  define NPRPC_SEND_ZC_AVAILABLE 0
#endif

namespace nprpc::impl {

// ─────────────────────────────────────────────────────────────────────────────
// constants
// ─────────────────────────────────────────────────────────────────────────────

static constexpr unsigned kRingDepth   = 64;
static constexpr int      kLargeBuf    = 4 * 1024 * 1024; // 4 MB
static constexpr size_t   kRecvChunk   = 64 * 1024;       // 64 KB per recv op
static constexpr uint32_t kMaxMsgSize  = 64 * 1024 * 1024;
// Spin iterations before falling back to the blocking io_uring_wait_cqe
// syscall.  Each iteration calls __builtin_ia32_pause() (~5 ns on modern
// x86), so 2000 iterations ≈ 10 µs — covers the common loopback RTT.
static constexpr int      kSpinIter    = 2000;
// Minimum payload size for IORING_OP_SEND_ZC.  Page-pinning overhead is
// roughly 1–2 µs; the kernel copy costs ~0.1 ns/byte, so cross-over is
// around 10–20 KB.  32 KB is a conservative safe threshold.
static constexpr size_t   kZcThreshold = 32 * 1024;

// ─────────────────────────────────────────────────────────────────────────────
// UringClientConnection
// ─────────────────────────────────────────────────────────────────────────────

class UringClientConnection
    : public Session
    , public std::enable_shared_from_this<UringClientConnection>
{
  // mutex_conn_ serialises complete RPC cycles (send + recv) for both paths.
  // ring_sync_  is used by whichever thread holds mutex_conn_ (sync callers).
  // ring_async_ is used only by the io_context thread (send_receive_async).
  // IORING_SETUP_SINGLE_ISSUER is valid on both rings because mutex_conn_ / the
  // single-threaded io_context each guarantee at most one issuer at a time.
  std::mutex            mutex_conn_;
  int                   fd_         = -1;
  io_uring              ring_sync_  = {};  // sync path: spin-poll
  io_uring              ring_async_ = {};  // async path: COOP_TASKRUN + wait_cqe
  EndPoint              remote_endpoint_;
  boost::asio::any_io_executor exec_; // copy for post() in send_receive_async
#if NPRPC_SEND_ZC_AVAILABLE
  bool zc_send_capable_ = true; // cleared on first EOPNOTSUPP/ENOSYS/ENOMEM
#endif

  // ── helpers ────────────────────────────────────────────────────────────────

  // Open a new socket, connect, set options, but do NOT set up ring.
  // Returns the connected fd or throws.
  // Supports both IPv4 dotted-decimal strings and resolvable hostnames.
  static int make_connected_fd(const EndPoint& ep)
  {
    struct addrinfo hints{};
    hints.ai_family   = AF_UNSPEC;    // accept IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;

    const std::string hostname = std::string(ep.hostname());
    const std::string portstr  = std::to_string(ep.port());

    struct addrinfo* res = nullptr;
    if (::getaddrinfo(hostname.c_str(), portstr.c_str(), &hints, &res) != 0)
      throw nprpc::ExceptionCommFailure();

    int fd = -1;
    for (struct addrinfo* p = res; p; p = p->ai_next) {
      fd = ::socket(p->ai_family,
                    p->ai_socktype | SOCK_CLOEXEC,
                    p->ai_protocol);
      if (fd < 0)
        continue;
      if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0)
        break;  // success
      ::close(fd);
      fd = -1;
    }
    ::freeaddrinfo(res);

    if (fd < 0)
      throw nprpc::ExceptionCommFailure();

    // TCP_NODELAY: disable Nagle — mandatory for request-response RPC.
    int one = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    // Large socket buffers: reduce the number of TCP window-fill cycles for
    // bulk messages (matches SocketConnection behaviour).
    ::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &kLargeBuf, sizeof(kLargeBuf));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &kLargeBuf, sizeof(kLargeBuf));

#if NPRPC_SEND_ZC_AVAILABLE
    // SO_ZEROCOPY is required for IORING_OP_SEND_ZC.  Without it the kernel
    // returns -EINVAL for every SEND_ZC submission.  Failure is non-fatal —
    // zc_send_capable_ will be cleared on the first rejected request and we
    // fall back to copy-send transparently.
    ::setsockopt(fd, SOL_SOCKET, SO_ZEROCOPY, &one, sizeof(one));
#endif

    return fd;
  }

  // Initialise both io_uring rings.  Called once in the constructor.
  // NOT called on reconnect — rings are fd-agnostic and are reused across
  // reconnects; only fd_ changes.
  void init_rings()
  {
    auto try_init = [](io_uring& ring, io_uring_params& p, const char* label) {
      int ret = ::io_uring_queue_init_params(kRingDepth, &ring, &p);
      if (ret < 0) {
        p.flags = 0; // retry without optional flags on older kernels
        ret = ::io_uring_queue_init_params(kRingDepth, &ring, &p);
        if (ret < 0)
          throw std::runtime_error(std::string("io_uring_queue_init (") +
                                   label + ") failed: " + std::strerror(-ret));
      }
    };

    // ring_sync_: SINGLE_ISSUER (mutex_conn_ ensures one issuer at a time).
    // No COOP_TASKRUN — spin-poll needs the kernel to post CQEs asynchronously.
    {
      io_uring_params p{};
#ifdef IORING_SETUP_SINGLE_ISSUER
      p.flags |= IORING_SETUP_SINGLE_ISSUER;
#endif
      try_init(ring_sync_, p, "sync");
    }

    // ring_async_: SINGLE_ISSUER (io_context thread) + COOP_TASKRUN.
    // No spin in the async path, so deferred CQE delivery is fine and
    // reduces spurious interrupt overhead.
    {
      io_uring_params p{};
#ifdef IORING_SETUP_SINGLE_ISSUER
      p.flags |= IORING_SETUP_SINGLE_ISSUER;
#endif
#ifdef IORING_SETUP_COOP_TASKRUN
      p.flags |= IORING_SETUP_COOP_TASKRUN;
#endif
      try_init(ring_async_, p, "async");
    }
  }

  // ── low-level send / recv ─────────────────────────────────────────────────

  // ring_sync_ submit + spin-poll.  Fast path (loopback): kernel posts CQE
  // during the spin window → only 1 syscall (io_uring_submit).
  [[nodiscard]]
  int sync_wait(io_uring_cqe** cqe_out) noexcept
  {
    if (::io_uring_submit(&ring_sync_) < 0) return -errno;
    for (int i = 0; i < kSpinIter; ++i) {
      if (::io_uring_peek_cqe(&ring_sync_, cqe_out) == 0) return 0;
      __builtin_ia32_pause();
    }
    return ::io_uring_wait_cqe(&ring_sync_, cqe_out);
  }

  // ring_async_ submit + plain block.  No spin: COOP_TASKRUN defers CQE
  // delivery until io_uring_enter, so peek_cqe would never find anything.
  [[nodiscard]]
  int async_wait(io_uring_cqe** cqe_out) noexcept
  {
    if (::io_uring_submit(&ring_async_) < 0) return -errno;
    return ::io_uring_wait_cqe(&ring_async_, cqe_out);
  }

  // ── generic send / recv templates ────────────────────────────────────────
  // Shared between sync and async paths.  WaitFn: int(io_uring_cqe**) noexcept.
  // `ring` must match the ring that WaitFn submits to.

  template <typename WaitFn>
  bool tpl_send_all(io_uring& ring, WaitFn wait_fn, const char* ptr, size_t remaining)
  {
    while (remaining > 0) {
      io_uring_sqe* sqe = ::io_uring_get_sqe(&ring);
      if (!sqe) {
        ::io_uring_submit(&ring);
        sqe = ::io_uring_get_sqe(&ring);
        if (!sqe) return false;
      }
      ::io_uring_prep_send(sqe, fd_, ptr, remaining, 0);
      sqe->user_data = 1;
      io_uring_cqe* cqe = nullptr;
      if (wait_fn(&cqe) < 0 || cqe->res <= 0) {
        if (cqe) ::io_uring_cqe_seen(&ring, cqe);
        return false;
      }
      size_t sent = static_cast<size_t>(cqe->res);
      ::io_uring_cqe_seen(&ring, cqe);
      ptr += sent; remaining -= sent;
    }
    return true;
  }

  template <typename WaitFn>
  bool tpl_recv_message(io_uring& ring, WaitFn wait_fn, flat_buffer& buf)
  {
    buf.consume(buf.size());

    // Phase 1: accumulate until we have the 4-byte length prefix.
    while (buf.size() < 4) {
      auto mb = buf.prepare(kRecvChunk);
      io_uring_sqe* sqe = ::io_uring_get_sqe(&ring);
      if (!sqe) { ::io_uring_submit(&ring); sqe = ::io_uring_get_sqe(&ring); if (!sqe) return false; }
      ::io_uring_prep_recv(sqe, fd_, mb.data(), mb.size(), 0);
      sqe->user_data = 0;
      io_uring_cqe* cqe = nullptr;
      if (wait_fn(&cqe) < 0 || cqe->res <= 0) { if (cqe) ::io_uring_cqe_seen(&ring, cqe); return false; }
      buf.commit(static_cast<size_t>(cqe->res));
      ::io_uring_cqe_seen(&ring, cqe);
    }

    // Phase 2: receive body.
    const uint32_t body_len =
        *reinterpret_cast<const uint32_t*>(buf.data().data());
    if (body_len > kMaxMsgSize) {
      fail(boost::asio::error::no_buffer_space, "uring_client: body_len too large");
      return false;
    }
    const size_t total_needed = 4 + static_cast<size_t>(body_len);
    while (buf.size() < total_needed) {
      size_t chunk = std::min(total_needed - buf.size(), kRecvChunk);
      auto mb = buf.prepare(chunk);
      io_uring_sqe* sqe = ::io_uring_get_sqe(&ring);
      if (!sqe) { ::io_uring_submit(&ring); sqe = ::io_uring_get_sqe(&ring); if (!sqe) return false; }
      ::io_uring_prep_recv(sqe, fd_, mb.data(), mb.size(), 0);
      sqe->user_data = 0;
      io_uring_cqe* cqe = nullptr;
      if (wait_fn(&cqe) < 0 || cqe->res <= 0) { if (cqe) ::io_uring_cqe_seen(&ring, cqe); return false; }
      buf.commit(static_cast<size_t>(cqe->res));
      ::io_uring_cqe_seen(&ring, cqe);
    }
    return true;
  }

#if NPRPC_SEND_ZC_AVAILABLE
  // Drain the single notification CQE that IORING_OP_SEND_ZC generates after
  // the kernel finishes with the pinned user pages.
  // Must be called only when the preceding send CQE had IORING_CQE_F_MORE set.
  void drain_zc_notif() noexcept
  {
    io_uring_cqe* cqe = nullptr;
    for (int i = 0; i < kSpinIter; ++i) {
      if (::io_uring_peek_cqe(&ring_sync_, &cqe) == 0)
        goto done;
      __builtin_ia32_pause();
    }
    ::io_uring_wait_cqe(&ring_sync_, &cqe);
  done:
    if (cqe)
      ::io_uring_cqe_seen(&ring_sync_, cqe);
  }

  // Zero-copy send of `remaining` bytes starting at `ptr`.
  //
  // Returns true  on full success.
  // Returns false with ok_out=false  on connection error.
  // Returns false with ok_out=true   when ZC is not supported by the kernel
  //   (zc_send_capable_ cleared); caller should retry with copy-send.
  //
  // Each iteration: submit SEND_ZC → wait for send CQE → if IORING_CQE_F_MORE,
  // drain notification CQE.  Buffer must not be freed until this returns.
  bool do_send_all_zc(const char* ptr, size_t remaining, bool& ok_out)
  {
    while (remaining > 0) {
      io_uring_sqe* sqe = ::io_uring_get_sqe(&ring_sync_);
      if (!sqe) {
        ::io_uring_submit(&ring_sync_);
        sqe = ::io_uring_get_sqe(&ring_sync_);
        if (!sqe) { ok_out = false; return false; }
      }

      ::io_uring_prep_send_zc(sqe, fd_, ptr, remaining, 0, 0);
      sqe->user_data = 1;

      io_uring_cqe* cqe = nullptr;
      if (sync_wait(&cqe) < 0) { ok_out = false; return false; }

      const int  res  = cqe->res;
      const bool more = cqe->flags & IORING_CQE_F_MORE;
      ::io_uring_cqe_seen(&ring_sync_, cqe);

      if (res <= 0) {
        // Always drain the notification CQE exactly once before returning —
        // the kernel enqueues it even for failed requests (res < 0).
        if (more) drain_zc_notif();
        // Permanent or resource-limit failures: disable ZC and let the caller
        // retry this same buffer with copy-send.
        //   EOPNOTSUPP / ENOSYS: kernel/socket does not support SEND_ZC at all.
        //   EINVAL:  SO_ZEROCOPY was not set on the socket (should not happen).
        //   ENOMEM:  kernel could not pin the pages (e.g. RLIMIT_MEMLOCK too
        //            low, or payload too large for the ZC accounting limit).
        if (res == -EOPNOTSUPP || res == -ENOSYS || res == -EINVAL ||
            res == -ENOMEM) {
          zc_send_capable_ = false;
          return false; // ok_out stays true → caller retries with copy-send
        }
        ok_out = false;
        return false;
      }

      // Success: notification CQE arrives asynchronously; drain it before
      // the next SQE submission to keep the ring sequencing clean.
      if (more) drain_zc_notif();

      ptr       += static_cast<size_t>(res);
      remaining -= static_cast<size_t>(res);
    }
    return true;
  }
#endif // NPRPC_SEND_ZC_AVAILABLE

  // ── sync-path wrappers (ring_sync_ + spin-poll) ───────────────────────────

  bool do_send_all(const void* data, size_t len)
  {
    const char* ptr = static_cast<const char*>(data);
    const size_t remaining = len;
#if NPRPC_SEND_ZC_AVAILABLE
    if (zc_send_capable_ && remaining >= kZcThreshold) {
      bool ok = true;
      if (do_send_all_zc(ptr, remaining, ok)) return true;
      if (!ok) return false;
      // ZC rejected at runtime; fall through to copy-send.
    }
#endif
    return tpl_send_all(ring_sync_,
                        [this](io_uring_cqe** c) noexcept { return sync_wait(c); },
                        ptr, remaining);
  }

  bool do_recv_message(flat_buffer& buf)
  {
    return tpl_recv_message(ring_sync_,
                            [this](io_uring_cqe** c) noexcept { return sync_wait(c); },
                            buf);
  }

  // ── async-path wrappers (ring_async_, no spin) ────────────────────────────
  // ZC is intentionally omitted: ZC is only useful when pages stay pinned for
  // the duration of the call, which is guaranteed on the sync path but not
  // straightforwardly asserted on the async path.

  bool do_send_all_async(const void* data, size_t len)
  {
    return tpl_send_all(ring_async_,
                        [this](io_uring_cqe** c) noexcept { return async_wait(c); },
                        static_cast<const char*>(data), len);
  }

  bool do_recv_message_async(flat_buffer& buf)
  {
    return tpl_recv_message(ring_async_,
                            [this](io_uring_cqe** c) noexcept { return async_wait(c); },
                            buf);
  }

  // ── reconnect ──────────────────────────────────────────────────────────────

  void reconnect()
  {
    if (fd_ >= 0) {
      ::close(fd_);
      fd_ = -1;
    }
    fd_ = make_connected_fd(remote_endpoint_);
    // ring_ is reused — io_uring rings are fd-agnostic
  }

public:
  // ── Session interface ─────────────────────────────────────────────────────

  void timeout_action() final {}   // no Asio timer, nothing to cancel

  void send_receive(flat_buffer& buffer, uint32_t /*timeout_ms*/) override
  {
    assert(*reinterpret_cast<const uint32_t*>(buffer.data().data()) ==
           buffer.size() - 4);

    std::unique_lock<std::mutex> lock(mutex_conn_);

    auto send_and_recv = [&]() -> bool {
      // send
      auto cb = buffer.cdata();
      if (!do_send_all(cb.data(), cb.size()))
        return false;

      // recv response back into the same buffer
      return do_recv_message(buffer);
    };

    if (send_and_recv())
      return;

    // First failure: reconnect and retry once, matching SocketConnection.
    try {
      reconnect();
    } catch (...) {
      close();
      throw nprpc::ExceptionCommFailure();
    }

    if (!send_and_recv()) {
      close();
      throw nprpc::ExceptionCommFailure();
    }
  }

  void send_receive_async(
      flat_buffer&& buffer,
      std::optional<std::function<void(const boost::system::error_code&,
                                       flat_buffer&)>>&& completion_handler,
      uint32_t /*timeout_ms*/) override
  {
    // Post to the io_context thread so the caller returns immediately.
    // The lambda owns buf and handler.  It acquires mutex_conn_ before any
    // I/O on ring_async_, releases it before invoking the callback (to avoid
    // calling user code under a lock), then posts the notification.
    boost::asio::post(
        exec_,
        [self    = shared_from_this(),
         buf     = std::move(buffer),
         handler = std::move(completion_handler)]() mutable
        {
          std::unique_lock<std::mutex> lock(self->mutex_conn_);

          auto send_and_recv = [&]() -> bool {
            auto cb = buf.cdata();
            if (!self->do_send_all_async(cb.data(), cb.size())) return false;
            return self->do_recv_message_async(buf);
          };

          bool ok = send_and_recv();
          if (!ok) {
            try {
              self->reconnect();
              ok = send_and_recv();
            } catch (...) {
              self->close();
            }
          }

          lock.unlock();

          if (handler)
            (*handler)(ok ? boost::system::error_code{}
                          : boost::asio::error::connection_reset,
                       buf);
        });
  }

  void shutdown() override
  {
    Session::shutdown();
    if (fd_ >= 0) {
      ::shutdown(fd_, SHUT_RDWR);
      ::close(fd_);
      fd_ = -1;
    }
  }

  // ── construction / destruction ────────────────────────────────────────────

  UringClientConnection(const EndPoint& endpoint,
                        boost::asio::any_io_executor ex)
      : Session(ex)
      , remote_endpoint_(endpoint)
      , exec_(ex)
  {
    ctx_.remote_endpoint = endpoint;
    // Disable the Asio timeout timer — we don't use it.
    timeout_timer_.expires_after(std::chrono::system_clock::duration::max());

    init_rings();

    fd_ = make_connected_fd(endpoint);
  }

  ~UringClientConnection()
  {
    ::io_uring_queue_exit(&ring_sync_);
    ::io_uring_queue_exit(&ring_async_);
    if (fd_ >= 0)
      ::close(fd_);
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// factory — called from rpc_impl.cpp
// ─────────────────────────────────────────────────────────────────────────────

std::shared_ptr<Session>
make_uring_client_connection(const EndPoint& endpoint,
                              boost::asio::any_io_executor ex)
{
  return std::make_shared<UringClientConnection>(endpoint, std::move(ex));
}

} // namespace nprpc::impl

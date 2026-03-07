// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT
//
// io_uring-based TCP CLIENT connection -- full-duplex / request-multiplexed.
//
// One io_uring ring (ring_send_) + one dedicated reader thread per connection:
//
//   ring_send_:  ALL sends (sync + async), protected by send_mutex_.
//                SINGLE_ISSUER: send_mutex_ ensures one sender at a time.
//                Sync senders spin-poll the send CQE for lowest latency.
//                Large payloads (>= kZcThreshold) use IORING_OP_SEND_ZC
//                (kernel 6.0+, liburing >= 2.3).  Falls back to copy-send.
//
//   reader_thread_: Blocking recv() loop — simpler and equally fast as
//                io_uring recv.  shutdown(SHUT_RDWR) reliably unblocks it.
//
// Each RPC is assigned a unique request_id (monotonic per-connection counter).
// The id is injected into Header.request_id before the send, and the reader
// thread uses it to route each incoming response to the correct waiter:
//
//   Sync callers (send_receive):
//     Register a stack-allocated SyncWaiter -> acquire send_mutex_ -> send
//     (spin-poll send CQE) -> release send_mutex_ -> park on atomic::wait.
//     Reader delivers response buffer and calls atomic::notify_one.
//
//   Async callers (send_receive_async):
//     Register a heap AsyncWaiter -> post lambda to exec_ -> lambda acquires
//     send_mutex_ -> sends -> releases.  Reader delivers response to the
//     callback via boost::asio::post(exec_, ...).
//
// Multiple concurrent sync + async RPCs can be in-flight simultaneously on
// one connection.  send_mutex_ is held only for the send phase (~us), not
// for the full RTT, so concurrent senders queue at most a few microseconds.
//
// Reconnect: on send failure the connection is re-established once.  All
// other in-flight waiters are woken with an error when the reader detects
// the broken fd and drains pending_requests_.

#include <liburing.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstring>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <variant>

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

// -----------------------------------------------------------------------------
// constants
// -----------------------------------------------------------------------------

static constexpr unsigned kRingDepth   = 64;
static constexpr int      kLargeBuf    = 4 * 1024 * 1024; // 4 MB
static constexpr uint32_t kMaxMsgSize  = 64 * 1024 * 1024;
// Spin iterations before falling back to the blocking io_uring_wait_cqe
// syscall.  Each iteration calls __builtin_ia32_pause() (~5 ns on modern
// x86), so 2000 iterations ~= 10 us -- covers the common loopback RTT.
static constexpr int      kSpinIter    = 2000;
// Minimum payload size for IORING_OP_SEND_ZC.  Page-pinning overhead is
// roughly 1-2 us; the kernel copy costs ~0.1 ns/byte, so cross-over is
// around 10-20 KB.  32 KB is a conservative safe threshold.
static constexpr size_t   kZcThreshold = 32 * 1024;

// -----------------------------------------------------------------------------
// UringClientConnection
// -----------------------------------------------------------------------------

class UringClientConnection
    : public Session
    , public std::enable_shared_from_this<UringClientConnection>
{
  // ---- per-request waiters -------------------------------------------------

  // Stack-allocated by the sync caller.  Reader writes the response buffer
  // and flips status so the caller's atomic::wait() returns.
  struct SyncWaiter {
    flat_buffer*     buf_out;    // caller's buffer; reader moves response here
    std::atomic<int> status{0}; // 0=pending, 1=ok, -1=error
  };

  // Heap-allocated for async callers.  Reader populates recv_buf and fires
  // the handler via boost::asio::post(exec_, ...).
  struct AsyncWaiter {
    flat_buffer recv_buf;
    std::optional<std::function<void(const boost::system::error_code&,
                                     flat_buffer&)>> handler;
  };

  // Stack-allocated in the coroutine frame of send_receive_coro().
  // Reader moves the response buffer, sets status, then posts h.resume()
  // to exec_ so the coroutine continues on the io_context thread pool.
  struct CoroWaiter {
    flat_buffer*            buf_out;
    std::coroutine_handle<> continuation;
    int                     status{0}; // 0=pending, 1=ok, -1=error
  };

  using PendingEntry = std::variant<SyncWaiter*, std::unique_ptr<AsyncWaiter>, CoroWaiter*>;

  // ---- members -------------------------------------------------------------

  // send_mutex_: held only for the send phase; never held during recv.
  // pending_mutex_: held only for map insert/erase/lookup (~100 ns).
  std::mutex                                 send_mutex_;
  std::mutex                                 pending_mutex_;
  std::atomic<uint32_t>                      next_request_id_{1};
  std::unordered_map<uint32_t, PendingEntry> pending_requests_;

  int      fd_        = -1;
  io_uring ring_send_ = {};  // sends only; serialised via send_mutex_

  EndPoint                     remote_endpoint_;
  boost::asio::any_io_executor exec_;
  std::thread                  reader_thread_;
  std::atomic<bool>            closed_{false};

#if NPRPC_SEND_ZC_AVAILABLE
  bool zc_send_capable_ = true; // cleared on first EOPNOTSUPP/ENOSYS/ENOMEM
#endif

  // ---- socket helpers ------------------------------------------------------

  static int make_connected_fd(const EndPoint& ep)
  {
    struct addrinfo hints{};
    hints.ai_family   = AF_UNSPEC;
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
      if (fd < 0) continue;
      if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
      ::close(fd);
      fd = -1;
    }
    ::freeaddrinfo(res);

    if (fd < 0) throw nprpc::ExceptionCommFailure();

    int one = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    ::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &kLargeBuf, sizeof(kLargeBuf));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &kLargeBuf, sizeof(kLargeBuf));

#if NPRPC_SEND_ZC_AVAILABLE
    // SO_ZEROCOPY is required for IORING_OP_SEND_ZC.  Failure is non-fatal --
    // zc_send_capable_ will be cleared on the first rejected send.
    ::setsockopt(fd, SOL_SOCKET, SO_ZEROCOPY, &one, sizeof(one));
#endif

    return fd;
  }

  // ---- ring initialisation -------------------------------------------------

  // Called once in the constructor.  Rings are fd-agnostic and survive
  // reconnects; only fd_ changes.
  void init_rings()
  {
    auto try_init = [](io_uring& ring, io_uring_params& p, const char* label) {
      int ret = ::io_uring_queue_init_params(kRingDepth, &ring, &p);
      if (ret < 0) {
        p.flags = 0;
        ret = ::io_uring_queue_init_params(kRingDepth, &ring, &p);
        if (ret < 0)
          throw std::runtime_error(std::string("io_uring_queue_init (") +
                                   label + ") failed: " + std::strerror(-ret));
      }
    };

    // ring_send_: multi-thread safe via send_mutex_.
    // Do NOT use IORING_SETUP_SINGLE_ISSUER: send_mutex_ serialises senders,
    // but the submitting thread can change (e.g. a coroutine continuation may
    // resume on an io_context thread different from the original caller).
    // SINGLE_ISSUER would let the kernel lock sends to the first submitter thread
    // and return -EEXIST for any other, causing a SIGSEGV inside liburing.
    // No COOP_TASKRUN: sync senders spin-poll send CQEs.
    {
      io_uring_params p{};
      try_init(ring_send_, p, "send");
    }

  }

  // ---- CQE wait helpers ----------------------------------------------------

  // ring_send_: submit + spin-poll.  Fast path (loopback): kernel posts CQE
  // during the spin window -> only 1 syscall (io_uring_submit).
  [[nodiscard]]
  int send_wait(io_uring_cqe** cqe_out) noexcept
  {
    if (::io_uring_submit(&ring_send_) < 0) return -errno;
    for (int i = 0; i < kSpinIter; ++i) {
      if (::io_uring_peek_cqe(&ring_send_, cqe_out) == 0) return 0;
      __builtin_ia32_pause();
    }
    return ::io_uring_wait_cqe(&ring_send_, cqe_out);
  }



  // ---- header helpers ------------------------------------------------------

  static void inject_request_id(flat_buffer& buf, uint32_t rid)
  {
    nprpc::impl::flat::Header_Direct hdr(buf, 0);
    hdr.request_id() = rid;
  }

  static uint32_t extract_request_id(flat_buffer& buf)
  {
    nprpc::impl::flat::Header_Direct hdr(buf, 0);
    return hdr.request_id();
  }

  // ---- send (ring_send_ only, called under send_mutex_) --------------------

  bool tpl_send_all(const char* ptr, size_t remaining)
  {
    while (remaining > 0) {
      io_uring_sqe* sqe = ::io_uring_get_sqe(&ring_send_);
      if (!sqe) {
        ::io_uring_submit(&ring_send_);
        sqe = ::io_uring_get_sqe(&ring_send_);
        if (!sqe) return false;
      }
      ::io_uring_prep_send(sqe, fd_, ptr, remaining, 0);
      sqe->user_data = 1;
      io_uring_cqe* cqe = nullptr;
      if (send_wait(&cqe) < 0 || cqe->res <= 0) {
        if (cqe) ::io_uring_cqe_seen(&ring_send_, cqe);
        return false;
      }
      const size_t sent = static_cast<size_t>(cqe->res);
      ::io_uring_cqe_seen(&ring_send_, cqe);
      ptr += sent; remaining -= sent;
    }
    return true;
  }

  // ---- recv (blocking syscall, reader_thread_ only) -----------------------
  //
  // Plain blocking recv() is simpler and more reliable than io_uring for the
  // receive path: shutdown(fd, SHUT_RDWR) reliably unblocks the syscall for
  // clean teardown, and there is no performance reason to use io_uring here
  // (the reader thread is always blocking anyway).

  bool recv_one_message(flat_buffer& buf)
  {
    buf.consume(buf.size());

    // Read exactly `n` bytes into `dst`; returns false on EOF/error.
    auto read_exact = [this](char* dst, size_t n) -> bool {
      while (n > 0) {
        ssize_t r = ::recv(fd_, dst, n, 0);
        if (r <= 0) {
          if (r < 0 && errno == EINTR) continue; // retry on signal
          return false;
        }
        dst += r;
        n   -= static_cast<size_t>(r);
      }
      return true;
    };

    // Phase 1: 4-byte size prefix.
    {
      auto mb = buf.prepare(4);
      if (!read_exact(static_cast<char*>(mb.data()), 4)) return false;
      buf.commit(4);
    }

    const uint32_t body_len =
        *reinterpret_cast<const uint32_t*>(buf.cdata().data());
    if (body_len > kMaxMsgSize) {
      fail(boost::asio::error::no_buffer_space, "uring_client: body_len too large");
      return false;
    }

    // Phase 2: body.
    if (body_len > 0) {
      auto mb = buf.prepare(body_len);
      if (!read_exact(static_cast<char*>(mb.data()), body_len)) return false;
      buf.commit(body_len);
    }
    return true;
  }

  // ---- ZC send (ring_send_, called under send_mutex_) ----------------------

#if NPRPC_SEND_ZC_AVAILABLE
  void drain_zc_notif() noexcept
  {
    io_uring_cqe* cqe = nullptr;
    for (int i = 0; i < kSpinIter; ++i) {
      if (::io_uring_peek_cqe(&ring_send_, &cqe) == 0) goto done;
      __builtin_ia32_pause();
    }
    ::io_uring_wait_cqe(&ring_send_, &cqe);
  done:
    if (cqe) ::io_uring_cqe_seen(&ring_send_, cqe);
  }

  bool do_send_all_zc(const char* ptr, size_t remaining, bool& ok_out)
  {
    while (remaining > 0) {
      io_uring_sqe* sqe = ::io_uring_get_sqe(&ring_send_);
      if (!sqe) {
        ::io_uring_submit(&ring_send_);
        sqe = ::io_uring_get_sqe(&ring_send_);
        if (!sqe) { ok_out = false; return false; }
      }

      ::io_uring_prep_send_zc(sqe, fd_, ptr, remaining, 0, 0);
      sqe->user_data = 1;

      io_uring_cqe* cqe = nullptr;
      if (send_wait(&cqe) < 0) { ok_out = false; return false; }

      const int  res  = cqe->res;
      const bool more = (cqe->flags & IORING_CQE_F_MORE) != 0;
      ::io_uring_cqe_seen(&ring_send_, cqe);

      if (res <= 0) {
        if (more) drain_zc_notif();
        if (res == -EOPNOTSUPP || res == -ENOSYS || res == -EINVAL ||
            res == -ENOMEM) {
          zc_send_capable_ = false;
          return false; // ok_out stays true -> caller retries with copy-send
        }
        ok_out = false;
        return false;
      }

      if (more) drain_zc_notif();
      ptr += static_cast<size_t>(res); remaining -= static_cast<size_t>(res);
    }
    return true;
  }
#endif // NPRPC_SEND_ZC_AVAILABLE

  // Unified send entry point; called under send_mutex_.
  bool do_send_all(const char* ptr, size_t len)
  {
#if NPRPC_SEND_ZC_AVAILABLE
    if (zc_send_capable_ && len >= kZcThreshold) {
      bool ok = true;
      if (do_send_all_zc(ptr, len, ok)) return true;
      if (!ok) return false;
      // ZC rejected at runtime; fall through to copy-send.
    }
#endif
    return tpl_send_all(ptr, len);
  }

  // ---- pending_requests_ helpers -------------------------------------------

  // Wake all registered waiters with an error.  Called by the reader thread
  // when recv fails (connection drop / fd closed).
  void drain_pending_with_error()
  {
    std::unordered_map<uint32_t, PendingEntry> local;
    {
      std::lock_guard lk(pending_mutex_);
      local = std::move(pending_requests_);
    }
    for (auto& [rid, entry] : local) {
      std::visit([this](auto& w) {
        using T = std::decay_t<decltype(w)>;
        if constexpr (std::is_same_v<T, SyncWaiter*>) {
          w->status.store(-1, std::memory_order_release);
          w->status.notify_one();
        } else if constexpr (std::is_same_v<T, CoroWaiter*>) {
          w->status = -1;
          boost::asio::post(exec_, [h = w->continuation]() { h.resume(); });
        } else {
          boost::asio::post(exec_,
            [waiter = std::move(w)]() mutable {
              if (waiter->handler)
                (*waiter->handler)(boost::asio::error::connection_reset,
                                   waiter->recv_buf);
            });
        }
      }, entry);
    }
  }

  // ---- reader thread -------------------------------------------------------

  void run_reader()
  {
    while (true) {
      flat_buffer buf;
      if (!recv_one_message(buf)) {
        drain_pending_with_error();
        return;
      }

      const uint32_t rid = extract_request_id(buf);

      std::optional<PendingEntry> entry;
      {
        std::lock_guard lk(pending_mutex_);
        auto it = pending_requests_.find(rid);
        if (it != pending_requests_.end()) {
          entry = std::move(it->second);
          pending_requests_.erase(it);
        }
      }
      if (!entry) continue; // unknown rid -- ignore (should not happen)

      std::visit([&](auto& w) {
        using T = std::decay_t<decltype(w)>;
        if constexpr (std::is_same_v<T, SyncWaiter*>) {
          *w->buf_out = std::move(buf);
          w->status.store(1, std::memory_order_release);
          w->status.notify_one();
        } else if constexpr (std::is_same_v<T, CoroWaiter*>) {
          *w->buf_out = std::move(buf);
          w->status = 1;
          boost::asio::post(exec_, [h = w->continuation]() { h.resume(); });
        } else {
          w->recv_buf = std::move(buf);
          boost::asio::post(exec_,
            [waiter = std::move(w)]() mutable {
              if (waiter->handler)
                (*waiter->handler)({}, waiter->recv_buf);
            });
        }
      }, *entry);
    }
  }

  // ---- reconnect -----------------------------------------------------------

  // Tear down the current connection, join the reader thread, and open a
  // fresh fd + reader.  Acquires send_mutex_ to ensure no concurrent sender
  // is mid-send when the fd is closed.
  void reconnect()
  {
    {
      std::lock_guard send_lk(send_mutex_);
      if (fd_ >= 0) {
        // SHUT_RDWR causes the blocked recv in ring_recv_ to complete with
        // 0 bytes (EOF), making run_reader() drain pending and exit.
        ::shutdown(fd_, SHUT_RDWR);
        ::close(fd_);
        fd_ = -1;
      }
    }

    if (reader_thread_.joinable())
      reader_thread_.join();

#if NPRPC_SEND_ZC_AVAILABLE
    zc_send_capable_ = true;
#endif

    fd_ = make_connected_fd(remote_endpoint_);
    reader_thread_ = std::thread(&UringClientConnection::run_reader, this);
  }

public:
  // ---- Session interface ---------------------------------------------------

  void timeout_action() final {}   // no Asio timer, nothing to cancel

  void send_receive(flat_buffer& buffer, uint32_t /*timeout_ms*/) override
  {
    assert(*reinterpret_cast<const uint32_t*>(buffer.data().data()) ==
           buffer.size() - 4);

    const uint32_t rid =
        next_request_id_.fetch_add(1, std::memory_order_relaxed);
    inject_request_id(buffer, rid);

    // Stack-allocated waiter; valid until this function returns.
    SyncWaiter waiter{&buffer};

    auto do_send = [&]() -> bool {
      std::lock_guard lk(send_mutex_);
      auto cb = buffer.cdata();
      return do_send_all(static_cast<const char*>(cb.data()), cb.size());
    };

    // Register BEFORE send: prevents a lost-wakeup if the server responds
    // before we reach atomic::wait (extremely unlikely but correct).
    {
      std::lock_guard lk(pending_mutex_);
      pending_requests_.emplace(rid, &waiter);
    }

    if (!do_send()) {
      { std::lock_guard lk(pending_mutex_); pending_requests_.erase(rid); }
      try { reconnect(); } catch (...) { close(); throw nprpc::ExceptionCommFailure(); }
      { std::lock_guard lk(pending_mutex_); pending_requests_.emplace(rid, &waiter); }
      if (!do_send()) {
        { std::lock_guard lk(pending_mutex_); pending_requests_.erase(rid); }
        close();
        throw nprpc::ExceptionCommFailure();
      }
    }

    // Park until the reader delivers our response (or an error).
    waiter.status.wait(0, std::memory_order_acquire);
    if (waiter.status.load(std::memory_order_relaxed) < 0) {
      close();
      throw nprpc::ExceptionCommFailure();
    }
  }

  nprpc::Task<> send_receive_coro(flat_buffer& buffer,
                                   uint32_t /*timeout_ms*/) override
  {
    assert(*reinterpret_cast<const uint32_t*>(buffer.data().data()) ==
           buffer.size() - 4);

    const uint32_t rid =
        next_request_id_.fetch_add(1, std::memory_order_relaxed);
    inject_request_id(buffer, rid);

    // Awaiter lives in the coroutine frame.  await_suspend() registers the
    // CoroWaiter, sends, and either suspends (ok) or returns false (error,
    // resume immediately with status=-1).
    struct SendAndAwait {
      UringClientConnection* self;
      uint32_t               rid;
      flat_buffer&           buffer;
      CoroWaiter             waiter{};  // frame-local; pointer stored in map

      bool await_ready() noexcept { return false; }

      bool await_suspend(std::coroutine_handle<> h) noexcept {
        waiter.buf_out      = &buffer;
        waiter.continuation = h;
        waiter.status       = 0;

        // Register BEFORE send (lost-wakeup guard).
        {
          std::lock_guard lk(self->pending_mutex_);
          self->pending_requests_.emplace(rid, &waiter);
        }

        bool ok;
        {
          std::lock_guard lk(self->send_mutex_);
          auto cb = buffer.cdata();
          ok = self->do_send_all(
              static_cast<const char*>(cb.data()), cb.size());
        }

        if (!ok) {
          std::lock_guard lk(self->pending_mutex_);
          self->pending_requests_.erase(rid);
          waiter.status = -1;
          return false;  // resume immediately; await_resume() throws
        }
        return true;  // suspend; reader will resume via exec_
      }

      void await_resume() {
        if (waiter.status < 0) throw nprpc::ExceptionCommFailure();
      }
    };

    co_await SendAndAwait{this, rid, buffer};
  }

  void send_receive_async(
      flat_buffer&& buffer,
      std::optional<std::function<void(const boost::system::error_code&,
                                       flat_buffer&)>>&& completion_handler,
      uint32_t /*timeout_ms*/) override
  {
    const uint32_t rid =
        next_request_id_.fetch_add(1, std::memory_order_relaxed);
    inject_request_id(buffer, rid);

    // Register waiter before posting the send -- same lost-wakeup guard.
    auto waiter = std::make_unique<AsyncWaiter>();
    waiter->handler = std::move(completion_handler);
    {
      std::lock_guard lk(pending_mutex_);
      pending_requests_.emplace(rid, std::move(waiter));
    }

    // Post the send to the io_context thread so the caller returns
    // immediately.  send_buf owns the outgoing data; AsyncWaiter::recv_buf
    // (populated by the reader) is separate.
    boost::asio::post(exec_,
      [self = shared_from_this(), rid, send_buf = std::move(buffer)]() mutable
      {
        std::lock_guard send_lk(self->send_mutex_);
        auto cb = send_buf.cdata();
        const bool ok =
            self->do_send_all(static_cast<const char*>(cb.data()), cb.size());
        if (!ok) {
          // Send failed: pull waiter from map and fire error callback.
          std::unique_ptr<AsyncWaiter> w;
          {
            std::lock_guard lk(self->pending_mutex_);
            auto it = self->pending_requests_.find(rid);
            if (it != self->pending_requests_.end()) {
              w = std::move(
                  std::get<std::unique_ptr<AsyncWaiter>>(it->second));
              self->pending_requests_.erase(it);
            }
          }
          if (w && w->handler)
            (*w->handler)(boost::asio::error::connection_reset, w->recv_buf);
        }
        // If ok: reader_thread_ will deliver the response.
      });
  }

  void shutdown() override
  {
    closed_.store(true, std::memory_order_seq_cst);
    Session::shutdown();
    if (fd_ >= 0) {
      ::shutdown(fd_, SHUT_RDWR);
      ::close(fd_);
      fd_ = -1;
    }
  }

  // ---- construction / destruction ------------------------------------------

  UringClientConnection(const EndPoint& endpoint,
                        boost::asio::any_io_executor ex)
      : Session(ex)
      , remote_endpoint_(endpoint)
      , exec_(ex)
  {
    ctx_.remote_endpoint = endpoint;
    timeout_timer_.expires_after(std::chrono::system_clock::duration::max());

    init_rings();

    fd_ = make_connected_fd(endpoint);
    reader_thread_ = std::thread(&UringClientConnection::run_reader, this);
  }

  ~UringClientConnection()
  {
    closed_.store(true, std::memory_order_seq_cst);
    // shutdown(SHUT_RDWR) unblocks the blocking recv() in reader_thread_.
    if (fd_ >= 0) {
      ::shutdown(fd_, SHUT_RDWR);
      ::close(fd_);
      fd_ = -1;
    }
    if (reader_thread_.joinable())
      reader_thread_.join();
    ::io_uring_queue_exit(&ring_send_);
  }
};

// -----------------------------------------------------------------------------
// factory -- called from rpc_impl.cpp
// -----------------------------------------------------------------------------

std::shared_ptr<Session>
make_uring_client_connection(const EndPoint& endpoint,
                              boost::asio::any_io_executor ex)
{
  return std::make_shared<UringClientConnection>(endpoint, std::move(ex));
}

} // namespace nprpc::impl

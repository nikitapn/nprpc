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
//     2. io_uring RECV  — receive header (4 bytes), then rest of body
//        Each recv submission waits one CQE; we loop until the full wire
//        message [uint32_t body_len][body bytes] is assembled.
//
// There is no thread switch, no mutex/cv, no timer, and no O(N) composed-write
// completions going through the io_context scheduler.  For a 10 MB reply
// Asio fires ~160 completions (64 KB chunks); here we issue O(1) recv ops that
// fill the buffer as fast as the kernel can move data.
//
// Per-connection io_uring ring with IORING_SETUP_SINGLE_ISSUER (kernel 6.0+).
// Falls back gracefully if the flag is unsupported.
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
#include <stdexcept>

#include <nprpc/common.hpp>
#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/impl/session.hpp>

#include "../logging.hpp"
#include "helper.hpp"

namespace nprpc::impl {

// ─────────────────────────────────────────────────────────────────────────────
// constants
// ─────────────────────────────────────────────────────────────────────────────

static constexpr unsigned kRingDepth   = 64;
static constexpr int      kLargeBuf    = 4 * 1024 * 1024; // 4 MB
static constexpr size_t   kRecvChunk   = 64 * 1024;       // 64 KB per recv op
static constexpr uint32_t kMaxMsgSize  = 64 * 1024 * 1024;

// ─────────────────────────────────────────────────────────────────────────────
// UringClientConnection
// ─────────────────────────────────────────────────────────────────────────────

class UringClientConnection
    : public Session
    , public std::enable_shared_from_this<UringClientConnection>
{
  int       fd_   = -1;
  io_uring  ring_ = {};
  EndPoint  remote_endpoint_;

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

    return fd;
  }

  // Initialise io_uring ring for the current fd_.
  // Called once in the constructor.  NOT called on reconnect — the same ring
  // is reused; only fd_ changes.
  void init_ring()
  {
    io_uring_params params{};
    // IORING_SETUP_SINGLE_ISSUER: hint to kernel that only one thread submits.
    // Available since kernel 6.0; silently ignored on older kernels if we clear
    // the flag on EINVAL.
#ifdef IORING_SETUP_SINGLE_ISSUER
    params.flags = IORING_SETUP_SINGLE_ISSUER;
#endif
    int ret = ::io_uring_queue_init_params(kRingDepth, &ring_, &params);
    if (ret < 0) {
#ifdef IORING_SETUP_SINGLE_ISSUER
      // Retry without the flag (older kernel)
      params.flags &= ~IORING_SETUP_SINGLE_ISSUER;
      ret = ::io_uring_queue_init_params(kRingDepth, &ring_, &params);
#endif
      if (ret < 0)
        throw std::runtime_error("io_uring_queue_init failed: " +
                                 std::string(std::strerror(-ret)));
    }
  }

  // ── low-level send / recv ─────────────────────────────────────────────────

  // Send all `len` bytes of `data`.  Loops on partial completions.
  // Returns false if the connection was closed by the peer.
  bool do_send_all(const void* data, size_t len)
  {
    const char* ptr = static_cast<const char*>(data);
    size_t remaining = len;

    while (remaining > 0) {
      io_uring_sqe* sqe = ::io_uring_get_sqe(&ring_);
      if (!sqe) {
        // Ring full — submit pending and retry
        ::io_uring_submit(&ring_);
        sqe = ::io_uring_get_sqe(&ring_);
        if (!sqe)
          return false;
      }

      ::io_uring_prep_send(sqe, fd_,
                           static_cast<const void*>(ptr),
                           remaining, 0);
      sqe->user_data = 1; // send tag

      int submitted = ::io_uring_submit(&ring_);
      if (submitted < 0)
        return false;

      io_uring_cqe* cqe = nullptr;
      int ret = ::io_uring_wait_cqe(&ring_, &cqe);
      if (ret < 0 || cqe->res <= 0) {
        if (cqe)
          ::io_uring_cqe_seen(&ring_, cqe);
        return false;
      }

      size_t sent = static_cast<size_t>(cqe->res);
      ::io_uring_cqe_seen(&ring_, cqe);

      ptr       += sent;
      remaining -= sent;
    }
    return true;
  }

  // Receive a complete wire message into `buf`.
  // Wire format: [uint32_t body_len (LE)][body_len bytes]
  // buf is reset on entry.  Returns false on connection error.
  bool do_recv_message(flat_buffer& buf)
  {
    buf.consume(buf.size()); // reset

    // ── phase 1: receive until we have at least 4 bytes (the length field) ──
    while (buf.size() < 4) {
      auto mb   = buf.prepare(kRecvChunk);

      io_uring_sqe* sqe = ::io_uring_get_sqe(&ring_);
      if (!sqe) {
        ::io_uring_submit(&ring_);
        sqe = ::io_uring_get_sqe(&ring_);
        if (!sqe)
          return false;
      }

      ::io_uring_prep_recv(sqe, fd_, mb.data(), mb.size(), 0);
      sqe->user_data = 0; // recv tag

      if (::io_uring_submit(&ring_) < 0)
        return false;

      io_uring_cqe* cqe = nullptr;
      int ret = ::io_uring_wait_cqe(&ring_, &cqe);
      if (ret < 0 || cqe->res <= 0) {
        if (cqe)
          ::io_uring_cqe_seen(&ring_, cqe);
        return false;
      }

      size_t n = static_cast<size_t>(cqe->res);
      ::io_uring_cqe_seen(&ring_, cqe);
      buf.commit(n);
    }

    // ── phase 2: receive body ────────────────────────────────────────────────
    const uint32_t body_len =
        *reinterpret_cast<const uint32_t*>(buf.data().data());

    if (body_len > kMaxMsgSize) {
      fail(boost::asio::error::no_buffer_space, "uring_client: body_len too large");
      return false;
    }

    const size_t total_needed = 4 + static_cast<size_t>(body_len);

    while (buf.size() < total_needed) {
      size_t want = total_needed - buf.size();
      // Cap per-recv to kRecvChunk so prepare() doesn't over-allocate
      size_t chunk = std::min(want, kRecvChunk);

      auto mb = buf.prepare(chunk);

      io_uring_sqe* sqe = ::io_uring_get_sqe(&ring_);
      if (!sqe) {
        ::io_uring_submit(&ring_);
        sqe = ::io_uring_get_sqe(&ring_);
        if (!sqe)
          return false;
      }

      ::io_uring_prep_recv(sqe, fd_, mb.data(), mb.size(), 0);
      sqe->user_data = 0;

      if (::io_uring_submit(&ring_) < 0)
        return false;

      io_uring_cqe* cqe = nullptr;
      int ret = ::io_uring_wait_cqe(&ring_, &cqe);
      if (ret < 0 || cqe->res <= 0) {
        if (cqe)
          ::io_uring_cqe_seen(&ring_, cqe);
        return false;
      }

      size_t n = static_cast<size_t>(cqe->res);
      ::io_uring_cqe_seen(&ring_, cqe);
      buf.commit(n);
    }

    return true;
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
    // Validate the wire format invariant (matches SocketConnection assertion).
    assert(*reinterpret_cast<const uint32_t*>(buffer.data().data()) ==
           buffer.size() - 4);

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
      uint32_t timeout_ms) override
  {
    // Synchronous fallback: execute on caller thread.
    // send_receive overwrites buffer with the response.
    try {
      send_receive(buffer, timeout_ms);
      if (completion_handler)
        (*completion_handler)(boost::system::error_code{}, buffer);
    } catch (const nprpc::ExceptionCommFailure&) {
      if (completion_handler)
        (*completion_handler)(boost::asio::error::connection_reset, buffer);
    }
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
  {
    ctx_.remote_endpoint = endpoint;
    // Disable the Asio timeout timer — we don't use it.
    timeout_timer_.expires_after(std::chrono::system_clock::duration::max());

    init_ring();

    fd_ = make_connected_fd(endpoint);
  }

  ~UringClientConnection()
  {
    ::io_uring_queue_exit(&ring_);
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

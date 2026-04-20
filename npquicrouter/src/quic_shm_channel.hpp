// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT
//
// SHM-based QUIC bidirectional channel between npquicrouter and Http3Server.
//
// Two rings per channel:
//   c2s  (client→server / ingress):  npquicrouter writes, Http3Server reads.
//   s2c  (server→client / egress):   Http3Server writes, npquicrouter reads.
//
// With both rings active, no per-session UDP socket is needed between the
// router and the backend: the real client endpoint is carried verbatim inside
// each ShmIngressFrame, so Http3Server always sees the true remote address in
// remote_ep_.  Egress frames therefore also carry the real client endpoint,
// letting ShmEgressReader call sendmsg() directly — no port translation.
//
// Frame layout for both directions:
//   [ShmIngressFrame / ShmEgressFrame header — 36 bytes]
//   [payload_len bytes of QUIC data]
//
// Ring ownership: npquicrouter creates both rings; Http3Server opens them.
// Both rings are removed on npquicrouter startup to clear stale state.
//
// SHM name convention:
//   make_shm_name(channel_name, "c2s") → /nprpc_<channel_name>_c2s
//   make_shm_name(channel_name, "s2c") → /nprpc_<channel_name>_s2c

#pragma once

#include <nprpc/impl/lock_free_ring_buffer.hpp>

#include <atomic>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

// Linux sendmsg / GSO
#include <netinet/in.h>
#include <netinet/udp.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

#include <arpa/inet.h>  // inet_ntop
#include <cerrno>
#include <cstring>       // strerror

// ─────────────────────────────────────────────────────────────────────────────
// Wire format (shared between both directions)
// ─────────────────────────────────────────────────────────────────────────────

// Ingress frame (c2s): written by npquicrouter, read by Http3Server.
// ep_storage carries the REAL client sockaddr so Http3Server uses it
// directly as remote_ep_ — no port translation needed.
#pragma pack(push, 1)
struct ShmIngressFrame {
  uint32_t payload_len;    // Total bytes of QUIC payload that follow.
  uint8_t  ep_len;         // Length of the sockaddr in ep_storage.
  uint8_t  _pad[3];        // Reserved, must be 0.
  uint8_t  ep_storage[28]; // sockaddr_in (16 B) or sockaddr_in6 (28 B).
};
#pragma pack(pop)

static_assert(sizeof(ShmIngressFrame) == 36,
              "ShmIngressFrame layout changed — update Http3Server reader");

// Egress frame (s2c): written by Http3Server, read by ShmEgressReader.
// Because Http3Server now sees the real client endpoint via ShmIngressFrame,
// remote_ep_ (and thus ep_storage here) always holds the real client address.
// ShmEgressReader uses it directly for sendmsg() — no lookup required.
#pragma pack(push, 1)
struct ShmEgressFrame {
  uint32_t payload_len;      // Total bytes of QUIC payload that follow.
  uint16_t gso_segment_size; // If >0: segmentation size (UDP_SEGMENT cmsg).
  uint8_t  ep_len;           // Length of the sockaddr in ep_storage.
  uint8_t  _pad;             // Reserved, must be 0.
  uint8_t  ep_storage[28];   // sockaddr_in (16 B) or sockaddr_in6 (28 B).
};
#pragma pack(pop)

static_assert(sizeof(ShmEgressFrame) == 36,
              "ShmEgressFrame layout changed — update Http3Server writer");

// Default ring buffer sizes.
static constexpr size_t kShmIngressRingSize = 32 * 1024 * 1024;
static constexpr size_t kShmEgressRingSize  = 32 * 1024 * 1024;

// ─────────────────────────────────────────────────────────────────────────────
// ShmIngressWriter  (npquicrouter side, c2s ring)
// ─────────────────────────────────────────────────────────────────────────────

// Owns the ingress ring buffer (creator side).  UdpRouter calls write_packet()
// for every UDP datagram received from a QUIC client.  Http3Server opens the
// ring from the other side and feeds packets to its QUIC engine.

class ShmIngressWriter
{
public:
  explicit ShmIngressWriter(const std::string& channel_name,
                            bool create_ring = true)
      : ring_name_(nprpc::impl::make_shm_name(channel_name, "c2s"))
  {
    if (create_ring) {
      nprpc::impl::LockFreeRingBuffer::remove(ring_name_);
      ring_ = nprpc::impl::LockFreeRingBuffer::create(ring_name_,
                                                      kShmIngressRingSize);
    } else {
      // Secondary producer: open the ring already created by the primary.
      ring_ = nprpc::impl::LockFreeRingBuffer::open(ring_name_);
    }
  }

  ShmIngressWriter(const ShmIngressWriter&) = delete;
  ShmIngressWriter& operator=(const ShmIngressWriter&) = delete;

  // Write one UDP datagram with its real client sockaddr to the ring.
  // Returns false (and drops the datagram) if the ring is full.
  bool write_packet(const sockaddr* client_addr,
                    socklen_t       client_addrlen,
                    const uint8_t*  data,
                    size_t          len)
  {
    if (client_addrlen > sizeof(ShmIngressFrame::ep_storage)) return false;
    const size_t msg_size = sizeof(ShmIngressFrame) + len;
    auto rsv = ring_->try_reserve_write(msg_size);
    if (!rsv) return false;
    ShmIngressFrame hdr{};
    hdr.payload_len = static_cast<uint32_t>(len);
    hdr.ep_len      = static_cast<uint8_t>(client_addrlen);
    std::memcpy(hdr.ep_storage, client_addr, client_addrlen);
    std::memcpy(rsv.data,                &hdr,  sizeof(hdr));
    std::memcpy(rsv.data + sizeof(hdr),  data,  len);
    ring_->commit_write(rsv, msg_size);
    return true;
  }

  const std::string& ring_name() const noexcept { return ring_name_; }

private:
  std::string                                       ring_name_;
  std::unique_ptr<nprpc::impl::LockFreeRingBuffer>  ring_;
};

// ─────────────────────────────────────────────────────────────────────────────
// ShmEgressReader  (npquicrouter side, s2c ring)
// ─────────────────────────────────────────────────────────────────────────────

// Owns the egress ring buffer (creator side) and drains it in a background
// thread, forwarding each frame to the UDP client via sendmsg(GSO).
// Because Http3Server now sees the real client endpoint via ShmIngressFrame,
// ep_storage in every egress frame already holds the real destination —
// no port-to-client translation is needed.

class ShmEgressReader
{
public:
  // @param channel_name  Config name, e.g. "quic_edge".
  // @param send_sock_fd  npquicrouter's listening UDP socket fd.
  ShmEgressReader(const std::string& channel_name, int send_sock_fd)
      : sock_fd_(send_sock_fd)
      , ring_name_(nprpc::impl::make_shm_name(channel_name, "s2c"))
  {
    nprpc::impl::LockFreeRingBuffer::remove(ring_name_);
    ring_ = nprpc::impl::LockFreeRingBuffer::create(ring_name_, kShmEgressRingSize);
  }

  ~ShmEgressReader() { stop(); }

  ShmEgressReader(const ShmEgressReader&) = delete;
  ShmEgressReader& operator=(const ShmEgressReader&) = delete;

  void start()
  {
    running_.store(true, std::memory_order_relaxed);
    thread_ = std::thread(&ShmEgressReader::loop, this);
  }

  void stop()
  {
    running_.store(false, std::memory_order_release);
    if (thread_.joinable()) thread_.join();
  }

  const std::string& ring_name() const noexcept { return ring_name_; }

private:
  void loop()
  {
    while (running_.load(std::memory_order_acquire)) {
      auto view = ring_->try_read_view();
      if (!view) {
        struct timespec ts = {0, 50'000}; // 50 µs
        nanosleep(&ts, nullptr);
        continue;
      }

      if (view.size < sizeof(ShmEgressFrame)) {
        ring_->commit_read(view);
        continue;
      }

      ShmEgressFrame hdr;
      std::memcpy(&hdr, view.data, sizeof(hdr));
      const size_t payload_len = hdr.payload_len;

      if (view.size < sizeof(ShmEgressFrame) + payload_len ||
          hdr.ep_len == 0 || hdr.ep_len > sizeof(hdr.ep_storage)) {
        ring_->commit_read(view);
        continue;
      }

      const uint8_t* payload = view.data + sizeof(ShmEgressFrame);
      send_frame(hdr, payload, payload_len);
      ring_->commit_read(view);
    }
  }

  void send_frame(const ShmEgressFrame& hdr,
                  const uint8_t*        payload,
                  size_t                payload_len)
  {
    // ep_storage carries the real client endpoint written by Http3Server.
    sockaddr_storage dest_addr{};
    std::memcpy(&dest_addr, hdr.ep_storage, hdr.ep_len);
    const socklen_t dest_len = static_cast<socklen_t>(hdr.ep_len);

    iovec iov{};
    iov.iov_base = const_cast<uint8_t*>(payload);
    iov.iov_len  = payload_len;

    msghdr msg{};
    msg.msg_name    = &dest_addr;
    msg.msg_namelen = dest_len;
    msg.msg_iov     = &iov;
    msg.msg_iovlen  = 1;

#ifdef UDP_SEGMENT
    alignas(cmsghdr) unsigned char ctrl[CMSG_SPACE(sizeof(uint16_t))]{};
    if (hdr.gso_segment_size > 0 && payload_len > hdr.gso_segment_size) {
      msg.msg_control    = ctrl;
      msg.msg_controllen = sizeof(ctrl);
      auto* cmsg         = CMSG_FIRSTHDR(&msg);
      cmsg->cmsg_level   = IPPROTO_UDP;
      cmsg->cmsg_type    = UDP_SEGMENT;
      cmsg->cmsg_len     = CMSG_LEN(sizeof(uint16_t));
      std::memcpy(CMSG_DATA(cmsg), &hdr.gso_segment_size,
                  sizeof(hdr.gso_segment_size));
    }
#endif

    const auto ret = ::sendmsg(sock_fd_, &msg, MSG_DONTWAIT);
    if (ret < 0) {
      const int err = errno;
      char addr_buf[INET6_ADDRSTRLEN] = {};
      uint16_t dest_port = 0;
      if (dest_addr.ss_family == AF_INET) {
        const auto* s = reinterpret_cast<const sockaddr_in*>(&dest_addr);
        inet_ntop(AF_INET, &s->sin_addr, addr_buf, sizeof(addr_buf));
        dest_port = ntohs(s->sin_port);
      } else {
        const auto* s = reinterpret_cast<const sockaddr_in6*>(&dest_addr);
        inet_ntop(AF_INET6, &s->sin6_addr, addr_buf, sizeof(addr_buf));
        dest_port = ntohs(s->sin6_port);
      }
      std::cerr << "[SHM Egress] sendmsg failed: " << std::strerror(err)
                << " (errno=" << err << ") dst=" << addr_buf << ':' << dest_port
                << " len=" << payload_len << "\n";
    }
  }

  int                                               sock_fd_;
  std::string                                       ring_name_;
  std::unique_ptr<nprpc::impl::LockFreeRingBuffer>  ring_;
  std::thread                                       thread_;
  std::atomic<bool>                                 running_{false};
};

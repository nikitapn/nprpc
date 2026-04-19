// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT
//
// SHM-based QUIC egress channel.
//
// Http3Server (backend) writes ShmEgressFrame entries to the ring buffer.
// ShmEgressReader (npquicrouter) reads them and calls sendmsg(GSO) to send
// QUIC packets to the actual UDP client — restoring the GSO batch that would
// otherwise be broken by the double-UDP-hop.
//
// Frame layout (header followed by payload_len bytes):
//
//   [ShmEgressFrame header - 36 bytes]
//   [payload bytes]
//
// Ring buffer ownership: npquicrouter creates (creator=true), Http3Server
// opens (creator=false). The ring is cleaned up when npquicrouter exits.
//
// SHM name convention:
//   make_shm_name(channel_name, "s2c") → /nprpc_<channel_name>_s2c
//   e.g. "quic_edge" → /nprpc_quic_edge_s2c

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

#include <shared_mutex>
#include <unordered_map>

// ─────────────────────────────────────────────────────────────────────────────
// Wire format
// ─────────────────────────────────────────────────────────────────────────────

// Header that precedes the payload bytes inside each ring buffer message.
// Written by Http3Server, read by ShmEgressReader.
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

// Total size of a ring message for a given payload.
inline constexpr size_t shm_egress_msg_size(size_t payload_len)
{
  return sizeof(ShmEgressFrame) + payload_len;
}

// Default egress ring buffer size: 32 MB — enough for many GSO bursts in
// flight simultaneously.
static constexpr size_t kShmEgressRingSize = 32 * 1024 * 1024;

// ─────────────────────────────────────────────────────────────────────────────
// PortToClientMap
// ─────────────────────────────────────────────────────────────────────────────

// Http3Server sees each QUIC client as 127.0.0.1:<backend_sock_ephemeral_port>.
// The SHM egress frame therefore contains that loopback address as the
// destination.  This map translates backend_sock local port → real client
// sockaddr so that ShmEgressReader can call sendmsg with the correct dest.
//
// Lifecycle: populated by UdpRouter (Asio thread), read by ShmEgressReader
// (background thread) — protected by a shared_mutex.

class PortToClientMap
{
public:
  void insert(uint16_t backend_port,
              const sockaddr* client_addr,
              socklen_t       client_addrlen)
  {
    Entry e{};
    e.addrlen = client_addrlen;
    std::memcpy(&e.addr, client_addr, client_addrlen);
    std::unique_lock lock(mu_);
    map_[backend_port] = e;
  }

  void erase(uint16_t backend_port)
  {
    std::unique_lock lock(mu_);
    map_.erase(backend_port);
  }

  // Returns true and fills addr_out/len_out when found.
  bool lookup(uint16_t        backend_port,
              sockaddr_storage& addr_out,
              socklen_t&        len_out) const
  {
    std::shared_lock lock(mu_);
    auto it = map_.find(backend_port);
    if (it == map_.end()) return false;
    std::memcpy(&addr_out, &it->second.addr, it->second.addrlen);
    len_out = it->second.addrlen;
    return true;
  }

private:
  struct Entry {
    sockaddr_storage addr{};
    socklen_t        addrlen = 0;
  };
  mutable std::shared_mutex                     mu_;
  std::unordered_map<uint16_t, Entry>           map_;
};

// ─────────────────────────────────────────────────────────────────────────────
// ShmEgressReader
// ─────────────────────────────────────────────────────────────────────────────

// Owns the egress ring buffer (creator side) and drains it in a background
// thread, forwarding each frame to the UDP client via sendmsg(GSO).
//
// Usage:
//   ShmEgressReader reader("quic_edge", listen_sock_fd);
//   reader.start();
//   // ... run until shutdown ...
//   // reader destructor calls stop() and joins the thread

class ShmEgressReader
{
public:
  // @param channel_name  Config name, e.g. "quic_edge".
  //                      Ring SHM name will be make_shm_name(channel_name,"s2c").
  // @param send_sock_fd  The file descriptor of npquicrouter's listening UDP
  //                      socket.  sendmsg is called on this fd.
  // @param port_map      Shared port→client translation table populated by
  //                      UdpRouter as sessions are created/destroyed.
  ShmEgressReader(const std::string&               channel_name,
                  int                              send_sock_fd,
                  std::shared_ptr<PortToClientMap> port_map)
      : sock_fd_(send_sock_fd)
      , ring_name_(nprpc::impl::make_shm_name(channel_name, "s2c"))
      , port_map_(std::move(port_map))
  {
    // Remove any stale SHM from a previous crash before creating.
    nprpc::impl::LockFreeRingBuffer::remove(ring_name_);
    ring_ = nprpc::impl::LockFreeRingBuffer::create(ring_name_, kShmEgressRingSize);
  }

  ~ShmEgressReader() { stop(); }

  // Non-copyable / non-movable (owns a thread and a raw fd reference).
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

  nprpc::impl::LockFreeRingBuffer& ring() noexcept { return *ring_; }
  const std::string& ring_name() const noexcept { return ring_name_; }

private:
  void loop()
  {
    while (running_.load(std::memory_order_acquire)) {
      auto view = ring_->try_read_view();
      if (!view) {
        // Ring is empty — yield briefly before polling again.
        // A future version can use the ring's eventfd for blocking wait.
        struct timespec ts = {0, 50'000}; // 50 µs
        nanosleep(&ts, nullptr);
        continue;
      }

      if (view.size < sizeof(ShmEgressFrame)) {
        // Corrupt frame — skip it.
        ring_->commit_read(view);
        continue;
      }

      ShmEgressFrame hdr;
      std::memcpy(&hdr, view.data, sizeof(hdr));

      const uint8_t* payload = view.data + sizeof(ShmEgressFrame);
      size_t payload_len = hdr.payload_len;

      if (view.size < sizeof(ShmEgressFrame) + payload_len ||
          hdr.ep_len == 0 || hdr.ep_len > sizeof(hdr.ep_storage)) {
        ring_->commit_read(view);
        continue;
      }

      std::cerr << "[SHM Egress] Sending " << payload_len
                << " bytes to client (GSO segment size: " << hdr.gso_segment_size
                << ")\n";

      send_frame(hdr, payload, payload_len);
      ring_->commit_read(view);
    }
  }

  void send_frame(const ShmEgressFrame& hdr,
                  const uint8_t*        payload,
                  size_t                payload_len)
  {
    // Http3Server's remote_ep_ is 127.0.0.1:<backend_sock_port>.
    // We need to translate that to the real client endpoint using port_map_.
    sockaddr_storage dest_addr{};
    socklen_t        dest_len = 0;

    uint16_t backend_port = 0;
    if (hdr.ep_len == sizeof(sockaddr_in)) {
      const sockaddr_in* sin =
          reinterpret_cast<const sockaddr_in*>(hdr.ep_storage);
      backend_port = ntohs(sin->sin_port);
    } else if (hdr.ep_len == sizeof(sockaddr_in6)) {
      const sockaddr_in6* sin6 =
          reinterpret_cast<const sockaddr_in6*>(hdr.ep_storage);
      backend_port = ntohs(sin6->sin6_port);
    } else {
      std::cerr << "[SHM Egress] unknown ep_len=" << (int)hdr.ep_len << "\n";
      return;
    }

    if (!port_map_->lookup(backend_port, dest_addr, dest_len)) {
      std::cerr << "[SHM Egress] no client found for backend_port=" << backend_port
                << " — session torn down?\n";
      return;
    }

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
      // Format the resolved destination for the error message.
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
  std::shared_ptr<PortToClientMap>                  port_map_;
  std::unique_ptr<nprpc::impl::LockFreeRingBuffer>  ring_;
  std::thread                                       thread_;
  std::atomic<bool>                                 running_{false};
};

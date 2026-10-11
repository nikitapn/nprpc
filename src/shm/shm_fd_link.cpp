// Copyright (c) 2021-2026, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include "../logging.hpp"
#include <nprpc/impl/lock_free_ring_buffer.hpp>
#include <nprpc/impl/shm_fd_link.hpp>

#include <algorithm>
#include <cstring>

#if defined(__linux__)
# include <cerrno>
# include <poll.h>
# include <sys/socket.h>
# include <sys/un.h>
# include <unistd.h>
#endif

namespace nprpc::impl {

#if defined(__linux__)

namespace {

// The fixed part of a record; the descriptors ride along as SCM_RIGHTS.
struct FdRecord {
  static constexpr uint32_t MAGIC = 0x46445243; // "FDRC"
  uint32_t magic = MAGIC;
  uint16_t slot = 0;
  uint16_t count = 0;
};

// Records that came before their message.  A well-behaved peer has at most
// one per thread sending concurrently; the bound is for one that is not.
constexpr size_t kMaxEarlyRecords = kRingSlots;

// How long a reader waits for the descriptors of a message it already holds.
// They were queued before the message was committed, so a wait that runs
// out means the peer marked a message without sending them.
constexpr auto kReceiveTimeout = std::chrono::seconds(2);

// How long the client waits for the listener to take its connection.
constexpr int kHelloTimeoutMs = 2000;

bool fill_address(const std::string& path, sockaddr_un& addr)
{
  if (path.empty() || path.size() >= sizeof(addr.sun_path))
    return false;
  std::memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
  return true;
}

} // namespace

std::string ShmFdLink::socket_path(const std::string& listener_name)
{
  // Beside the rings, so that whoever can reach them — another container
  // with the same directory mounted as its /dev/shm — can reach it too.
  return "/dev/shm" + make_shm_name(listener_name, "fd");
}

std::shared_ptr<ShmFdLink> ShmFdLink::connect(const std::string& listener_name,
                                              const std::string& channel_id)
{
  sockaddr_un addr;
  if (!fill_address(socket_path(listener_name), addr) ||
      channel_id.size() >= sizeof(ShmFdHello::channel_id))
    return nullptr;

  UniqueFd sock(::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0));
  if (!sock)
    return nullptr;

  if (::connect(sock.get(), reinterpret_cast<const sockaddr*>(&addr),
                sizeof(addr)) != 0) {
    // ENOENT: a server from before this existed, or one that could not
    // create its socket.  Not an error — the channel just has no fds.
    if (errno != ENOENT)
      NPRPC_LOG_WARN("SHM fd link: cannot connect to {}: {}", addr.sun_path,
                     std::strerror(errno));
    return nullptr;
  }

  ShmFdHello hello;
  std::memcpy(hello.channel_id, channel_id.c_str(), channel_id.size() + 1);
  if (::send(sock.get(), &hello, sizeof(hello), MSG_NOSIGNAL) !=
      static_cast<ssize_t>(sizeof(hello))) {
    NPRPC_LOG_WARN("SHM fd link: hello failed: {}", std::strerror(errno));
    return nullptr;
  }

  pollfd pfd{sock.get(), POLLIN, 0};
  int ready;
  do {
    ready = ::poll(&pfd, 1, kHelloTimeoutMs);
  } while (ready < 0 && errno == EINTR);

  uint32_t answer = 0;
  if (ready != 1 ||
      ::recv(sock.get(), &answer, sizeof(answer), 0) !=
          static_cast<ssize_t>(sizeof(answer)) ||
      answer != ShmFdHello::kAccepted) {
    NPRPC_LOG_WARN("SHM fd link: listener did not accept channel {}",
                   channel_id);
    return nullptr;
  }

  auto link = std::make_shared<ShmFdLink>();
  link->attach(std::move(sock));
  return link;
}

bool ShmFdLink::attach(UniqueFd socket)
{
  if (attached() || !socket)
    return false;
  socket_ = std::move(socket);
  attached_.store(true, std::memory_order_release);
  return true;
}

bool ShmFdLink::send(uint16_t slot, std::vector<UniqueFd>& fds)
{
  if (!attached() || fds.empty() || fds.size() > kMaxFdsPerMessage) {
    if (fds.size() > kMaxFdsPerMessage)
      NPRPC_LOG_ERROR("SHM fd link: {} descriptors in one message, the most "
                      "is {}",
                      fds.size(), kMaxFdsPerMessage);
    fds.clear();
    return false;
  }

  FdRecord record;
  record.slot = slot;
  record.count = static_cast<uint16_t>(fds.size());

  iovec iov{&record, sizeof(record)};

  alignas(cmsghdr) char control[CMSG_SPACE(sizeof(int) * kMaxFdsPerMessage)];
  const size_t fds_bytes = sizeof(int) * fds.size();

  msghdr msg{};
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_control = control;
  msg.msg_controllen = CMSG_SPACE(fds_bytes);

  cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
  cmsg->cmsg_level = SOL_SOCKET;
  cmsg->cmsg_type = SCM_RIGHTS;
  cmsg->cmsg_len = CMSG_LEN(fds_bytes);
  auto* out = reinterpret_cast<int*>(CMSG_DATA(cmsg));
  for (size_t i = 0; i < fds.size(); ++i)
    out[i] = fds[i].get();

  ssize_t n;
  do {
    n = ::sendmsg(socket_.get(), &msg, MSG_NOSIGNAL);
  } while (n < 0 && errno == EINTR);

  // In flight now, or never going to be: either way these copies are done.
  fds.clear();

  if (n != static_cast<ssize_t>(sizeof(record))) {
    NPRPC_LOG_ERROR("SHM fd link: sendmsg failed: {}",
                    n < 0 ? std::strerror(errno) : "short write");
    return false;
  }
  return true;
}

bool ShmFdLink::receive_record(uint16_t& slot,
                               std::vector<UniqueFd>& fds,
                               std::chrono::milliseconds timeout)
{
  pollfd pfd{socket_.get(), POLLIN, 0};
  int ready;
  do {
    ready = ::poll(&pfd, 1, static_cast<int>(timeout.count()));
  } while (ready < 0 && errno == EINTR);
  if (ready != 1)
    return false;

  FdRecord record;
  iovec iov{&record, sizeof(record)};
  alignas(cmsghdr) char control[CMSG_SPACE(sizeof(int) * kMaxFdsPerMessage)];

  msghdr msg{};
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_control = control;
  msg.msg_controllen = sizeof(control);

  ssize_t n;
  do {
    n = ::recvmsg(socket_.get(), &msg, MSG_CMSG_CLOEXEC);
  } while (n < 0 && errno == EINTR);
  if (n <= 0)
    return false; // peer gone

  // Own whatever arrived before looking at anything else, so that every
  // way out of here closes it.
  fds.clear();
  for (cmsghdr* c = CMSG_FIRSTHDR(&msg); c; c = CMSG_NXTHDR(&msg, c)) {
    if (c->cmsg_level != SOL_SOCKET || c->cmsg_type != SCM_RIGHTS)
      continue;
    const size_t count = (c->cmsg_len - CMSG_LEN(0)) / sizeof(int);
    const auto* in = reinterpret_cast<const int*>(CMSG_DATA(c));
    for (size_t i = 0; i < count; ++i)
      fds.emplace_back(in[i]);
  }

  if (n != static_cast<ssize_t>(sizeof(record)) ||
      record.magic != FdRecord::MAGIC) {
    NPRPC_LOG_ERROR("SHM fd link: malformed record");
    fds.clear();
    return false;
  }
  if ((msg.msg_flags & MSG_CTRUNC) != 0 || fds.size() != record.count) {
    // Out of descriptors here (RLIMIT_NOFILE), most likely.  What did come
    // is still delivered; the indices past it find nothing.
    NPRPC_LOG_ERROR("SHM fd link: {} of {} descriptors arrived", fds.size(),
                    record.count);
  }

  slot = record.slot;
  return true;
}

std::vector<UniqueFd> ShmFdLink::receive(uint16_t slot)
{
  auto early = std::find_if(early_.begin(), early_.end(),
                            [slot](const auto& r) { return r.first == slot; });
  if (early != early_.end()) {
    auto fds = std::move(early->second);
    early_.erase(early);
    return fds;
  }

  if (!attached())
    return {};

  const auto deadline = std::chrono::steady_clock::now() + kReceiveTimeout;
  for (;;) {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now());
    uint16_t got = 0;
    std::vector<UniqueFd> fds;
    if (left.count() <= 0 || !receive_record(got, fds, left)) {
      NPRPC_LOG_ERROR("SHM fd link: descriptors for slot {} never arrived",
                      slot);
      return {};
    }
    if (got == slot)
      return fds;

    if (early_.size() >= kMaxEarlyRecords)
      early_.erase(early_.begin());
    early_.emplace_back(got, std::move(fds));
  }
}

#else // !__linux__

std::string ShmFdLink::socket_path(const std::string&) { return {}; }

std::shared_ptr<ShmFdLink> ShmFdLink::connect(const std::string&,
                                              const std::string&)
{
  return nullptr;
}

bool ShmFdLink::attach(UniqueFd) { return false; }

bool ShmFdLink::send(uint16_t, std::vector<UniqueFd>& fds)
{
  fds.clear();
  return false;
}

std::vector<UniqueFd> ShmFdLink::receive(uint16_t) { return {}; }

bool ShmFdLink::receive_record(uint16_t&,
                               std::vector<UniqueFd>&,
                               std::chrono::milliseconds)
{
  return false;
}

#endif

} // namespace nprpc::impl

// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include "../logging.hpp"
#include <nprpc/impl/lock_free_ring_buffer.hpp>
#include <nprpc/impl/shared_memory_listener.hpp>

#include <boost/interprocess/shared_memory_object.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#if defined(__linux__)
#include <filesystem>
#endif

namespace nprpc::impl {

// Why this exists at all.
//
// The server creates a client's rings and is the only side that removes them,
// which is right while it is running and worth nothing when it is not: a
// process that is killed, crashes, or calls exit() without unwinding runs no
// destructor, and its rings stay in /dev/shm holding real memory — they are
// resident, not reserved — until somebody reboots. Two rings per client
// accumulating over a day of restarts is measured in gigabytes.
//
// No destructor can fix that, because the case is precisely the one where no
// destructor runs. The next server to start is the first process in a
// position to notice, so this runs there.

#if defined(__linux__)

namespace {

constexpr const char* kShmDir    = "/dev/shm";
constexpr std::string_view kPrefix = "nprpc_";

// What is known about the processes behind one channel's segments.
struct Group {
  std::vector<std::string> segments;
  bool any_dead  = false;
  bool any_alive = false;
};

// "nprpc_<id>_c2s" -> "nprpc_<id>". A channel's two rings live or die
// together, and the accept ring is a group of its own.  nullopt for a name
// that is not a ring at all.
//
// This is a filter as much as it is a grouping, and it has to be: probing a
// segment means asking Boost.Interprocess to open it as a managed segment,
// and a managed open of something that is *not* one waits for an
// initialisation marker that is never coming.  So only names this transport
// gives its rings are ever opened.
std::optional<std::string> group_of(const std::string& name)
{
  for (std::string_view suffix : {"_c2s", "_s2c", "_accept"}) {
    if (name.size() > suffix.size() &&
        name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
      return name.substr(0, name.size() - suffix.size());
    }
  }
  return std::nullopt;
}

// Every ring carries a 1024-slot header array of 16 bytes each before any
// payload, so anything smaller than that is not one whatever it is called —
// a second guard against opening a segment that will not answer.
constexpr std::uintmax_t kSmallestRing = 16 * 1024;

} // namespace

size_t reap_stale_shm_segments()
{
  std::map<std::string, Group> groups;

  std::error_code ec;
  std::filesystem::directory_iterator dir(kShmDir, ec);
  if (ec) {
    NPRPC_LOG_WARN("Cannot scan {} for stale segments: {}", kShmDir,
                   ec.message());
    return 0;
  }

  // Descriptor side-channel sockets (ShmFdLink), "nprpc_<listener>_fd".
  std::vector<std::string> fd_sockets;

  for (const auto& entry : dir) {
    const std::string name = entry.path().filename().string();
    if (name.compare(0, kPrefix.size(), kPrefix) != 0)
      continue;

    if (name.ends_with("_fd") && entry.is_socket(ec)) {
      fd_sockets.push_back(name);
      continue;
    }

    const auto group_id = group_of(name);
    if (!group_id)
      continue;

    const auto size = entry.file_size(ec);
    if (ec || size < kSmallestRing)
      continue;

    // The header is also what confirms the segment is one of ours: anything
    // without a readable RingBufferHeader is left where it is.
    const auto owner = LockFreeRingBuffer::peek_owner("/" + name);
    if (!owner)
      continue;

    auto& group = groups[*group_id];
    group.segments.push_back(name);
    if (!owner->valid()) {
      // A ring nobody has claimed yet. Says nothing either way — a server
      // that has just created a client's rings is in exactly this state.
      continue;
    }
    if (process_alive(*owner))
      group.any_alive = true;
    else
      group.any_dead = true;
  }

  size_t removed = 0;
  for (const auto& [id, group] : groups) {
    // One dead end is enough to condemn a channel, but only if the other end
    // is not still there: a live process may hold a mapping of a ring whose
    // writer has gone, and unlinking underneath it would leave it logging a
    // failed cleanup of its own later on.
    if (!group.any_dead || group.any_alive)
      continue;

    for (const auto& segment : group.segments) {
      // Leading slash, as everything else in this transport names them.
      const std::string shm_name = "/" + segment;
      if (boost::interprocess::shared_memory_object::remove(shm_name.c_str())) {
        ++removed;
        NPRPC_LOG_INFO("Removed stale shared memory segment '{}'", shm_name);
      } else {
        NPRPC_LOG_WARN("Could not remove stale segment '{}'", shm_name);
      }
    }
  }

  // A listener's socket outlives it the same way its accept ring does, and
  // is no use without the ring: it is made after the ring and removed
  // before it, so one without the other is a leftover.
  for (const auto& socket : fd_sockets) {
    const std::string base = socket.substr(0, socket.size() - 3);
    const auto ring = std::filesystem::path(kShmDir) / (base + "_accept");
    if (std::filesystem::exists(ring, ec) || ec)
      continue;
    if (std::filesystem::remove(std::filesystem::path(kShmDir) / socket, ec)) {
      ++removed;
      NPRPC_LOG_INFO("Removed stale socket '{}/{}'", kShmDir, socket);
    }
  }

  if (removed != 0)
    NPRPC_LOG_INFO("Removed {} stale shared memory segment(s)", removed);

  return removed;
}

#else

size_t reap_stale_shm_segments()
{
  // Nothing to walk: Windows unmaps a section when its last handle closes,
  // and macOS gives no directory of POSIX shared memory to enumerate.
  return 0;
}

#endif

} // namespace nprpc::impl

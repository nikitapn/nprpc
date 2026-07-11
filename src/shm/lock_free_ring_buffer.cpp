// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include "../logging.hpp"
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/shared_memory_object.hpp>

#include <thread>

#include <nprpc/common.hpp>
#include <nprpc/impl/lock_free_ring_buffer.hpp>
#include <nprpc/impl/nprpc_impl.hpp>

namespace nprpc::impl {

// Helper to get page size
static size_t get_page_size()
{
  static size_t page_size = static_cast<size_t>(sysconf(_SC_PAGE_SIZE));
  return page_size;
}

// SHM layout:
//
//  [0 .. page_size)                          — page 0: RingBufferHeader (Boost)
//  [page_size .. page_size+header_ring_sz)   — SlotHeader[kRingSlots] (plain map)
//  [page_size+header_ring_sz .. total)       — payload data (mirrored separately)
//
// Header ring: NOT mirrored. SlotHeader[s] is always at a stable virtual
// address (shm_base + page_size + s*sizeof(SlotHeader)); direct index access,
// no wrap-boundary issue, actual_size always at the same offset.
//
// Payload ring: mirrored (2× ring_window). Variable-length payloads can straddle
// the ring boundary; the double-mmap lets the caller use a single memcpy.

static size_t round_up_page(size_t n)
{
  size_t page = get_page_size();
  return (n + page - 1) & ~(page - 1);
}

// Page-aligned size of SlotHeader[kRingSlots] in SHM.
static size_t header_ring_bytes()
{
  return round_up_page(kRingSlots * sizeof(SlotHeader));
}

// How long the consumer waits for a claimed slot to be committed before
// giving up and reporting "empty".  Covers the producer's window between
// the write_cursor CAS and the actual_size commit store (two plain stores
// plus the payload memcpy).  A very large memcpy can exceed this — the read
// cursor doesn't move, so the next call simply retries the slot; a producer
// that died mid-write stalls the ring but no longer hangs the consumer
// inside a try_ function.
static constexpr int kCommitSpinYields = 1024;

// Shared mmap setup used by both create() and open().
static bool setup_mappings(int fd,
                           size_t page_size,
                           size_t hdr_ring_sz,
                           size_t ring_window,
                           SlotHeader** slot_headers_out,
                           uint8_t**    payload_region_out,
                           void**       mirror_base_out)
{
  // Map the slot-header region (plain, no mirror).
  void* hdr_map = mmap(nullptr, hdr_ring_sz,
                       PROT_READ | PROT_WRITE, MAP_SHARED,
                       fd, static_cast<off_t>(page_size));
  if (hdr_map == MAP_FAILED) {
    NPRPC_LOG_ERROR("mmap slot header ring failed: {}", strerror(errno));
    return false;
  }
  *slot_headers_out = static_cast<SlotHeader*>(hdr_map);

  // Reserve 2× ring_window for the mirrored payload mapping.
  size_t payload_offset = page_size + hdr_ring_sz;
  void* reserved = mmap(nullptr, 2 * ring_window, PROT_NONE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (reserved == MAP_FAILED) {
    munmap(hdr_map, hdr_ring_sz);
    NPRPC_LOG_ERROR("mmap reserve for payload failed: {}", strerror(errno));
    return false;
  }

  void* first = mmap(reserved, ring_window, PROT_READ | PROT_WRITE,
                     MAP_SHARED | MAP_FIXED, fd,
                     static_cast<off_t>(payload_offset));
  if (first == MAP_FAILED || first != reserved) {
    munmap(hdr_map, hdr_ring_sz);
    munmap(reserved, 2 * ring_window);
    NPRPC_LOG_ERROR("mmap first payload window failed: {}", strerror(errno));
    return false;
  }

  void* second_addr = static_cast<uint8_t*>(reserved) + ring_window;
  void* second = mmap(second_addr, ring_window, PROT_READ | PROT_WRITE,
                      MAP_SHARED | MAP_FIXED, fd,
                      static_cast<off_t>(payload_offset));
  if (second == MAP_FAILED || second != second_addr) {
    munmap(first, ring_window);
    munmap(hdr_map, hdr_ring_sz);
    munmap(reserved, 2 * ring_window);
    NPRPC_LOG_ERROR("mmap second payload window failed: {}", strerror(errno));
    return false;
  }

  *payload_region_out = static_cast<uint8_t*>(reserved);
  *mirror_base_out    = reserved;
  return true;
}

size_t LockFreeRingBuffer::calculate_shm_size(size_t buffer_size)
{
  size_t page_size = get_page_size();
  return page_size + header_ring_bytes() + buffer_size;
}

std::unique_ptr<LockFreeRingBuffer>
LockFreeRingBuffer::create(const std::string& name, size_t buffer_size, size_t max_message_size)
{
  try {
    // Payload offsets live in the low 48 bits of the packed cursors.
    if (buffer_size >= (uint64_t(1) << kCursorSlotShift))
      throw std::runtime_error("buffer_size exceeds 48-bit payload cursor range");

    size_t page_size   = get_page_size();
    size_t hdr_ring_sz = header_ring_bytes();
    size_t ring_window = round_up_page(buffer_size);
    // Size the file to the page-aligned mapping window, not buffer_size:
    // wrapping messages spill into [buffer_size, ring_window) through the
    // mirror, and MAP_SHARED stores past EOF are undefined.
    size_t total_size  = page_size + hdr_ring_sz + ring_window;

    boost::interprocess::managed_shared_memory shm(
        boost::interprocess::create_only, name.c_str(), total_size);

    auto* header = shm.construct<RingBufferHeader>("header")(buffer_size, max_message_size);
    if (!header)
      throw std::runtime_error("Failed to construct RingBufferHeader");

    {
      uint8_t* shm_base  = static_cast<uint8_t*>(shm.get_address());
      size_t   hdr_end   = (reinterpret_cast<uint8_t*>(header) - shm_base) + sizeof(RingBufferHeader);
      if (hdr_end > page_size)
        throw std::runtime_error("RingBufferHeader doesn't fit in first page");
    }

    std::string posix_name = name;
    if (posix_name.empty() || posix_name[0] != '/')
      posix_name.insert(posix_name.begin(), '/');

    int fd = shm_open(posix_name.c_str(), O_RDWR | O_CLOEXEC, 0600);
    if (fd == -1)
      throw std::runtime_error(std::string("shm_open: ") + strerror(errno));

    SlotHeader* slot_headers   = nullptr;
    uint8_t*    payload_region = nullptr;
    void*       mirror_base    = nullptr;
    bool ok = setup_mappings(fd, page_size, hdr_ring_sz, ring_window,
                             &slot_headers, &payload_region, &mirror_base);
    close(fd);
    if (!ok)
      throw std::runtime_error("setup_mappings failed");

    // Zero-initialise the slot header array (actual_size must start at 0).
    std::memset(slot_headers, 0, hdr_ring_sz);

    NPRPC_LOG_INFO(
        "Created ring buffer '{}': payload={} bytes, {} header slots, "
        "payload mirror at {}",
        name, buffer_size, kRingSlots, static_cast<void*>(payload_region));

    return std::unique_ptr<LockFreeRingBuffer>(new LockFreeRingBuffer(
        name, std::move(shm), header, slot_headers, payload_region,
        mirror_base, ring_window, true));

  } catch (const boost::interprocess::interprocess_exception& e) {
    NPRPC_LOG_ERROR("Failed to create ring buffer '{}': {}", name, e.what());
    throw;
  }
}

std::unique_ptr<LockFreeRingBuffer>
LockFreeRingBuffer::open(const std::string& name)
{
  try {
    boost::interprocess::managed_shared_memory shm(
        boost::interprocess::open_only, name.c_str());

    auto result = shm.find<RingBufferHeader>("header");
    if (result.first == nullptr)
      throw std::runtime_error("Header not found in shared memory");
    RingBufferHeader* header = result.first;

    size_t page_size   = get_page_size();
    size_t hdr_ring_sz = header_ring_bytes();
    size_t ring_window = round_up_page(header->buffer_size);

    std::string posix_name = name;
    if (posix_name.empty() || posix_name[0] != '/')
      posix_name.insert(posix_name.begin(), '/');

    int fd = shm_open(posix_name.c_str(), O_RDWR | O_CLOEXEC, 0600);
    if (fd == -1)
      throw std::runtime_error(std::string("shm_open: ") + strerror(errno));

    SlotHeader* slot_headers   = nullptr;
    uint8_t*    payload_region = nullptr;
    void*       mirror_base    = nullptr;
    bool ok = setup_mappings(fd, page_size, hdr_ring_sz, ring_window,
                             &slot_headers, &payload_region, &mirror_base);
    close(fd);
    if (!ok)
      throw std::runtime_error("setup_mappings failed");

    NPRPC_LOG_INFO(
        "Opened ring buffer '{}': payload={} bytes, payload mirror at {}",
        name, header->buffer_size, static_cast<void*>(payload_region));

    return std::unique_ptr<LockFreeRingBuffer>(new LockFreeRingBuffer(
        name, std::move(shm), header, slot_headers, payload_region,
        mirror_base, ring_window, false));

  } catch (const boost::interprocess::interprocess_exception& e) {
    NPRPC_LOG_ERROR("Failed to open ring buffer '{}': {}", name, e.what());
    throw;
  }
}

void LockFreeRingBuffer::remove(const std::string& name)
{
  boost::interprocess::shared_memory_object::remove(name.c_str());
  NPRPC_LOG_INFO("Removed ring buffer '{}'", name);
}

LockFreeRingBuffer::LockFreeRingBuffer(
    const std::string& name,
    boost::interprocess::managed_shared_memory&& shm,
    RingBufferHeader* header,
    SlotHeader*       slot_headers,
    uint8_t*          payload_region,
    void*             mirror_base,
    size_t            ring_window,
    bool              is_creator)
    : name_(name)
    , shm_(std::move(shm))
    , header_(header)
    , slot_headers_(slot_headers)
    , payload_region_(payload_region)
    , mirror_base_(mirror_base)
    , ring_window_(ring_window)
    , is_creator_(is_creator)
{
}

LockFreeRingBuffer::~LockFreeRingBuffer()
{
  if (slot_headers_)
    munmap(slot_headers_, header_ring_bytes());

  if (mirror_base_)
    munmap(mirror_base_, 2 * ring_window_);

  if (is_creator_) {
    auto ok = boost::interprocess::shared_memory_object::remove(name_.c_str());
    if (!ok)
      NPRPC_LOG_WARN("Failed to remove shared memory for ring buffer '{}'", name_);
    else
      NPRPC_LOG_INFO("LockFreeRingBuffer destroyed and removed: '{}'", name_);
  }
}

size_t LockFreeRingBuffer::used_payload_bytes() const
{
  uint64_t w = cursor_payload(header_->write_cursor.load(std::memory_order_acquire));
  uint64_t r = cursor_payload(header_->read_cursor.load(std::memory_order_acquire));
  return static_cast<size_t>((w - r + header_->buffer_size) % header_->buffer_size);
}

bool LockFreeRingBuffer::claim_slot_and_payload(size_t size,
                                                uint16_t& slot_out,
                                                uint64_t& payload_off_out)
{
  const uint64_t B = header_->buffer_size;
  uint64_t old_cursor = header_->write_cursor.load(std::memory_order_relaxed);
  for (;;) {
    // Consumer cursor snapshot.  It may be stale, but stale only
    // under-reports free space, so the checks stay conservative.
    uint64_t rd = header_->read_cursor.load(std::memory_order_acquire);

    uint16_t in_flight =
        static_cast<uint16_t>(cursor_slot(old_cursor) - cursor_slot(rd));
    if (in_flight >= kRingSlots)
      return false;

    uint64_t w_pay = cursor_payload(old_cursor);
    uint64_t used  = (w_pay - cursor_payload(rd) + B) % B;
    if (used + size + 1 > B)
      return false;

    // One CAS claims the slot and the payload bytes together: the checks
    // above are atomic with the claim, and payload offsets are handed out
    // in slot order.
    uint64_t new_cursor =
        pack_cursor(static_cast<uint16_t>(cursor_slot(old_cursor) + 1),
                    (w_pay + size) % B);
    if (header_->write_cursor.compare_exchange_weak(
            old_cursor, new_cursor,
            std::memory_order_acq_rel, std::memory_order_relaxed)) {
      slot_out        = cursor_slot(old_cursor);
      payload_off_out = w_pay;
      return true;
    }
    // CAS failure reloaded old_cursor; re-check capacity against it.
  }
}

bool LockFreeRingBuffer::try_write(const void* data, size_t size)
{
  if (size == 0 || size > header_->max_message_size) {
    NPRPC_LOG_ERROR("Message size {} out of range (max {})", size, header_->max_message_size);
    return false;
  }

  uint16_t slot;
  uint64_t payload_off;
  if (!claim_slot_and_payload(size, slot, payload_off))
    return false;

  // --- Fill slot header ---
  SlotHeader& sh = slot_headers_[slot % kRingSlots];
  sh.actual_size.store(0, std::memory_order_release);
  sh.claimed_size = static_cast<uint32_t>(size);
  sh.payload_off  = payload_off;

  // Copy data (mirrored: single memcpy safe across wrap boundary).
  std::memcpy(payload_region_ + payload_off, static_cast<const uint8_t*>(data), size);

  // Commit: consumer's spin signal.
  sh.actual_size.store(static_cast<uint32_t>(size), std::memory_order_release);

  if (header_->waiting_readers.load(std::memory_order_seq_cst) > 0) {
    boost::interprocess::scoped_lock<boost::interprocess::interprocess_mutex>
        lock(header_->mutex);
    header_->data_available.notify_one();
  }

  return true;
}

size_t LockFreeRingBuffer::try_read(void* buffer, size_t buffer_size)
{
  for (;;) {
    uint64_t rd = header_->read_cursor.load(std::memory_order_relaxed);
    uint64_t wr = header_->write_cursor.load(std::memory_order_acquire);
    if (cursor_slot(rd) == cursor_slot(wr))
      return 0;

    SlotHeader& sh = slot_headers_[cursor_slot(rd) % kRingSlots];

    // Bounded wait for the producer's commit signal.
    uint32_t actual_size = sh.actual_size.load(std::memory_order_acquire);
    for (int i = 0; actual_size == 0 && i < kCommitSpinYields; ++i) {
      std::this_thread::yield();
      actual_size = sh.actual_size.load(std::memory_order_acquire);
    }
    if (actual_size == 0)
      return 0; // not committed yet; cursor unchanged, next call retries

    uint32_t claimed_size = sh.claimed_size;
    uint64_t payload_off  = sh.payload_off;

    // Validate everything read from shared memory before using it — a buggy
    // or hostile peer controls these fields.
    if (claimed_size == 0 || claimed_size > header_->max_message_size ||
        payload_off >= header_->buffer_size) {
      NPRPC_LOG_ERROR("Corrupt slot header: claimed_size={}, payload_off={}",
                      claimed_size, payload_off);
      return 0;
    }

    if (actual_size == kSlotSkipped) {
      // Aborted write: release the slot and its payload bytes, try the next.
      sh.actual_size.store(0, std::memory_order_release);
      header_->read_cursor.store(
          pack_cursor(static_cast<uint16_t>(cursor_slot(rd) + 1),
                      (payload_off + claimed_size) % header_->buffer_size),
          std::memory_order_release);
      continue;
    }

    if (actual_size > claimed_size) {
      NPRPC_LOG_ERROR("Corrupt actual_size {} > claimed_size {}",
                      actual_size, claimed_size);
      return 0;
    }
    if (actual_size > buffer_size) {
      NPRPC_LOG_ERROR("Buffer too small: message={}, buffer={}", actual_size, buffer_size);
      return 0;
    }

    std::memcpy(static_cast<uint8_t*>(buffer),
                payload_region_ + payload_off, actual_size);

    sh.actual_size.store(0, std::memory_order_release);

    header_->read_cursor.store(
        pack_cursor(static_cast<uint16_t>(cursor_slot(rd) + 1),
                    (payload_off + claimed_size) % header_->buffer_size),
        std::memory_order_release);

    return actual_size;
  }
}

size_t LockFreeRingBuffer::read_with_timeout(void* buffer,
                                             size_t buffer_size,
                                             std::chrono::milliseconds timeout)
{
  size_t bytes = try_read(buffer, buffer_size);
  if (bytes > 0)
    return bytes;

  boost::interprocess::scoped_lock<boost::interprocess::interprocess_mutex>
      lock(header_->mutex);

  auto deadline = boost::posix_time::microsec_clock::universal_time() +
                  boost::posix_time::milliseconds(timeout.count());

  header_->waiting_readers.fetch_add(1, std::memory_order_seq_cst);
  while (is_empty()) {
    auto now = boost::posix_time::microsec_clock::universal_time();
    if (now >= deadline) {
      header_->waiting_readers.fetch_sub(1, std::memory_order_relaxed);
      return 0;
    }
    if (!header_->data_available.timed_wait(lock, deadline)) {
      header_->waiting_readers.fetch_sub(1, std::memory_order_relaxed);
      return 0;
    }
  }
  header_->waiting_readers.fetch_sub(1, std::memory_order_relaxed);

  lock.unlock();
  return try_read(buffer, buffer_size);
}

size_t LockFreeRingBuffer::available_bytes() const
{
  size_t used = used_payload_bytes();
  return header_->buffer_size - used - 1;
}

bool LockFreeRingBuffer::is_empty() const
{
  uint64_t rd = header_->read_cursor.load(std::memory_order_relaxed);
  uint64_t wr = header_->write_cursor.load(std::memory_order_acquire);
  return cursor_slot(rd) == cursor_slot(wr);
}

bool LockFreeRingBuffer::is_full(size_t message_size) const
{
  return message_size + 1 > available_bytes();
}

//------------------------------------------------------------------------------
// Zero-copy API
//------------------------------------------------------------------------------

LockFreeRingBuffer::WriteReservation
LockFreeRingBuffer::try_reserve_write(size_t min_size)
{
  if (min_size == 0 || min_size > header_->max_message_size) {
    NPRPC_LOG_ERROR("Requested size {} out of range (max {})", min_size,
                    header_->max_message_size);
    return {};
  }

  uint16_t slot;
  uint64_t payload_off;
  if (!claim_slot_and_payload(min_size, slot, payload_off))
    return {};

  // --- Initialise slot header ---
  SlotHeader& sh = slot_headers_[slot % kRingSlots];
  sh.actual_size.store(0, std::memory_order_release);
  sh.claimed_size = static_cast<uint32_t>(min_size);
  sh.payload_off  = payload_off;

  WriteReservation result;
  result.slot_idx = slot;
  result.data     = payload_region_ + payload_off;
  result.max_size = min_size;
  result.valid    = true;
  return result;
}

void LockFreeRingBuffer::commit_write(const WriteReservation& reservation,
                                      size_t actual_size)
{
  if (!reservation.valid) {
    NPRPC_LOG_ERROR("commit_write: invalid reservation");
    return;
  }
  if (actual_size == 0) {
    // 0 is the "uncommitted" sentinel — storing it would leave the consumer
    // waiting on this slot forever.  Treat as an aborted write instead.
    NPRPC_LOG_WARN("commit_write: actual_size 0, aborting reservation");
    abort_write(reservation);
    return;
  }
  if (actual_size > reservation.max_size) {
    // Committing more than was reserved would let the consumer read past the
    // claimed payload bytes; abort rather than leave the slot uncommitted.
    NPRPC_LOG_ERROR("commit_write: actual_size {} exceeds reserved {}, aborting",
                    actual_size, reservation.max_size);
    abort_write(reservation);
    return;
  }

  SlotHeader& sh = slot_headers_[reservation.slot_idx % kRingSlots];
  sh.actual_size.store(static_cast<uint32_t>(actual_size),
                       std::memory_order_release);

  if (header_->waiting_readers.load(std::memory_order_seq_cst) > 0) {
    boost::interprocess::scoped_lock<boost::interprocess::interprocess_mutex>
        lock(header_->mutex);
    header_->data_available.notify_one();
  }
}

void LockFreeRingBuffer::abort_write(const WriteReservation& reservation)
{
  if (!reservation.valid) {
    NPRPC_LOG_ERROR("abort_write: invalid reservation");
    return;
  }

  SlotHeader& sh = slot_headers_[reservation.slot_idx % kRingSlots];
  sh.actual_size.store(kSlotSkipped, std::memory_order_release);

  // Wake a sleeping reader so it can release the slot promptly.
  if (header_->waiting_readers.load(std::memory_order_seq_cst) > 0) {
    boost::interprocess::scoped_lock<boost::interprocess::interprocess_mutex>
        lock(header_->mutex);
    header_->data_available.notify_one();
  }
}

LockFreeRingBuffer::ReadView LockFreeRingBuffer::try_read_view()
{
  for (;;) {
    uint64_t rd = header_->read_cursor.load(std::memory_order_relaxed);
    uint64_t wr = header_->write_cursor.load(std::memory_order_acquire);

    if (cursor_slot(rd) == cursor_slot(wr))
      return {};

    SlotHeader& sh = slot_headers_[cursor_slot(rd) % kRingSlots];

    // Bounded wait for the producer's commit signal.
    uint32_t actual_size = sh.actual_size.load(std::memory_order_acquire);
    for (int i = 0; actual_size == 0 && i < kCommitSpinYields; ++i) {
      std::this_thread::yield();
      actual_size = sh.actual_size.load(std::memory_order_acquire);
    }
    if (actual_size == 0)
      return {}; // not committed yet; cursor unchanged, next call retries

    uint32_t claimed_size = sh.claimed_size;
    uint64_t payload_off  = sh.payload_off;

    // Validate everything read from shared memory before using it — a buggy
    // or hostile peer controls these fields.
    if (claimed_size == 0 || claimed_size > header_->max_message_size ||
        payload_off >= header_->buffer_size) {
      NPRPC_LOG_ERROR("Corrupt slot header in read_view: claimed_size={}, payload_off={}",
                      claimed_size, payload_off);
      return {};
    }

    if (actual_size == kSlotSkipped) {
      // Aborted write: release the slot and its payload bytes, try the next.
      sh.actual_size.store(0, std::memory_order_release);
      header_->read_cursor.store(
          pack_cursor(static_cast<uint16_t>(cursor_slot(rd) + 1),
                      (payload_off + claimed_size) % header_->buffer_size),
          std::memory_order_release);
      continue;
    }

    if (actual_size > claimed_size) {
      NPRPC_LOG_ERROR("actual_size {} > claimed_size {} in read_view",
                      actual_size, claimed_size);
      return {};
    }

    ReadView result;
    result.data     = payload_region_ + payload_off;
    result.size     = actual_size;
    result.slot_idx = cursor_slot(rd);
    result.valid    = true;
    return result;
  }
}

void LockFreeRingBuffer::commit_read(const ReadView& view)
{
  if (!view.valid) {
    NPRPC_LOG_ERROR("commit_read: invalid view");
    return;
  }

  SlotHeader& sh = slot_headers_[view.slot_idx % kRingSlots];

  // Zero actual_size (release) before advancing the cursor.  Producers that
  // acquire-load read_cursor will subsequently observe actual_size == 0
  // when they reuse this slot index.
  sh.actual_size.store(0, std::memory_order_release);

  uint32_t claimed_size = sh.claimed_size;
  uint64_t payload_off  = sh.payload_off;

  header_->read_cursor.store(
      pack_cursor(static_cast<uint16_t>(view.slot_idx + 1),
                  (payload_off + claimed_size) % header_->buffer_size),
      std::memory_order_release);
}


void LockFreeRingBuffer::wait_for_readable(std::chrono::milliseconds timeout)
{
  // Spin phase: yield-based, ~1-5 µs on a modern CPU under light contention.
  // This covers the common case where a burst of packets is in flight.
  for (int i = 0; i < 500 && is_empty(); ++i)
    std::this_thread::yield();

  if (!is_empty())
    return;

  // Sleep phase: fall back to condvar.
  // seq_cst on fetch_add pairs with seq_cst on the producer's load:
  // either the producer sees waiting_readers > 0 and signals us, or it
  // advanced write_cursor (acq_rel CAS) before our is_empty() recheck and
  // we see data without sleeping at all — no lost wakeup is possible.
  boost::interprocess::scoped_lock<boost::interprocess::interprocess_mutex>
      lock(header_->mutex);

  header_->waiting_readers.fetch_add(1, std::memory_order_seq_cst);
  if (is_empty()) {
    auto deadline = boost::posix_time::microsec_clock::universal_time() +
                    boost::posix_time::milliseconds(timeout.count());
    header_->data_available.timed_wait(lock, deadline);
  }
  header_->waiting_readers.fetch_sub(1, std::memory_order_relaxed);
}

} // namespace nprpc::impl

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

size_t LockFreeRingBuffer::calculate_shm_size(size_t buffer_size)
{
  // Memory layout must ensure data region starts at page-aligned offset
  // to avoid double-mapping the header (which contains pthread mutex).
  //
  // Layout:
  // [0 ... ~2KB)        - Boost managed_shared_memory bookkeeping
  // [~2KB ... ~2.5KB)   - Header object with name string
  // [~2.5KB ... 4KB)    - Padding to reach page boundary
  // [4KB ... 4KB+size)  - Data region (page-aligned start)
  //
  // We reserve one full page for overhead to guarantee data starts on page
  // boundary
  size_t page_size = get_page_size();
  size_t overhead = page_size; // One page for header + bookkeeping
  return overhead + buffer_size;
}

std::unique_ptr<LockFreeRingBuffer>
LockFreeRingBuffer::create(const std::string& name, size_t buffer_size)
{
  try {
    size_t page_size = get_page_size();

    // Calculate total size: one page for header/bookkeeping + buffer
    // Data will start exactly at page_size offset
    size_t total_size = page_size + buffer_size;

    // 1) Create managed shared memory (Boost)
    boost::interprocess::managed_shared_memory shm(
        boost::interprocess::create_only, name.c_str(), total_size);

    // 2) Construct header (Boost will place it within the first page)
    auto* header = shm.construct<RingBufferHeader>("header")(buffer_size,
                                                             MAX_MESSAGE_SIZE);
    if (!header)
      throw std::runtime_error("Failed to construct RingBufferHeader");

    // 3) Verify header fits within first page
    uint8_t* shm_base = static_cast<uint8_t*>(shm.get_address());
    size_t header_offset = reinterpret_cast<uint8_t*>(header) - shm_base;
    size_t header_end = header_offset + sizeof(RingBufferHeader);

    if (header_end > page_size) {
      throw std::runtime_error(
          "Header doesn't fit in first page - increase page reservation");
    }

    // 4) Data region starts exactly at page_size offset (page-aligned)
    // We don't use Boost's construct because we need precise control over
    // placement
    uint8_t* data_region = shm_base + page_size;

    // Store data offset in header for open() to find it
    // (We're repurposing unused space or adding a field if needed)
    // Actually, we can calculate it: data always starts at page_size offset

    // 5) Ring window is exactly buffer_size (rounded up to page)
    size_t ring_window = (buffer_size + page_size - 1) & ~(page_size - 1);

    // Reserve 2x ring window for mirrored mapping
    void* reserved = mmap(nullptr, 2 * ring_window, PROT_NONE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (reserved == MAP_FAILED) {
      throw std::runtime_error(
          "Failed to reserve virtual address space for mirrored mapping");
    }

    // 6) Open the POSIX shm object to get an fd
    std::string posix_name = name;
    if (posix_name.empty() || posix_name[0] != '/')
      posix_name.insert(posix_name.begin(), '/');

    int fd = shm_open(posix_name.c_str(), O_RDWR | O_CLOEXEC, 0600);
    if (fd == -1) {
      munmap(reserved, 2 * ring_window);
      throw std::runtime_error(std::string("Failed to open shared memory: ") +
                               strerror(errno));
    }

    // 7) Map only the DATA region (starting from page_size offset), not the
    // header!
    void* first_map = mmap(reserved, ring_window, PROT_READ | PROT_WRITE,
                           MAP_SHARED | MAP_FIXED, fd, page_size);
    if (first_map == MAP_FAILED || first_map != reserved) {
      close(fd);
      munmap(reserved, 2 * ring_window);
      throw std::runtime_error(
          std::string("Failed to create first memory mapping: ") +
          strerror(errno));
    }

    void* second_map_addr = static_cast<uint8_t*>(reserved) + ring_window;
    void* second_map =
        mmap(second_map_addr, ring_window, PROT_READ | PROT_WRITE,
             MAP_SHARED | MAP_FIXED, fd, page_size);
    if (second_map == MAP_FAILED || second_map != second_map_addr) {
      munmap(first_map, ring_window);
      close(fd);
      munmap(reserved, 2 * ring_window);
      throw std::runtime_error(
          std::string("Failed to create mirrored memory mapping: ") +
          strerror(errno));
    }

    close(fd);

    // 8) Mirrored data region pointer - points to start of reserved area
    uint8_t* mirrored_data_region = static_cast<uint8_t*>(reserved);

    NPRPC_LOG_INFO("Created ring buffer '{}': {} bytes with mirrored "
                   "mapping at {} "
                   "(ring_window={})",
                   name, buffer_size, static_cast<void*>(mirrored_data_region),
                   ring_window);

    return std::unique_ptr<LockFreeRingBuffer>(new LockFreeRingBuffer(
        name, std::move(shm), header, mirrored_data_region, reserved,
        ring_window, true));

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

    // Find header
    auto result = shm.find<RingBufferHeader>("header");
    if (result.first == nullptr)
      throw std::runtime_error("Header not found in shared memory");
    RingBufferHeader* header = result.first;

    size_t buffer_size = header->buffer_size;
    size_t page_size = get_page_size();

    // Data region starts at page_size offset (page-aligned, same as in
    // create()) We don't use Boost's find for data - we know exactly where
    // it is

    // Ring window is buffer_size rounded up to page boundary
    size_t ring_window = (buffer_size + page_size - 1) & ~(page_size - 1);

    // Reserve virtual address space for mirrored data region
    void* reserved = mmap(nullptr, 2 * ring_window, PROT_NONE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (reserved == MAP_FAILED)
      throw std::runtime_error(
          "Failed to reserve virtual address space for mirrored mapping");

    // Normalize POSIX name
    std::string posix_name = name;
    if (posix_name.empty() || posix_name[0] != '/')
      posix_name.insert(posix_name.begin(), '/');

    int fd = shm_open(posix_name.c_str(), O_RDWR | O_CLOEXEC, 0600);
    if (fd == -1) {
      munmap(reserved, 2 * ring_window);
      throw std::runtime_error(std::string("Failed to open shared memory: ") +
                               strerror(errno));
    }

    // Map only the DATA region (starting from page_size offset), not the
    // header!
    void* first_map = mmap(reserved, ring_window, PROT_READ | PROT_WRITE,
                           MAP_SHARED | MAP_FIXED, fd, page_size);
    if (first_map == MAP_FAILED || first_map != reserved) {
      close(fd);
      munmap(reserved, 2 * ring_window);
      throw std::runtime_error(
          std::string("Failed to create first memory mapping: ") +
          strerror(errno));
    }

    void* second_map_addr = static_cast<uint8_t*>(reserved) + ring_window;
    void* second_map =
        mmap(second_map_addr, ring_window, PROT_READ | PROT_WRITE,
             MAP_SHARED | MAP_FIXED, fd, page_size);
    if (second_map == MAP_FAILED || second_map != second_map_addr) {
      munmap(first_map, ring_window);
      close(fd);
      munmap(reserved, 2 * ring_window);
      throw std::runtime_error(
          std::string("Failed to create mirrored memory mapping: ") +
          strerror(errno));
    }

    close(fd);

    // Mirrored data region pointer - points to start of reserved area
    uint8_t* mirrored_data_region = static_cast<uint8_t*>(reserved);

    NPRPC_LOG_INFO(
        "Opened ring buffer '{}': {} bytes with mirrored mapping at {} "
        "(ring_window={})",
        name, buffer_size, static_cast<void*>(mirrored_data_region),
        ring_window);

    return std::unique_ptr<LockFreeRingBuffer>(new LockFreeRingBuffer(
        name, std::move(shm), header, mirrored_data_region, reserved,
        ring_window, false));

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
    uint8_t* data_region,
    void* mirror_base,
    size_t ring_window,
    bool is_creator)
    : name_(name)
    , shm_(std::move(shm))
    , header_(header)
    , data_region_(data_region)
    , mirror_base_(mirror_base)
    , ring_window_(ring_window)
    , is_creator_(is_creator)
{
}

LockFreeRingBuffer::~LockFreeRingBuffer()
{
  // Unmap the mirrored virtual memory (2x ring_window_)
  if (mirror_base_) {
    munmap(mirror_base_, 2 * ring_window_);
  }

  // Cleanup is automatic - managed_shared_memory destructor handles it
  // But we should remove the shared memory object if we created it
  if (is_creator_) {
    auto ok = boost::interprocess::shared_memory_object::remove(name_.c_str());
    if (!ok) {
      NPRPC_LOG_WARN(
          "Warning: Failed to remove shared memory for ring buffer '{}'",
          name_);
    } else {
      NPRPC_LOG_INFO("LockFreeRingBuffer destroyed and removed: '{}'", name_);
    }
  }
}

size_t LockFreeRingBuffer::used_bytes() const
{
  // Use write_idx (claimed frontier) vs read_idx.
  // Unsigned subtraction wraps correctly because both indices are always
  // kept in [0, buffer_size) and write_idx >= read_idx modulo buffer_size.
  size_t w = header_->write_idx.load(std::memory_order_acquire);
  size_t r = header_->read_idx.load(std::memory_order_acquire);
  return (w - r + header_->buffer_size) % header_->buffer_size;
}

bool LockFreeRingBuffer::try_write(const void* data, size_t size)
{
  if (size > header_->max_message_size) {
    NPRPC_LOG_ERROR("Message size {} exceeds maximum {}", size,
                    header_->max_message_size);
    return false;
  }

  // Slot layout: [claimed_size u32][actual_size u32][data bytes]
  static constexpr size_t kHdr = sizeof(uint32_t) * 2;
  size_t total = kHdr + size;

  // CAS-claim the slot (same loop as try_reserve_write).
  size_t old_idx, new_idx;
  do {
    old_idx = header_->write_idx.load(std::memory_order_relaxed);
    size_t r = header_->read_idx.load(std::memory_order_acquire);
    size_t used = (old_idx - r + header_->buffer_size) % header_->buffer_size;
    if (used + total + 1 > header_->buffer_size)
      return false; // Buffer full
    new_idx = (old_idx + total) % header_->buffer_size;
  } while (!header_->write_idx.compare_exchange_weak(
               old_idx, new_idx,
               std::memory_order_acq_rel,
               std::memory_order_relaxed));

  // Write claimed_size (navigation header for consumer)
  uint32_t claimed32 = static_cast<uint32_t>(size);
  std::memcpy(data_region_ + old_idx, &claimed32, sizeof(uint32_t));

  // Write data (mirrored mapping: single memcpy always safe)
  size_t data_start = (old_idx + kHdr) % header_->buffer_size;
  std::memcpy(data_region_ + data_start,
              static_cast<const uint8_t*>(data), size);

  // Commit: store actual_size with release — this is the consumer's signal.
  auto* actual_slot = reinterpret_cast<std::atomic<uint32_t>*>(
      data_region_ + (old_idx + sizeof(uint32_t)) % header_->buffer_size);
  actual_slot->store(claimed32, std::memory_order_release);

  // Notify waiting readers
  {
    boost::interprocess::scoped_lock<boost::interprocess::interprocess_mutex>
        lock(header_->mutex);
    header_->data_available.notify_one();
  }

  return true;
}

size_t LockFreeRingBuffer::try_read(void* buffer, size_t buffer_size)
{
  static constexpr size_t kHdr = sizeof(uint32_t) * 2;

  size_t commit_idx = header_->commit_idx.load(std::memory_order_relaxed);
  size_t write_idx  = header_->write_idx.load(std::memory_order_acquire);

  if (commit_idx == write_idx)
    return 0; // Empty

  uint32_t claimed_size = 0;
  std::memcpy(&claimed_size, data_region_ + commit_idx, sizeof(uint32_t));

  if (claimed_size == 0 || claimed_size > header_->max_message_size) {
    NPRPC_LOG_ERROR("Corrupt claimed_size: {}", claimed_size);
    return 0;
  }

  // Spin until committed
  auto* actual_slot = reinterpret_cast<std::atomic<uint32_t>*>(
      data_region_ +
      (commit_idx + sizeof(uint32_t)) % header_->buffer_size);
  uint32_t actual_size;
  while ((actual_size = actual_slot->load(std::memory_order_acquire)) == 0)
    std::this_thread::yield();

  if (actual_size > buffer_size) {
    NPRPC_LOG_ERROR("Buffer too small: message={}, buffer={}",
                    actual_size, buffer_size);
    return 0;
  }

  size_t data_start = (commit_idx + kHdr) % header_->buffer_size;
  std::memcpy(static_cast<uint8_t*>(buffer),
              data_region_ + data_start, actual_size);

  size_t next = (commit_idx + kHdr + claimed_size) % header_->buffer_size;
  header_->commit_idx.store(next, std::memory_order_relaxed);
  header_->read_idx.store(next, std::memory_order_release);

  return actual_size;
}

size_t LockFreeRingBuffer::read_with_timeout(void* buffer,
                                             size_t buffer_size,
                                             std::chrono::milliseconds timeout)
{
  // First try non-blocking read
  size_t bytes = try_read(buffer, buffer_size);
  if (bytes > 0) {
    return bytes;
  }

  // Wait for data with timeout
  boost::interprocess::scoped_lock<boost::interprocess::interprocess_mutex>
      lock(header_->mutex);

  // Calculate absolute deadline in boost posix_time
  auto deadline = boost::posix_time::microsec_clock::universal_time() +
                  boost::posix_time::milliseconds(timeout.count());

  while (is_empty()) {
    auto now = boost::posix_time::microsec_clock::universal_time();
    if (now >= deadline) {
      return 0; // Timeout
    }

    // Wait with absolute time
    if (!header_->data_available.timed_wait(lock, deadline)) {
      return 0; // Timeout
    }
  }

  // Release lock and try read again
  lock.unlock();
  return try_read(buffer, buffer_size);
}

size_t LockFreeRingBuffer::available_bytes() const
{
  size_t used = used_bytes();
  // Keep 1 byte reserved to distinguish full from empty
  return header_->buffer_size - used - 1;
}

bool LockFreeRingBuffer::is_empty() const
{
  size_t commit_idx = header_->commit_idx.load(std::memory_order_relaxed);
  size_t write_idx  = header_->write_idx.load(std::memory_order_acquire);
  return commit_idx == write_idx;
}

bool LockFreeRingBuffer::is_full(size_t message_size) const
{
  size_t total_bytes = sizeof(uint32_t) + message_size;
  return total_bytes + 1 > available_bytes();
}

//------------------------------------------------------------------------------
// Zero-copy API implementations
//------------------------------------------------------------------------------

LockFreeRingBuffer::WriteReservation
LockFreeRingBuffer::try_reserve_write(size_t min_size)
{
  // Slot layout: [claimed_size u32][actual_size u32][data bytes]
  // claimed_size is written at claim time (= min_size); it lets the consumer
  // navigate to the next slot boundary without waiting for commit_write.
  // actual_size is zeroed at claim time; commit_write stores the real size
  // with release semantics — this is the consumer's spin-wait signal.
  //
  // We claim exactly min_size bytes of data.  Claiming all available space
  // would make the ring appear full to other producers until commit_read is
  // called, causing false "buffer full" failures in pipelined scenarios where
  // the next write arrives before commit_read runs.
  //
  // ABA note: the CAS below is not protected by a generation counter.
  // ABA requires other producers to lap the entire ring between this thread's
  // load and its CAS — extremely unlikely with a 16 MB ring.
  static constexpr size_t kHdr = sizeof(uint32_t) * 2;
  size_t total = kHdr + min_size;

  size_t old_idx, new_idx;
  do {
    old_idx = header_->write_idx.load(std::memory_order_relaxed);
    size_t r = header_->read_idx.load(std::memory_order_acquire);
    size_t used = (old_idx - r + header_->buffer_size) % header_->buffer_size;
    // Keep 1 byte gap to distinguish full from empty.
    if (used + total + 1 > header_->buffer_size)
      return {}; // Not enough space
    new_idx = (old_idx + total) % header_->buffer_size;
  } while (!header_->write_idx.compare_exchange_weak(
               old_idx, new_idx,
               std::memory_order_acq_rel,
               std::memory_order_relaxed));

  // Slot [old_idx, old_idx + total) is now exclusively ours.
  // Write claimed_size immediately so the consumer can find the next slot.
  uint32_t claimed32 = static_cast<uint32_t>(min_size);
  std::memcpy(data_region_ + old_idx, &claimed32, sizeof(uint32_t));
  // Zero actual_size — consumer spins on this until commit_write fires.
  uint32_t zero = 0;
  std::memcpy(data_region_ + (old_idx + sizeof(uint32_t)) % header_->buffer_size,
              &zero, sizeof(uint32_t));

  WriteReservation result;
  result.write_idx = old_idx;
  result.data      = data_region_ + (old_idx + kHdr) % header_->buffer_size;
  result.max_size  = min_size;
  result.valid     = true;
  return result;
}

void LockFreeRingBuffer::commit_write(const WriteReservation& reservation,
                                      size_t actual_size)
{
  if (!reservation.valid) {
    NPRPC_LOG_ERROR("LockFreeRingBuffer::commit_write: invalid reservation");
    return;
  }

  if (actual_size > reservation.max_size) {
    NPRPC_LOG_ERROR("LockFreeRingBuffer::commit_write: actual_size {} "
                    "exceeds reserved {}",
                    actual_size, reservation.max_size);
    return;
  }

  // Write actual_size into the slot header with release semantics.
  // This is the only commit signal the consumer waits on; write_idx was
  // already advanced during try_reserve_write so we must NOT touch it here.
  auto* actual_slot = reinterpret_cast<std::atomic<uint32_t>*>(
      data_region_ +
      (reservation.write_idx + sizeof(uint32_t)) % header_->buffer_size);
  actual_slot->store(static_cast<uint32_t>(actual_size),
                     std::memory_order_release);

  // Notify waiting readers
  {
    boost::interprocess::scoped_lock<boost::interprocess::interprocess_mutex>
        lock(header_->mutex);
    header_->data_available.notify_one();
  }
}

LockFreeRingBuffer::ReadView LockFreeRingBuffer::try_read_view()
{
  static constexpr size_t kHdr = sizeof(uint32_t) * 2;

  // commit_idx is the consumer's scan cursor (single consumer — no CAS).
  size_t commit_idx = header_->commit_idx.load(std::memory_order_relaxed);
  size_t write_idx  = header_->write_idx.load(std::memory_order_acquire);

  if (commit_idx == write_idx)
    return {}; // Nothing claimed yet

  // claimed_size tells us how large this slot is so we can find the next one
  // even before actual_size is committed.
  uint32_t claimed_size = 0;
  std::memcpy(&claimed_size,
              data_region_ + commit_idx, sizeof(uint32_t));

  if (claimed_size == 0 || claimed_size > header_->max_message_size) {
    NPRPC_LOG_ERROR("Corrupt claimed_size in read_view: {}", claimed_size);
    return {};
  }

  // Spin until the producer commits by storing a non-zero actual_size.
  // This is a bounded spin: producers write actual_size immediately after
  // filling the data bytes, so the window is at most one memcpy wide.
  auto* actual_slot = reinterpret_cast<std::atomic<uint32_t>*>(
      data_region_ +
      (commit_idx + sizeof(uint32_t)) % header_->buffer_size);
  uint32_t actual_size;
  while ((actual_size = actual_slot->load(std::memory_order_acquire)) == 0)
    std::this_thread::yield();

  if (actual_size > claimed_size) {
    NPRPC_LOG_ERROR("actual_size {} > claimed_size {} in read_view",
                    actual_size, claimed_size);
    return {};
  }

  size_t data_start = (commit_idx + kHdr) % header_->buffer_size;
  // Advance commit_idx to the next slot (uses the full claimed slot size).
  size_t next_commit = (commit_idx + kHdr + claimed_size) % header_->buffer_size;
  header_->commit_idx.store(next_commit, std::memory_order_relaxed);

  ReadView result;
  result.data      = data_region_ + data_start;
  result.size      = actual_size;
  result.read_idx  = next_commit;
  result.valid     = true;
  return result;
}

void LockFreeRingBuffer::commit_read(const ReadView& view)
{
  if (!view.valid) {
    NPRPC_LOG_ERROR("LockFreeRingBuffer::commit_read: invalid view");
    return;
  }

  // size_t old_read = header_->read_idx.load(std::memory_order_acquire);
  // size_t write = header_->write_idx.load(std::memory_order_acquire);

  // std::cout << "[D] commit_read: old=" << old_read << " new=" <<
  // view.read_idx
  //           << " write=" << write << std::endl;

  // Update read index with release semantics
  header_->read_idx.store(view.read_idx, std::memory_order_release);
}

} // namespace nprpc::impl

// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <atomic>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/sync/interprocess_condition.hpp>
#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include <chrono>
#include <cstdint>
#include <cstring>

namespace nprpc::impl {

// Lock-free ring buffer for multiple producers, single consumer (MPSC).
// Uses memory-mapped shared memory with a mirrored payload mapping for
// true zero-copy IPC.
//
// Two-ring design:
//
// SHM layout:
// +-----------------------------------------+
// | RingBufferHeader  (page 0, metadata)    |
// +-----------------------------------------+
// | SlotHeader[N]     (fixed-size, no mirror)|  <- header ring
// +-----------------------------------------+
// | Payload ring × 2  (mirrored mapping)    |  <- variable-length data
// +-----------------------------------------+
//
// SlotHeader lives at a stable virtual address: headers[seq % N].
// actual_size is always at the same offset within SlotHeader, so consumer
// can zero it reliably regardless of payload size — the root correctness
// problem of the previous single-ring design.
//
// Cursors pack both ring positions into a single 64-bit word:
// [63:48] slot counter (mod 2^16), [47:0] payload byte offset (mod
// buffer_size).  Packing makes the slot claim, the payload claim and both
// capacity checks one atomic CAS, which is what makes multiple concurrent
// producers safe:
//  - the capacity check and the claim are atomic, so racing producers
//    cannot collectively overcommit the payload ring;
//  - payload offsets are assigned in slot order, so the consumer's cursor
//    update (payload_off + claimed_size of the slot it just consumed) is
//    always the exact start of the next live message's payload.
//
// Producer protocol (try_write / try_reserve_write + commit_write):
//   1. CAS write_cursor → claims header slot s = slot(old) % N and
//      claimed_size payload bytes at payload(old).  Fails (ring full) if
//      either the slot ring or the payload ring lacks capacity relative to
//      an acquire-loaded snapshot of read_cursor; a stale snapshot only
//      under-reports free space, never over-reports it.
//   2. Zero headers[s].actual_size (release) — stable address, always correct.
//   3. Store headers[s].claimed_size, headers[s].payload_off.
//   4. Fill data at payload_region + payload_off.
//   5. Release-store headers[s].actual_size != 0 — commit signal.
//
// Consumer protocol (try_read_view):
//   1. Spin on headers[slot(read_cursor) % N].actual_size != 0.
//   2. Read claimed_size / payload_off from header.
//   3. Return ReadView pointing into payload ring.
//   4. commit_read() zeros actual_size (release), then release-stores
//      read_cursor = {slot+1, (payload_off + claimed_size) % buffer_size}.
//
// Synchronization:
// - write_cursor: claimed via CAS (acq_rel).
// - actual_size: zero at init, release-zeroed by consumer, release-set by
//   producer.
// - read_cursor: single-consumer, release store so producers' acquire load
//   sees both the zeroed actual_size and the consumer's finished payload
//   reads before the space is reused.

// Number of header slots.  Must be a power of two.
static constexpr uint32_t kRingSlots = 1024;

// The slot counter lives in 16 bits and wraps mod 2^16; slot(counter) %
// kRingSlots is only stable across that wrap if kRingSlots divides 2^16.
static_assert((kRingSlots & (kRingSlots - 1)) == 0,
              "kRingSlots must be a power of two");
static_assert(kRingSlots <= (1u << 16),
              "slot counter is 16 bits; kRingSlots must divide 2^16");

// Packed-cursor helpers.
inline constexpr uint32_t kCursorSlotShift   = 48;
inline constexpr uint64_t kCursorPayloadMask = (uint64_t(1) << kCursorSlotShift) - 1;

inline constexpr uint16_t cursor_slot(uint64_t c)
{
  return static_cast<uint16_t>(c >> kCursorSlotShift);
}
inline constexpr uint64_t cursor_payload(uint64_t c)
{
  return c & kCursorPayloadMask;
}
inline constexpr uint64_t pack_cursor(uint16_t slot, uint64_t payload)
{
  return (static_cast<uint64_t>(slot) << kCursorSlotShift) | payload;
}

// Per-slot metadata stored in the fixed-size header ring.
// Each instance lives at headers[seq % kRingSlots] — a stable address
// independent of payload size.
struct alignas(16) SlotHeader {
  std::atomic<uint32_t> actual_size{0};  // 0 = empty; commit signal
  uint32_t              claimed_size{0}; // payload bytes reserved
  uint64_t              payload_off{0};  // byte offset into payload ring
};
static_assert(sizeof(SlotHeader) == 16, "SlotHeader must be 16 bytes");

struct alignas(64) RingBufferHeader {
  // Packed producer cursor: {slot counter, payload offset} (see pack_cursor).
  // Claimed by producers with a single CAS so capacity check + claim are
  // atomic and payload offsets are assigned in slot order.
  alignas(64) std::atomic<uint64_t> write_cursor{0};

  // Packed consumer cursor: advanced only by the single consumer.  One
  // atomic word so producers always see a consistent (slot, payload)
  // snapshot for their capacity checks.
  alignas(64) std::atomic<uint64_t> read_cursor{0};

  // Fixed at creation
  size_t   buffer_size;        // Payload ring size in bytes
  uint32_t max_message_size;   // Maximum single message size

  // For blocking reads
  boost::interprocess::interprocess_mutex   mutex;
  boost::interprocess::interprocess_condition data_available;
  // Number of threads currently sleeping in read_with_timeout().
  // Producers check this before acquiring the mutex: if 0, skip the notify.
  std::atomic<uint32_t> waiting_readers{0};

  RingBufferHeader(size_t buf_size, uint32_t max_msg_sz)
      : buffer_size(buf_size)
      , max_message_size(max_msg_sz)
  {
  }
};

class LockFreeRingBuffer
{
public:
  // Configuration for continuous circular buffer (variable-sized messages)
  static constexpr size_t DEFAULT_BUFFER_SIZE = 16 * 1024 * 1024;
  static constexpr uint32_t MAX_MESSAGE_SIZE = 12 * 1024 * 1024;

  // Create new ring buffer in shared memory
  static std::unique_ptr<LockFreeRingBuffer>
  create(const std::string& name, size_t buffer_size = DEFAULT_BUFFER_SIZE, size_t max_message_size = MAX_MESSAGE_SIZE);

  // Open existing ring buffer
  static std::unique_ptr<LockFreeRingBuffer> open(const std::string& name);

  // Remove shared memory region (call when destroying channel)
  static void remove(const std::string& name);

  ~LockFreeRingBuffer();

  // Non-blocking write
  // Returns true if message was written, false if buffer full
  bool try_write(const void* data, size_t size);

  // Non-blocking read
  // Returns number of bytes read (0 if empty)
  size_t try_read(void* buffer, size_t buffer_size);

  // Blocking read with timeout
  // Returns number of bytes read (0 on timeout)
  size_t read_with_timeout(void* buffer,
                           size_t buffer_size,
                           std::chrono::milliseconds timeout);

  // Block until the ring is non-empty or timeout expires.
  // Spins briefly (yield-based) before falling asleep via the condvar so the
  // fast path (data arrives quickly) pays zero syscall cost.
  // Safe to call from a spinning consumer thread to eliminate the nanosleep
  // pattern while remaining correct for cross-process signaling.
  void wait_for_readable(std::chrono::milliseconds timeout = std::chrono::milliseconds(10));

  //--------------------------------------------------------------------------
  // Zero-copy API for direct buffer access
  //--------------------------------------------------------------------------

  struct WriteReservation {
    uint8_t* data;       // Pointer to write data (into payload ring)
    size_t   max_size;   // Maximum bytes that can be written (== reserved size)
    uint64_t slot_idx;   // Internal: slot counter claimed from write_cursor
    bool     valid;      // true if reservation succeeded

    explicit operator bool() const { return valid; }
  };

  struct ReadView {
    const uint8_t* data;     // Pointer to message data (into payload ring)
    size_t         size;     // Message size in bytes
    uint64_t       slot_idx; // Internal: slot counter from read_cursor
    bool           valid;    // true if read succeeded

    explicit operator bool() const { return valid; }
  };

  // Reserve space for writing a message (zero-copy write)
  // Call commit_write() after writing to complete the operation
  // @param min_size Minimum size you need (will fail if not available)
  // @return WriteReservation with pointer and max_size (full available
  // space), or invalid if no space
  WriteReservation try_reserve_write(size_t min_size);

  // Commit a reserved write with the actual size written
  // Must be called after try_reserve_write() with the actual bytes written
  void commit_write(const WriteReservation& reservation, size_t actual_size);

  // Get a read view into the ring buffer (zero-copy read)
  // Call commit_read() after processing to advance read pointer
  // @return ReadView with data pointer, or invalid if empty
  ReadView try_read_view();

  // Commit a read, advancing the read pointer
  void commit_read(const ReadView& view);

  // Statistics
  size_t buffer_size() const { return header_->buffer_size; }
  size_t available_bytes() const;
  bool is_empty() const;
  bool is_full(size_t message_size) const;

  // Access to header for synchronization (used by SharedMemoryChannel)
  RingBufferHeader* header() { return header_; }

private:
  LockFreeRingBuffer(const std::string& name,
                     boost::interprocess::managed_shared_memory&& shm,
                     RingBufferHeader* header,
                     SlotHeader*       slot_headers,
                     uint8_t*          payload_region,
                     void*             mirror_base,
                     size_t            ring_window,
                     bool              is_creator);

  // Calculate total shared memory size needed
  static size_t calculate_shm_size(size_t buffer_size);

  // Atomically claim one header slot and `size` payload bytes with a single
  // CAS on write_cursor.  Returns false if either ring is full.
  bool claim_slot_and_payload(size_t size,
                              uint16_t& slot_out,
                              uint64_t& payload_off_out);

  // Helper to calculate used payload bytes
  size_t used_payload_bytes() const;

  std::string name_;
  boost::interprocess::managed_shared_memory shm_;
  RingBufferHeader* header_;
  SlotHeader*       slot_headers_; // Fixed-size header ring (kRingSlots entries)
  uint8_t*          payload_region_; // Points to start of mirrored payload region
  void*             mirror_base_;    // Base address for munmap
  size_t            ring_window_;    // Size of each mapped window (page-aligned)
  bool              is_creator_;     // Should we remove shm on destruction?
};

// Helper: Generate unique names for shared memory regions
inline std::string make_shm_name(const std::string& channel_id,
                                 const std::string& direction)
{
  return "/nprpc_" + channel_id + "_" + direction;
}

} // namespace nprpc::impl

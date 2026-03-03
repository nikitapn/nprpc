// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <sys/mman.h>

namespace nprpc::impl {

/**
 * @brief Thread-local bump allocator for sync RPC request serialization.
 *
 * Each sync proxy call resets the arena at entry, then serializes all input
 * arguments into a contiguous slab without any malloc/realloc.  The key win
 * for deeply-nested structs is try_extend(): when a flat::Vector or
 * flat::String grows the flat_buffer, the arena can extend the existing
 * allocation in-place (the tip cursor just moves forward), so no memcpy of
 * the already-written header/struct data occurs.
 *
 * Lifetime contract (sync calls only):
 *   1. arena.reset()          -- call entry, cursor back to 0
 *   2. flat_buffer uses arena  -- serializes request
 *   3. send_receive(buf)       -- blocks; by return, transport has consumed
 *                                 the bytes from the slab
 *   4. buf receives response   -- flat_buffer falls back to heap for receive
 *                                 (arena_backed_ cleared inside grow())
 *   5. next call → goto 1
 *
 * Async calls must NOT use this arena because the flat_buffer is moved into
 * the io_context and the calling thread can reset the arena immediately.
 *
 * The slab is mmap(MAP_ANONYMOUS|MAP_PRIVATE).  reset() calls madvise
 * MADV_DONTNEED so the OS reclaims physical pages that were touched; they
 * will be faulted in on demand in the next serialization cycle, keeping
 * RSS low when the arena is idle.
 */
class BumpArena
{
  static constexpr size_t kSlabSize    = 256 * 1024; // 256 KB, tunable
  static constexpr size_t kAlignment   = 8;           // covers all IDL primitives

  uint8_t* slab_     = nullptr;
  size_t   cursor_   = 0;
  size_t   capacity_ = 0;

  void ensure_init() noexcept
  {
    if (slab_) return;
    // MAP_ANONYMOUS pages are zero-filled; we only fault what we touch.
    slab_ = static_cast<uint8_t*>(
        ::mmap(nullptr, kSlabSize, PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    if (slab_ == MAP_FAILED) {
      slab_     = nullptr;
      capacity_ = 0;
    } else {
      capacity_ = kSlabSize;
    }
  }

public:
  ~BumpArena()
  {
    if (slab_) {
      ::munmap(slab_, capacity_);
      slab_ = nullptr;
    }
  }

  // Non-copyable / non-movable (thread_local has fixed address)
  BumpArena(const BumpArena&)            = delete;
  BumpArena& operator=(const BumpArena&) = delete;

  BumpArena() = default;

  /**
   * Allocate `n` bytes (8-byte aligned).
   * Returns nullptr if the slab is exhausted (caller falls back to malloc).
   */
  uint8_t* alloc(size_t n) noexcept
  {
    ensure_init();
    if (!slab_) return nullptr;
    size_t aligned = (n + kAlignment - 1) & ~(kAlignment - 1);
    if (cursor_ + aligned > capacity_) return nullptr;
    uint8_t* p = slab_ + cursor_;
    cursor_ += aligned;
    return p;
  }

  /**
   * Try to extend the most-recently-returned allocation in-place.
   *
   * @param ptr      The pointer returned by the last alloc() call.
   * @param old_cap  The capacity that was allocated at that time.
   * @param new_cap  The desired new (larger) capacity.
   * @returns true if the extension succeeded (no copy needed).
   *
   * The extension can only succeed when `ptr` is still at the tip of the
   * arena (nothing else has been allocated since).
   */
  bool try_extend(const uint8_t* ptr, size_t old_cap, size_t new_cap) noexcept
  {
    if (!slab_ || !ptr) return false;

    size_t old_aligned = (old_cap + kAlignment - 1) & ~(kAlignment - 1);
    size_t new_aligned = (new_cap + kAlignment - 1) & ~(kAlignment - 1);

    // The allocation must still be at the tip.
    if (ptr + old_aligned != slab_ + cursor_) return false;
    // Must fit in the slab.
    if ((ptr - slab_) + new_aligned > capacity_) return false;

    cursor_ = static_cast<size_t>(ptr - slab_) + new_aligned;
    return true;
  }

  /**
   * Reset the arena for a new call.
   * madvise(MADV_DONTNEED) tells the kernel it may reclaim physical pages,
   * keeping RSS low between calls without an munmap/mmap round-trip.
   */
  void reset() noexcept
  {
    if (slab_ && cursor_ > 0) {
      ::madvise(slab_, cursor_, MADV_DONTNEED);
      cursor_ = 0;
    }
  }

  // True once the slab has been successfully mapped.
  bool available() const noexcept { return slab_ != nullptr; }
};

/**
 * Per-thread bump arena.  Lazily initialised on first access.
 * Lives for the duration of the thread; never freed until thread exit.
 */
inline BumpArena& tls_bump_arena() noexcept
{
  thread_local BumpArena arena;
  return arena;
}

} // namespace nprpc::impl

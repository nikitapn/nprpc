// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <atomic>
#include <mutex>

namespace nprpc::impl {

class AdaptiveSpinMutex
{
  std::atomic_flag spinlock_ = ATOMIC_FLAG_INIT;
  std::mutex fallback_mutex_;
  static constexpr int MAX_SPINS = 40; // Tune this based on profiling

public:
  void lock()
  {
    // Try spinning first
    for (int i = 0; i < MAX_SPINS; ++i) {
      if (!spinlock_.test_and_set(std::memory_order_acquire)) {
        return; // Got it without syscall!
      }
// Pause instruction to avoid memory barrier storms
#if defined(__x86_64__) || defined(__i386__)
      __builtin_ia32_pause();
#elif defined(__aarch64__)
      asm volatile("yield" ::: "memory");
#endif
    }

    // Spinning failed, fall back to mutex (syscall)
    spinlock_.clear(std::memory_order_release);
    fallback_mutex_.lock();
  }

  void unlock()
  {
    if (fallback_mutex_.try_lock()) {
      // We're in fallback mode
      fallback_mutex_.unlock();
    } else {
      // We're in spin mode
      spinlock_.clear(std::memory_order_release);
    }
  }
};

class SpinMutex
{
  std::atomic_flag flag_ = ATOMIC_FLAG_INIT;

public:
  void lock()
  {
    while (flag_.test_and_set(std::memory_order_acquire)) {
#if defined(__x86_64__) || defined(__i386__)
      __builtin_ia32_pause();
#endif
    }
  }

  void unlock() { flag_.clear(std::memory_order_release); }
};

} // namespace nprpc::impl
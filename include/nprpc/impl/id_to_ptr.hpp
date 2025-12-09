// Copyright (c) 2021-2025 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by
// LICENSING file in the topmost directory

#pragma once

#include <atomic>
#include <cassert>

namespace nprpc::impl {

// A lock-free ID to pointer mapping structure
template <typename T> class IdToPtr
{
  struct Item {
    uint32_t next;
    std::atomic<uint32_t> gix; // Make atomic for proper visibility
    T val;
  };

  struct Val {
    union {
      uint64_t val;
      struct {
        uint32_t idx;
        uint32_t cnt;
      };
    };
  };

  const uint32_t max_size_;
  std::atomic<Val> tail_ix_;

  using Items = std::aligned_storage_t<sizeof(Item), alignof(Item)>*;
  Items items_;

  constexpr static uint32_t index(uint64_t id) noexcept
  {
    return id & 0xFFFF'FFFFull;
  }

  constexpr static uint32_t generation_index(uint64_t id) noexcept
  {
    return (id >> 32) & 0xFFFF'FFFFul;
  }

  constexpr Item& data(size_t ix) noexcept
  {
    return *std::launder(&reinterpret_cast<Item*>(items_)[ix]);
  }

public:
  uint64_t add(const T& val) noexcept
  {
    Val old_free, new_free;
    old_free = tail_ix_.load(std::memory_order_relaxed);
    for (;;) {
      if (old_free.idx == max_size_) [[unlikely]]
        return (uint64_t)(-1);
      new_free.idx = data(old_free.idx).next;
      new_free.cnt = old_free.cnt + 1;
      // Use acq_rel: acquire to read next pointer, release to publish the
      // claimed slot
      if (tail_ix_.compare_exchange_weak(old_free, new_free,
                                         std::memory_order_acq_rel,
                                         std::memory_order_relaxed))
        break;
    }

    const uint64_t idx = old_free.idx;
    const uint64_t gix = data(idx).gix.load(std::memory_order_relaxed);

    data(idx).val = val;

    // Ensure val write is visible before returning ID to caller
    std::atomic_thread_fence(std::memory_order_release);

    return (gix << 32) | idx;
  }

  void remove(uint64_t id) noexcept
  {
    const uint32_t idx = index(id);
    auto& to_be_removed = data(idx);

    to_be_removed.gix.fetch_add(1, std::memory_order_relaxed);

    Val new_free;
    new_free.val = 0ull;
    new_free.idx = idx;
    Val old_free = tail_ix_.load(std::memory_order_relaxed);
    do {
      to_be_removed.next = old_free.idx;
      new_free.cnt = old_free.cnt + 1;
    } while (!tail_ix_.compare_exchange_weak(old_free, new_free,
                                             std::memory_order_relaxed,
                                             std::memory_order_relaxed));
  }

  std::remove_pointer_t<T>* get(uint64_t id) noexcept
  {
    const auto idx = index(id);
    if (idx >= max_size_)
      return nullptr;

    // Acquire fence to ensure we see writes from add()
    std::atomic_thread_fence(std::memory_order_acquire);

    auto& item = data(idx);
    if (item.gix.load(std::memory_order_relaxed) != generation_index(id))
      return nullptr;

    if constexpr (std::is_pointer_v<T> == false) {
      return &item.val;
    } else {
      return item.val;
    }
  }

  void init() noexcept
  {
    for (uint32_t i = 0; i < max_size_; ++i) {
      data(i).gix.store(0, std::memory_order_relaxed);
      data(i).next = i + 1;
      if constexpr (std::is_pointer_v<T>) {
        data(i).val = nullptr;
      }
    }
    tail_ix_ = Val{0};
  }

  IdToPtr(uint32_t max_size) noexcept : max_size_{max_size}
  {
    assert(max_size_ > 0);
    items_ = reinterpret_cast<Items>(new char[max_size * sizeof(Item)]);
    init();
  }
};

} // namespace nprpc::impl
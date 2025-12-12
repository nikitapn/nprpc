// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include <nprpc/flat_buffer.hpp>
#include <nprpc/impl/lock_free_ring_buffer.hpp>

namespace nprpc {

NPRPC_API void flat_buffer::commit_read_if_needed()
{
  if (!has_read_view_)
    return;

  // Cast back to LockFreeRingBuffer and call commit_read
  auto* ring = static_cast<impl::LockFreeRingBuffer*>(ring_buffer_);
  if (ring) {
    // Reconstruct ReadView
    impl::LockFreeRingBuffer::ReadView view;
    view.data = view_base_;
    view.size = view_size_;
    view.read_idx = read_view_read_idx_;
    view.valid = true;

    ring->commit_read(view);
  }

  // Clear the tracking
  ring_buffer_ = nullptr;
  read_view_read_idx_ = 0;
  has_read_view_ = false;
}

} // namespace nprpc

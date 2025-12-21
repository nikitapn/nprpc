// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <boost/asio/buffer.hpp>

#include <nprpc/export.hpp>

namespace nprpc {

// Forward declaration - actual EndPoint is in endpoint.hpp
class EndPoint;
class flat_buffer;

namespace impl {
// Forward declaration of RPC interface for zero-copy buffer growth
class RpcImpl;
extern RpcImpl* g_rpc;

// Forward declaration - will request new shared memory buffer
bool prepare_zero_copy_buffer_grow(const EndPoint& endpoint,
                                   flat_buffer& buffer,
                                   size_t new_size,
                                   const void* existing_data,
                                   size_t existing_size);
} // namespace impl

/**
 * @brief A flat buffer that can operate in two modes:
 *        1. Owned mode: manages its own heap-allocated memory (like
 * boost::beast::flat_buffer)
 *        2. View mode: references external memory (e.g., shared memory ring
 * buffer)
 *
 * In view mode, the buffer provides a zero-copy view into shared memory.
 * The interface is compatible with boost::beast::flat_buffer for seamless
 * integration with existing marshalling code.
 *
 * Memory Layout (owned mode):
 * +------------------+------------------+------------------+
 * |   [consumed]     |    [readable]    |   [writable]     |
 * +------------------+------------------+------------------+
 * ^                  ^                  ^                  ^
 * buffer_           in_                out_               capacity_
 *
 * Memory Layout (view mode):
 * +------------------+------------------+
 * |    [readable]    |   [extendable]   |
 * +------------------+------------------+
 * ^                  ^                  ^
 * view_base_        view_base_+size_   view_base_+max_size_
 */

// Forward declaration for ADL
// class flat_buffer;

// Free function declaration for Boost.Beast read_size ADL lookup
// Must be declared before any Beast headers that might instantiate
// has_read_size_helper std::size_t read_size_helper(flat_buffer& buffer,
// std::size_t max_size) noexcept;

class flat_buffer
{
public:
  using const_buffers_type = boost::asio::const_buffer;
  using mutable_buffers_type = boost::asio::mutable_buffer;

private:
  // Owned mode storage
  std::uint8_t* buffer_ = nullptr; // Base allocation pointer
  std::size_t in_ = 0;             // Read position (offset from buffer_)
  std::size_t out_ = 0;            // Write position (offset from buffer_)
  std::size_t capacity_ = 0;       // Total allocated capacity

  // View mode storage
  std::uint8_t* view_base_ = nullptr; // External memory base (nullptr if owned)
  std::size_t view_size_ = 0;         // Current data size in view
  std::size_t view_max_size_ = 0;     // Maximum size available in view

  // Optional endpoint for shared memory buffer growth/fallback
  const EndPoint* endpoint_ = nullptr;

  // For shared memory zero-copy writes: stores the write_idx from
  // WriteReservation Used to reconstruct the reservation for commit_write()
  std::size_t reservation_write_idx_ = 0;
  bool has_reservation_ = false;

  // For shared memory zero-copy reads: stores the ReadView for commit_read()
  // When set, flat_buffer will automatically commit the read when needed
  void* ring_buffer_ =
      nullptr; // LockFreeRingBuffer* (opaque to avoid circular dependency)
  std::size_t read_view_read_idx_ = 0;
  bool has_read_view_ = false;

  static constexpr std::size_t default_growth_factor = 2;
  static constexpr std::size_t min_allocation = 512;

  bool is_view() const noexcept { return view_base_ != nullptr; }

  /// Convert view mode to owned mode, preserving data
  /// @param additional Additional capacity needed beyond current data
  void reallocate_in_view_mode(std::size_t additional)
  {
    std::size_t current_data_size = view_size_;
    std::size_t new_cap = current_data_size + additional;
    new_cap = std::max(new_cap * default_growth_factor, min_allocation);

    // Save current view state before any modifications
    std::uint8_t* old_view_base = view_base_;
    const EndPoint* old_endpoint = endpoint_;

    // Try to get a new larger shared memory buffer first
    if (old_endpoint && impl::g_rpc) {
      // Clear view state so prepare_zero_copy_buffer_grow can set up new
      // view
      view_base_ = nullptr;
      view_size_ = 0;
      view_max_size_ = 0;
      endpoint_ = nullptr;
      has_reservation_ = false;

      if (impl::prepare_zero_copy_buffer_grow(*old_endpoint, *this, new_cap,
                                              old_view_base,
                                              current_data_size)) {
        // Successfully got a new shared memory buffer with data copied
        return;
      }
      // Fall through to heap allocation
    }

    std::uint8_t* new_buf = new std::uint8_t[new_cap];

    // Copy existing data from old view
    if (current_data_size > 0 && old_view_base) {
      std::memcpy(new_buf, old_view_base, current_data_size);
    }

    // Switch to owned mode
    buffer_ = new_buf;
    in_ = 0;
    out_ = current_data_size;
    capacity_ = new_cap;

    // Clear view state (may already be cleared)
    view_base_ = nullptr;
    view_size_ = 0;
    view_max_size_ = 0;
    endpoint_ = nullptr;
    has_reservation_ = false;
  }

  void grow(std::size_t n)
  {
    if (is_view()) {
      // View mode buffer exceeded max_size - gracefully fall back to
      // owned mode
      reallocate_in_view_mode(n);
      return;
    }

    std::size_t const current_size = out_ - in_;
    std::size_t const required = current_size + n;

    // Calculate new capacity
    std::size_t new_cap = std::max(capacity_ * default_growth_factor, required);
    new_cap = std::max(new_cap, min_allocation);

    // Allocate new buffer
    std::uint8_t* new_buf = new std::uint8_t[new_cap];

    // Copy existing data
    if (current_size > 0) {
      std::memcpy(new_buf, buffer_ + in_, current_size);
    }

    // Free old buffer
    delete[] buffer_;

    // Update pointers
    buffer_ = new_buf;
    in_ = 0;
    out_ = current_size;
    capacity_ = new_cap;
  }

public:
  /// Default constructor (owned mode, empty)
  flat_buffer() = default;

  /// Constructor with initial capacity (owned mode)
  explicit flat_buffer(std::size_t initial_capacity)
      : buffer_(new std::uint8_t[initial_capacity])
      , capacity_(initial_capacity)
  {
  }

  /// View constructor - creates a zero-copy view into external memory
  /// @param base Pointer to external memory (e.g., shared memory)
  /// @param size Current readable size
  /// @param max_size Maximum size available for extension (for prepare())
  /// @param endpoint Optional endpoint for fallback buffer growth
  flat_buffer(std::uint8_t* base,
              std::size_t size,
              std::size_t max_size,
              const EndPoint* endpoint = nullptr)
      : view_base_(base)
      , view_size_(size)
      , view_max_size_(max_size)
      , endpoint_(endpoint)
  {
  }

  /// Move constructor
  flat_buffer(flat_buffer&& other) noexcept
      : buffer_(other.buffer_)
      , in_(other.in_)
      , out_(other.out_)
      , capacity_(other.capacity_)
      , view_base_(other.view_base_)
      , view_size_(other.view_size_)
      , view_max_size_(other.view_max_size_)
      , endpoint_(other.endpoint_)
      , reservation_write_idx_(other.reservation_write_idx_)
      , has_reservation_(other.has_reservation_)
  {
    other.buffer_ = nullptr;
    other.in_ = 0;
    other.out_ = 0;
    other.capacity_ = 0;
    other.view_base_ = nullptr;
    other.view_size_ = 0;
    other.view_max_size_ = 0;
    other.endpoint_ = nullptr;
    other.reservation_write_idx_ = 0;
    other.has_reservation_ = false;
  }

  /// Move assignment
  flat_buffer& operator=(flat_buffer&& other) noexcept
  {
    if (this != &other) {
      delete[] buffer_;

      buffer_ = other.buffer_;
      in_ = other.in_;
      out_ = other.out_;
      capacity_ = other.capacity_;
      view_base_ = other.view_base_;
      view_size_ = other.view_size_;
      view_max_size_ = other.view_max_size_;
      endpoint_ = other.endpoint_;
      reservation_write_idx_ = other.reservation_write_idx_;
      has_reservation_ = other.has_reservation_;

      other.buffer_ = nullptr;
      other.in_ = 0;
      other.out_ = 0;
      other.capacity_ = 0;
      other.view_base_ = nullptr;
      other.view_size_ = 0;
      other.view_max_size_ = 0;
      other.endpoint_ = nullptr;
      other.reservation_write_idx_ = 0;
      other.has_reservation_ = false;
    }
    return *this;
  }

  /// Copy constructor (deep copy, always creates owned buffer)
  flat_buffer(const flat_buffer& other)
      : buffer_(other.size() > 0 ? new std::uint8_t[other.size()] : nullptr)
      , in_(0)
      , out_(other.size())
      , capacity_(other.size())
  {
    if (out_ > 0) {
      std::memcpy(buffer_, other.data().data(), out_);
    }
  }

  /// Copy assignment
  flat_buffer& operator=(const flat_buffer& other)
  {
    if (this != &other) {
      flat_buffer tmp(other);
      *this = std::move(tmp);
    }
    return *this;
  }

  /// Destructor
  ~flat_buffer()
  {
    commit_read_if_needed();
    delete[] buffer_; // Safe even if nullptr
                      // view_base_ is not owned, don't delete
  }

  //--------------------------------------------------------------------------
  // boost::beast::flat_buffer compatible interface
  //--------------------------------------------------------------------------

  /// Returns the size of the readable area
  std::size_t size() const noexcept
  {
    if (is_view()) {
      return view_size_;
    }
    return out_ - in_;
  }

  /// Returns the maximum possible size
  std::size_t max_size() const noexcept
  {
    if (is_view()) {
      return view_max_size_;
    }
    return static_cast<std::size_t>(-1); // Essentially unlimited for owned
  }

  /// Returns the current capacity
  std::size_t capacity() const noexcept
  {
    if (is_view()) {
      return view_max_size_;
    }
    return capacity_ - in_;
  }

  /// Returns a const buffer representing the readable data
  const_buffers_type data() const noexcept
  {
    if (is_view()) {
      return {view_base_, view_size_};
    }
    return {buffer_ + in_, out_ - in_};
  }

  /// Returns a mutable buffer representing the writable area
  mutable_buffers_type data() noexcept
  {
    if (is_view()) {
      return {view_base_, view_size_};
    }
    return {buffer_ + in_, out_ - in_};
  }

  /// Returns a const buffer representing the readable data (alias for
  /// compatibility)
  const_buffers_type cdata() const noexcept { return data(); }

  /// Prepare writable area of specified size
  /// @return Mutable buffer for writing
  /// @note In view mode, if the requested size exceeds max_size, the buffer
  ///       will gracefully convert to owned mode (copying existing data)
  mutable_buffers_type prepare(std::size_t n)
  {
    if (is_view()) {
      // In view mode, we can extend up to max_size
      if (view_size_ + n > view_max_size_) {
        // Graceful fallback: convert to owned buffer and grow
        grow(n);
        // After grow(), we're in owned mode - fall through to owned
        // logic
      } else {
        return {view_base_ + view_size_, n};
      }
    }

    // Owned mode
    if (out_ + n > capacity_) {
      grow(n);
    }
    return {buffer_ + out_, n};
  }

  /// Commit written bytes to readable area
  void commit(std::size_t n) noexcept
  {
    if (is_view()) {
      view_size_ = std::min(view_size_ + n, view_max_size_);
    } else {
      out_ = std::min(out_ + n, capacity_);
    }
  }

  /// Consume bytes from the readable area
  void consume(std::size_t n) noexcept
  {
    if (is_view()) {
      // In view mode, consuming shifts the view
      // This is used to "reset" the buffer
      if (n >= view_size_) {
        view_size_ = 0;
      } else {
        // Note: for simplicity, we don't shift view_base_
        // This matches how it's typically used (consume all before
        // reuse)
        view_size_ -= n;
        view_base_ += n;
        view_max_size_ -= n;
      }
    } else {
      in_ = std::min(in_ + n, out_);
      if (in_ == out_) {
        // Buffer empty, reset positions
        in_ = 0;
        out_ = 0;
      }
    }
  }

  /// Clear all data
  void clear() noexcept
  {
    if (is_view()) {
      view_size_ = 0;
    } else {
      in_ = 0;
      out_ = 0;
    }
  }

  //--------------------------------------------------------------------------
  // View mode specific methods
  //--------------------------------------------------------------------------

  /// Check if buffer is in view mode
  bool is_view_mode() const noexcept { return is_view(); }

  /// Create a view into shared memory
  /// @param base Pointer to shared memory region
  /// @param size Current data size
  /// @param max_size Maximum writable size
  /// @param endpoint Optional endpoint for fallback buffer growth
  /// @param write_idx Optional write index for WriteReservation tracking
  void set_view(std::uint8_t* base,
                std::size_t size,
                std::size_t max_size,
                const EndPoint* endpoint = nullptr,
                std::size_t write_idx = 0,
                bool has_reservation = false)
  {
    // Release owned memory if any
    delete[] buffer_;
    buffer_ = nullptr;
    in_ = 0;
    out_ = 0;
    capacity_ = 0;

    // Set view
    view_base_ = base;
    view_size_ = size;
    view_max_size_ = max_size;
    endpoint_ = endpoint;
    reservation_write_idx_ = write_idx;
    has_reservation_ = has_reservation;
  }

  /// Create a view from a ReadView (for zero-copy response reading)
  /// @param read_view The ReadView from ring buffer
  /// @param ring_buffer Pointer to the LockFreeRingBuffer (for commit_read)
  void set_view_from_read(const void* data,
                          std::size_t size,
                          void* ring_buffer,
                          std::size_t read_idx)
  {
    // Release owned memory if any
    delete[] buffer_;
    buffer_ = nullptr;
    in_ = 0;
    out_ = 0;
    capacity_ = 0;

    // Set view (read-only, so size == max_size)
    view_base_ =
        const_cast<std::uint8_t*>(static_cast<const std::uint8_t*>(data));
    view_size_ = size;
    view_max_size_ = size; // Can't extend a read view
    endpoint_ = nullptr;
    reservation_write_idx_ = 0;
    has_reservation_ = false;

    // Track ReadView for commit_read
    ring_buffer_ = ring_buffer;
    read_view_read_idx_ = read_idx;
    has_read_view_ = true;
  }

  /// Commit the read if this buffer is a view from a ReadView
  /// Call this after unmarshaling is complete to free ring buffer space
  NPRPC_API void commit_read_if_needed();

  /// Check if this buffer has a pending read to commit
  bool has_pending_read() const noexcept { return has_read_view_; }

  /// Convert from view mode to owned mode (makes a copy)
  void detach_view()
  {
    if (!is_view())
      return;

    std::size_t sz = view_size_;
    std::uint8_t* old_base = view_base_;

    // Clear view
    view_base_ = nullptr;
    view_size_ = 0;
    view_max_size_ = 0;
    endpoint_ = nullptr;
    reservation_write_idx_ = 0;
    has_reservation_ = false;

    // Allocate and copy
    if (sz > 0) {
      buffer_ = new std::uint8_t[sz];
      std::memcpy(buffer_, old_base, sz);
      capacity_ = sz;
      in_ = 0;
      out_ = sz;
    }
  }

  /// Get raw pointer to data (for compatibility with existing code)
  std::uint8_t* data_ptr() noexcept
  {
    if (is_view()) {
      return view_base_;
    }
    return buffer_ + in_;
  }

  const std::uint8_t* data_ptr() const noexcept
  {
    if (is_view()) {
      return view_base_;
    }
    return buffer_ + in_;
  }

  /// Get the endpoint associated with this buffer (for view mode)
  const EndPoint* endpoint() const noexcept { return endpoint_; }

  //--------------------------------------------------------------------------
  // Zero-copy write reservation accessors
  //--------------------------------------------------------------------------

  /// Check if this buffer has a pending write reservation
  bool has_write_reservation() const noexcept { return has_reservation_; }

  /// Get the write_idx for reconstructing WriteReservation
  std::size_t reservation_write_idx() const noexcept
  {
    return reservation_write_idx_;
  }

  /// Clear the write reservation (after commit or abandon)
  void clear_reservation() noexcept
  {
    has_reservation_ = false;
    reservation_write_idx_ = 0;
  }

  /// @brief  Swap two flat_buffer instances
  /// @param a First buffer
  /// @param b Second buffer
  static void swap(flat_buffer& a, flat_buffer& b) noexcept
  {
    using std::swap;
    swap(a.buffer_, b.buffer_);
    swap(a.in_, b.in_);
    swap(a.out_, b.out_);
    swap(a.capacity_, b.capacity_);
    swap(a.view_base_, b.view_base_);
    swap(a.view_size_, b.view_size_);
    swap(a.view_max_size_, b.view_max_size_);
    swap(a.endpoint_, b.endpoint_);
    swap(a.reservation_write_idx_, b.reservation_write_idx_);
    swap(a.has_reservation_, b.has_reservation_);
    swap(a.ring_buffer_, b.ring_buffer_);
    swap(a.read_view_read_idx_, b.read_view_read_idx_);
    swap(a.has_read_view_, b.has_read_view_);
  }

  static constexpr std::size_t default_initial_size() noexcept { return 512; }
};

} // namespace nprpc

#include <boost/beast/core/detail/config.hpp>
#include <boost/beast/core/impl/read_size.hpp>

// Probably a bug in /usr/include/boost/beast/core/detect_ssl.hpp:601:30.
// Error says: "Note: there are 2 candidates but it's refering to read_size()
// overloads inside boost::beast::detail namespace" So we provide an explicit
// specialization for boost::beast::detail::read_size to resolve the ambiguity
// I have no idea why this is needed for custom flat_buffer but not for
// boost::beast::flat_buffer
namespace boost::beast::detail {
template <class DynamicBuffer>
std::size_t read_size(DynamicBuffer& buffer, std::size_t max_size)
{
  return read_size(buffer, max_size, has_read_size_helper<DynamicBuffer>{});
}
} // namespace boost::beast::detail

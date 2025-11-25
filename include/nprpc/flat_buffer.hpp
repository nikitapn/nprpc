// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

#pragma once

#include <boost/asio/buffer.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <algorithm>

namespace nprpc {

/**
 * @brief A flat buffer that can operate in two modes:
 *        1. Owned mode: manages its own heap-allocated memory (like boost::beast::flat_buffer)
 *        2. View mode: references external memory (e.g., shared memory ring buffer)
 * 
 * In view mode, the buffer provides a zero-copy view into shared memory.
 * The interface is compatible with boost::beast::flat_buffer for seamless integration
 * with existing marshalling code.
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
// Must be declared before any Beast headers that might instantiate has_read_size_helper
// std::size_t read_size_helper(flat_buffer& buffer, std::size_t max_size) noexcept;

class flat_buffer {
public:
    using const_buffers_type = boost::asio::const_buffer;
    using mutable_buffers_type = boost::asio::mutable_buffer;

private:
    // Owned mode storage
    char* buffer_ = nullptr;    // Base allocation pointer
    std::size_t in_ = 0;        // Read position (offset from buffer_)  
    std::size_t out_ = 0;       // Write position (offset from buffer_)
    std::size_t capacity_ = 0;  // Total allocated capacity
    
    // View mode storage
    char* view_base_ = nullptr;       // External memory base (nullptr if owned)
    std::size_t view_size_ = 0;       // Current data size in view
    std::size_t view_max_size_ = 0;   // Maximum size available in view
    
    static constexpr std::size_t default_growth_factor = 2;
    static constexpr std::size_t min_allocation = 512;

    bool is_view() const noexcept { return view_base_ != nullptr; }

    void grow(std::size_t n) {
        if (is_view()) {
            throw std::length_error("flat_buffer: cannot grow view mode buffer beyond max_size");
        }
        
        std::size_t const current_size = out_ - in_;
        std::size_t const required = current_size + n;
        
        // Calculate new capacity
        std::size_t new_cap = std::max(capacity_ * default_growth_factor, required);
        new_cap = std::max(new_cap, min_allocation);
        
        // Allocate new buffer
        char* new_buf = new char[new_cap];
        
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
        : buffer_(new char[initial_capacity])
        , capacity_(initial_capacity)
    {}
    
    /// View constructor - creates a zero-copy view into external memory
    /// @param base Pointer to external memory (e.g., shared memory)
    /// @param size Current readable size
    /// @param max_size Maximum size available for extension (for prepare())
    flat_buffer(char* base, std::size_t size, std::size_t max_size)
        : view_base_(base)
        , view_size_(size)
        , view_max_size_(max_size)
    {}
    
    /// Move constructor
    flat_buffer(flat_buffer&& other) noexcept
        : buffer_(other.buffer_)
        , in_(other.in_)
        , out_(other.out_)
        , capacity_(other.capacity_)
        , view_base_(other.view_base_)
        , view_size_(other.view_size_)
        , view_max_size_(other.view_max_size_)
    {
        other.buffer_ = nullptr;
        other.in_ = 0;
        other.out_ = 0;
        other.capacity_ = 0;
        other.view_base_ = nullptr;
        other.view_size_ = 0;
        other.view_max_size_ = 0;
    }
    
    /// Move assignment
    flat_buffer& operator=(flat_buffer&& other) noexcept {
        if (this != &other) {
            delete[] buffer_;
            
            buffer_ = other.buffer_;
            in_ = other.in_;
            out_ = other.out_;
            capacity_ = other.capacity_;
            view_base_ = other.view_base_;
            view_size_ = other.view_size_;
            view_max_size_ = other.view_max_size_;
            
            other.buffer_ = nullptr;
            other.in_ = 0;
            other.out_ = 0;
            other.capacity_ = 0;
            other.view_base_ = nullptr;
            other.view_size_ = 0;
            other.view_max_size_ = 0;
        }
        return *this;
    }
    
    /// Copy constructor (deep copy, always creates owned buffer)
    flat_buffer(const flat_buffer& other)
        : buffer_(other.size() > 0 ? new char[other.size()] : nullptr)
        , in_(0)
        , out_(other.size())
        , capacity_(other.size())
    {
        if (out_ > 0) {
            std::memcpy(buffer_, other.data().data(), out_);
        }
    }
    
    /// Copy assignment
    flat_buffer& operator=(const flat_buffer& other) {
        if (this != &other) {
            flat_buffer tmp(other);
            *this = std::move(tmp);
        }
        return *this;
    }
    
    /// Destructor
    ~flat_buffer() {
        delete[] buffer_;  // Safe even if nullptr
        // view_base_ is not owned, don't delete
    }
    
    //--------------------------------------------------------------------------
    // boost::beast::flat_buffer compatible interface
    //--------------------------------------------------------------------------
    
    /// Returns the size of the readable area
    std::size_t size() const noexcept {
        if (is_view()) {
            return view_size_;
        }
        return out_ - in_;
    }
    
    /// Returns the maximum possible size
    std::size_t max_size() const noexcept {
        if (is_view()) {
            return view_max_size_;
        }
        return static_cast<std::size_t>(-1);  // Essentially unlimited for owned
    }
    
    /// Returns the current capacity
    std::size_t capacity() const noexcept {
        if (is_view()) {
            return view_max_size_;
        }
        return capacity_ - in_;
    }
    
    /// Returns a const buffer representing the readable data
    const_buffers_type data() const noexcept {
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
    
    /// Returns a const buffer representing the readable data (alias for compatibility)
    const_buffers_type cdata() const noexcept {
        return data();
    }

    /// Prepare writable area of specified size
    /// @return Mutable buffer for writing
    mutable_buffers_type prepare(std::size_t n) {
        if (is_view()) {
            // In view mode, we can extend up to max_size
            if (view_size_ + n > view_max_size_) {
                throw std::length_error("flat_buffer: prepare would exceed view max_size");
            }
            return {view_base_ + view_size_, n};
        }
        
        // Owned mode
        if (out_ + n > capacity_) {
            grow(n);
        }
        return {buffer_ + out_, n};
    }
    
    /// Commit written bytes to readable area
    void commit(std::size_t n) noexcept {
        if (is_view()) {
            view_size_ = std::min(view_size_ + n, view_max_size_);
        } else {
            out_ = std::min(out_ + n, capacity_);
        }
    }
    
    /// Consume bytes from the readable area
    void consume(std::size_t n) noexcept {
        if (is_view()) {
            // In view mode, consuming shifts the view
            // This is used to "reset" the buffer
            if (n >= view_size_) {
                view_size_ = 0;
            } else {
                // Note: for simplicity, we don't shift view_base_
                // This matches how it's typically used (consume all before reuse)
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
    void clear() noexcept {
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
    void set_view(char* base, std::size_t size, std::size_t max_size) {
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
    }
    
    /// Convert from view mode to owned mode (makes a copy)
    void detach_view() {
        if (!is_view()) return;
        
        std::size_t sz = view_size_;
        char* old_base = view_base_;
        
        // Clear view
        view_base_ = nullptr;
        view_size_ = 0;
        view_max_size_ = 0;
        
        // Allocate and copy
        if (sz > 0) {
            buffer_ = new char[sz];
            std::memcpy(buffer_, old_base, sz);
            capacity_ = sz;
            in_ = 0;
            out_ = sz;
        }
    }
    
    /// Get raw pointer to data (for compatibility with existing code)
    char* data_ptr() noexcept {
        if (is_view()) {
            return view_base_;
        }
        return buffer_ + in_;
    }
    
    const char* data_ptr() const noexcept {
        if (is_view()) {
            return view_base_;
        }
        return buffer_ + in_;
    }
    
    //--------------------------------------------------------------------------
    // Boost.Beast DynamicBuffer support (ADL functions)
    //--------------------------------------------------------------------------
    
    /// Friend declaration for Boost.Beast read_size ADL lookup
    // friend std::size_t read_size_helper(flat_buffer& buffer, std::size_t max_size) noexcept;
};

// Inline definition of read_size_helper for Boost.Beast DynamicBuffer support
// This function is found via ADL when boost::beast::read_size() is called
// inline std::size_t read_size_helper(flat_buffer& buffer, std::size_t max_size) noexcept {
//     auto const current_size = buffer.size();
//     auto const limit = buffer.max_size() - current_size;
//     return std::min<std::size_t>(
//         std::max<std::size_t>(512, buffer.capacity() - current_size),
//         std::min<std::size_t>(max_size, limit));
// }

} // namespace nprpc

// namespace boost::beast::detail {

// inline std::size_t read_size_helper(::nprpc::flat_buffer& buffer, std::size_t max_size) noexcept {
//     auto const current_size = buffer.size();
//     auto const limit = buffer.max_size() - current_size;
//     return std::min<std::size_t>(
//         std::max<std::size_t>(512, buffer.capacity() - current_size),
//         std::min<std::size_t>(max_size, limit));
// }

// } // namespace boost::beast

#include <boost/beast/core/detail/config.hpp>
#include <boost/beast/core/impl/read_size.hpp>

// Probably a bug in /usr/include/boost/beast/core/detect_ssl.hpp:601:30 it says:
// note: there are 2 candidates but it's refering to read_size() overloads inside boost::beast::detail namespace
// So we provide an explicit specialization for boost::beast::detail::read_size to resolve the ambiguity
// I have no idea why this is needed for custom flat_buffer but not for boost::beast::flat_buffer
namespace boost::beast::detail {
template<class DynamicBuffer>
std::size_t
read_size(
    DynamicBuffer& buffer, std::size_t max_size)
{
    return read_size(buffer, max_size,
        has_read_size_helper<DynamicBuffer>{});
}
}


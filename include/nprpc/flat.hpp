// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#ifndef NPRPC_FLAT_HPP_
#define NPRPC_FLAT_HPP_

#include <cassert>
#include <cstddef> // std::byte
#include <cstdint>
#include <memory>
#include <ostream>
#include <string_view>
#include <tuple>
#include <vector>

#include <nprpc/flat_buffer.hpp>

/// Zero-copy views of NPRPC's wire format.
///
/// Generated code hands servants their arguments as views into the receive
/// buffer rather than copies: `Span` for strings and vectors of plain
/// values, `<Message>_Direct` accessors for messages, `Span_ref` for
/// vectors of messages or strings. The views are valid only during the
/// call; copy out anything you keep.
namespace nprpc::flat {

/// A `boolean` as stored on the wire: one byte, 0 or 1.
class Boolean
{
  std::uint8_t value_;

public:
  /// Stores `value`.
  void set(bool value) noexcept { value_ = value ? 0x01 : 0x00; }

  /// The stored value.
  bool get() const noexcept
  {
    assert(value_ == 0x00 || value_ == 0x01);
    return value_ == 0x01 ? true : false;
  }

  /// Stores `value`.
  Boolean& operator=(bool value) noexcept
  {
    set(value);
    return *this;
  }

  /// The stored value.
  operator bool() const noexcept { return get(); }
};

// Wire layout and buffer plumbing: used by generated code, not by callers.
namespace detail {

template <typename T> struct TSpan {
  // Forward iterators for concept compliance
  using iterator = T*;
  using const_iterator = const T*;

  T* const first;
  T* const last;

  T* begin() { return first; }

  T* end() { return last; }

  const T* begin() const { return first; }

  const T* end() const { return last; }

  auto size() const noexcept { return static_cast<uint32_t>(last - first); }

  T& operator[](int i) { return first[i]; }

  const T& operator[](int i) const { return first[i]; }

  void* data() { return (void*)begin(); }

  const void* data() const { return (const void*)begin(); }
};

} // namespace detail

/// A contiguous run of `T` inside a buffer: pointers `first` and `last`,
/// `begin()`/`end()`, `size()`, `operator[]` and `data()`. Does not own the
/// memory.
///
/// `Span<char>` and `Span<const char>` convert to `std::string_view`.
template <typename T> struct Span : public detail::TSpan<T> {
};

/// A writable string in a buffer; converts to `std::string_view` and
/// `std::string`.
template <> struct Span<char> : public detail::TSpan<char> {
  /// Copies the first `size()` characters of `str`, which must be at least
  /// that long; the span is not resized.
  void operator=(const char* str)
  {
    for (char& c : *this)
      c = *str++;
  }
  /// The characters, without a copy.
  operator std::string_view() const noexcept
  {
    return {this->first, this->size()};
  }
  /// The characters, copied.
  operator std::string() const noexcept
  {
    return std::string{this->first, this->size()};
  }
};

/// A read-only string in a buffer.
template <> struct Span<const char> : public detail::TSpan<const char> {
  /// The characters, without a copy.
  operator std::string_view() const noexcept
  {
    return {this->first, this->size()};
  }
};

/// Bytes in a buffer.
template <> struct Span<uint8_t> : public detail::TSpan<uint8_t> {
  /// Copies `str`'s bytes in; the span must be large enough.
  void operator=(std::string_view str)
  {
    std::memcpy(this->data(), str.data(), str.size());
  }
};

/// Prints the characters.
inline std::ostream& operator<<(std::ostream& os, const Span<char>& span)
{
  for (auto& e : span)
    os << e;
  return os;
}

/// A vector of messages or strings inside a buffer. Iterating yields a
/// `TD` accessor (`<Message>_Direct`, `String_Direct1`) for each element.
///
/// ```cpp
/// void Save(nprpc::flat::Span_ref<flat::Post, flat::Post_Direct> posts) override {
///   for (auto post : posts)
///     store(post.slug(), post.title());
/// }
/// ```
template <class T, class TD> struct Span_ref {
  /// The buffer the elements live in.
  flat_buffer& buffer;
  /// Byte offset of the first element within `buffer`.
  const std::uint32_t first; // absolute
  /// Byte offset one past the last element.
  const std::uint32_t last;  // absolute

  /// Iterator; dereferencing gives the element's `TD` accessor.
  struct Ptr_ref {
    /// The buffer the element lives in.
    flat_buffer& buffer;
    /// Byte offset of the element.
    std::uint32_t offset;

    /// Moves to the next element.
    Ptr_ref& operator++()
    {
      offset += sizeof(T);
      return *this;
    }

    /// Moves to the next element, returning the previous position.
    Ptr_ref operator++(int)
    {
      auto tmp = offset;
      offset += sizeof(T);
      return {buffer, tmp};
    }

    /// The element's accessor.
    TD operator*()
    {
      assert(offset % alignof(T) == 0);
      return {buffer, offset};
    }

    /// Same position.
    bool operator==(const Ptr_ref& other) const noexcept
    {
      return offset == other.offset;
    }
    /// Different position.
    bool operator!=(const Ptr_ref& other) const noexcept
    {
      return offset != other.offset;
    }

    /// Positioned at byte offset `o` of `b`.
    Ptr_ref(flat_buffer& b, std::uint32_t o)
        : buffer(b)
        , offset(o)
    {
      assert(offset % alignof(T) == 0);
    }
  };

  /// Address of the first element.
  void* data()
  {
    return (void*)(reinterpret_cast<std::byte*>(buffer.data().data()) + first);
  }

  /// Address one past the last element.
  void* data_end()
  {
    return (void*)(reinterpret_cast<std::byte*>(buffer.data().data()) + last);
  }

  /// First element.
  Ptr_ref begin() { return {buffer, first}; }
  /// One past the last element.
  Ptr_ref end() { return {buffer, last}; }

  /// Element `i`; dereference it for the accessor.
  Ptr_ref operator[](size_t i) { return {buffer, first + static_cast<std::uint32_t>(i * sizeof(T))}; }

  /// Number of elements.
  auto size() const noexcept { return (last - first) / sizeof(T); }

  /// A view of the byte range `range` of `b`; created by generated code.
  Span_ref(flat_buffer& b,
           const std::tuple<std::uint32_t, std::uint32_t>& range)
      : buffer(b)
      , first(std::get<0>(range))
      , last(std::get<1>(range))
  {
    assert(first % alignof(T) == 0);
  }
};

// In-buffer layout of arrays and vectors, and the allocator they share.
namespace detail {

template <typename T, size_t Size> class Array
{
  alignas(T) std::byte storage_[sizeof(T) * Size];

  T* data() noexcept
  {
    return std::launder(reinterpret_cast<T*>(&storage_[0]));
  }

  const T* data() const noexcept
  {
    return std::launder(reinterpret_cast<const T*>(&storage_[0]));
  }

public:
  auto begin() noexcept { return data(); }

  auto end() noexcept { return data() + Size; }

  const auto begin() const noexcept { return data(); }

  const auto end() const noexcept { return data() + Size; }

  operator Span<T>() noexcept { return {begin(), end()}; }
  operator Span<const T>() const noexcept { return {begin(), end()}; }

  std::tuple<std::uint32_t, std::uint32_t> range(void* base_ptr) const noexcept
  {
    auto data_offset_abs = ((std::byte*)this - (std::byte*)base_ptr);
    assert(data_offset_abs % alignof(T) == 0);
    return {static_cast<uint32_t>(data_offset_abs),
            static_cast<uint32_t>(data_offset_abs + Size * sizeof(T))};
  }
};

// Simulate the size growth performed by _alloc / alloc_arm without writing.
// Must stay in lock-step with those routines: same padding rule, and a zero
// nbytes request leaves the cursor unchanged (matches _alloc early-return).
[[nodiscard]] inline std::size_t grow_size(std::size_t cursor,
                                           std::size_t align,
                                           std::size_t nbytes) noexcept
{
  if (!nbytes)
    return cursor;
  const auto rem = cursor % align;
  const auto padding = rem ? align - rem : 0;
  return cursor + padding + nbytes;
}

// returns a new 'this' if the buffer is rellocated and an offset to allocated
// space from that 'this'
template <typename T>
[[nodiscard]] std::tuple<void*, std::uint32_t>
_alloc(void* This, flat_buffer& buffer, uint32_t size)
{
  if (!size)
    return std::make_tuple(This, 0u);

  auto this_ = reinterpret_cast<std::byte*>(This);
  const auto old_base = reinterpret_cast<std::byte*>(buffer.data().data());
  const auto this_offset = this_ - old_base;
  const auto current_size = buffer.data().size();

  auto rem = current_size % alignof(T);
  auto padding = rem ? alignof(T) - rem : 0;

  const auto size_bytes = size * sizeof(T);
  auto ptr = reinterpret_cast<std::byte*>(
                 buffer.prepare(size_bytes + padding).data()) +
             padding;
  buffer.commit(size_bytes + padding);

  if (old_base != buffer.data().data()) {
    this_ = (std::byte*)buffer.data().data() + this_offset;
  }

  return std::make_tuple(static_cast<void*>(this_),
                         (uint32_t)(size_t)(ptr - this_));
}

template <typename T> class Vector
{
  uint32_t offset_; // in bytes from this pointer
  uint32_t size_;   // in elements
protected:
  [[nodiscard]] auto alloc(flat_buffer& buffer, uint32_t size)
  {
    auto [this_, offset] = _alloc<T>(this, buffer, size);
    static_cast<Vector<T>*>(this_)->offset_ = offset;
    static_cast<Vector<T>*>(this_)->size_ = size;
    return static_cast<Vector<T>*>(this_);
  }

public:
  auto size() const noexcept { return size_; }

  auto begin() noexcept
  {
    return (T*)(reinterpret_cast<std::byte*>(this) + offset_);
  }

  auto end() noexcept { return begin() + size_; }

  auto begin() const noexcept
  {
    return (const T*)(reinterpret_cast<const std::byte*>(this) + offset_);
  }

  auto end() const noexcept { return begin() + size_; }

  operator Span<T>() noexcept { return {begin(), end()}; }
  operator Span<const T>() const noexcept { return {begin(), end()}; }

  template <bool UseAssert = true>
  std::tuple<std::uint32_t, std::uint32_t> range(void* base_ptr) const noexcept
  {
    auto data_offset_abs = ((std::byte*)this - (std::byte*)base_ptr) + offset_;

    if constexpr (UseAssert) {
      assert(data_offset_abs % alignof(T) == 0);
    }

    return {static_cast<uint32_t>(data_offset_abs),
            static_cast<uint32_t>(data_offset_abs + size_ * sizeof(T))};
  }

  bool check_size_align(void* base_ptr, uint32_t max_buffer_size) const noexcept
  {
    const auto [a, b] = range<false>(base_ptr);
    return (a % alignof(T) == 0 && b <= max_buffer_size);
  }

  Vector() = default;
  Vector(flat_buffer& buffer, std::uint32_t elements_size)
  {
    [[maybe_unused]] auto this_ = alloc(buffer, elements_size);
  }
};

} // namespace detail

/// Accessor for a `vector<T>` field while building or reading a message.
/// `Vector_Direct1` and `Vector_Direct2` add element access.
template <typename T> class Vector_Direct
{
protected:
  flat_buffer& buffer_;
  std::uint32_t offset_;

  auto& v() noexcept
  {
    return *reinterpret_cast<detail::Vector<T>*>((std::byte*)buffer_.data().data() +
                                         offset_);
  }
  auto& v() const noexcept
  {
    return *reinterpret_cast<const detail::Vector<T>*>(
        (const std::byte*)buffer_.data().data() + offset_);
  }

public:
  /// Number of elements.
  std::uint32_t size() const noexcept { return v().size(); }
  /// Allocates `length` elements in the buffer, replacing the old ones.
  /// Accessors obtained earlier may be invalidated by the reallocation.
  void length(size_t length) noexcept
  {
    new (&v()) detail::Vector<T>(buffer_, static_cast<std::uint32_t>(length));
  }
  /// Bounds check used by generated validation code.
  bool _check_size_align(uint32_t max_buffer_size) const noexcept
  {
    return v().check_size_align(buffer_.data().data(), max_buffer_size);
  }

  /// Byte offset of the field within the buffer.
  std::uint32_t offset() const noexcept { return offset_; }

  /// The field at `offset` in `buffer`; created by generated code.
  Vector_Direct(flat_buffer& buffer, std::uint32_t offset)
      : buffer_(buffer)
      , offset_(offset)
  {
  }
};

/// `Vector_Direct` for elements of plain types; call it for a `Span<T>`.
template <typename T> class Vector_Direct1 : public Vector_Direct<T>
{
public:
  /// The elements.
  auto operator()() noexcept { return (Span<T>)this->v(); }

  /// The field at `offset` in `buffer`; created by generated code.
  Vector_Direct1(flat_buffer& buffer, std::uint32_t offset)
      : Vector_Direct<T>(buffer, offset)
  {
  }
};

/// `Vector_Direct` for elements that are messages or strings; call it for
/// a `Span_ref` of `TD` accessors.
template <typename T, typename TD>
class Vector_Direct2 : public Vector_Direct<T>
{
public:
  /// The elements.
  auto operator()() noexcept
  {
    return Span_ref<T, TD>(this->buffer_,
                           this->v().range(this->buffer_.data().data()));
  }

  /// The field at `offset` in `buffer`; created by generated code.
  Vector_Direct2(flat_buffer& buffer, std::uint32_t offset)
      : Vector_Direct<T>(buffer, offset)
  {
  }
};

/// A string's layout in the buffer. Public, unlike the other layout types:
/// servants taking `vector<string>` receive `Span_ref<String, String_Direct1>`.
class String : public detail::Vector<char>
{
public:
  /// Stores `str` in `buffer`.
  String(flat_buffer& buffer, std::string_view str)
  {
    auto this_ = alloc(buffer, static_cast<std::uint32_t>(str.length()));
    std::memcpy(this_->begin(), str.data(), str.length());
  }

  /// Stores `str` in `buffer`.
  String(flat_buffer& buffer, const char* str)
      : String(buffer, std::string_view{str, std::strlen(str)})
  {
  }

  /// Stores `str` in `buffer`.
  String(flat_buffer& buffer, const std::string& str)
      : String(buffer, std::string_view{std::begin(str), std::end(str)})
  {
  }
};

/// Accessor for a `string` field; call it for a `Span<char>`.
class String_Direct1 : public Vector_Direct1<char>
{
public:
  /// Replaces the string with `str`.
  void operator=(std::string_view str) noexcept
  {
    length(static_cast<std::uint32_t>(str.length()));
    auto span = this->operator()();
    std::copy(str.begin(), str.end(), span.begin());
  }

  /// The field at `offset` in `buffer`; created by generated code.
  String_Direct1(flat_buffer& buffer, std::uint32_t offset)
      : Vector_Direct1<char>(buffer, offset)
  {
  }
};

// In-buffer layout of an optional.
namespace detail {

template <typename T> class Optional
{
  std::uint32_t offset_; // from this pointer
public:
  std::uint32_t offset() const noexcept
  {
    assert(has_value());
    return offset_;
  }

  bool has_value() const noexcept { return offset_ != 0; }

  T& value() noexcept
  {
    assert(has_value());
    return *reinterpret_cast<T*>(reinterpret_cast<std::byte*>(this) + offset_);
  }

  bool check_size_align(void* base_ptr, uint32_t max_buffer_size) const noexcept
  {
    if (!offset_)
      return true;
    auto const data_offset_abs =
        ((std::byte*)this - (std::byte*)base_ptr) + offset_;
    return ((data_offset_abs % alignof(T) == 0) &&
            (data_offset_abs + sizeof(T) <= max_buffer_size));
  }

  Optional() = default;

  Optional(flat_buffer& buffer)
  {
    auto [this_, offset] = _alloc<T>(this, buffer, 1);
    static_cast<Optional<T>*>(this_)->offset_ = offset;
  }

  explicit Optional(int) { offset_ = 0; }
};

} // namespace detail

/// Accessor for an optional (`name?: T`) field. `value()` returns `T&` for
/// plain types and the `TD` accessor for messages and strings.
template <typename T, typename TD = void> class Optional_Direct
{
  flat_buffer& buffer_;
  std::uint32_t offset_;

  detail::Optional<T>& opt() noexcept
  {
    return *reinterpret_cast<detail::Optional<T>*>((std::byte*)buffer_.data().data() +
                                           offset_);
  }

  const detail::Optional<T>& opt() const noexcept
  {
    return *reinterpret_cast<const detail::Optional<T>*>(
        (std::byte*)buffer_.data().data() + offset_);
  }

public:
  /// Bounds check used by generated validation code.
  bool _check_size_align(uint32_t max_buffer_size) const noexcept
  {
    return opt().check_size_align(buffer_.data().data(), max_buffer_size);
  }

  /// Makes room for a value, which `value()` then fills in.
  void alloc() noexcept { new (&opt()) detail::Optional<T>(buffer_); }
  /// Marks the field as empty.
  void set_nullopt() noexcept { new (&opt()) detail::Optional<T>(0); }
  /// Whether the field holds a value.
  bool has_value() const noexcept { return opt().has_value(); }

  /// What `value()` returns.
  using value_ret_t = std::conditional_t<std::is_same_v<TD, void>, T&, TD>;

  /// The value; only when `has_value()`.
  value_ret_t value() noexcept
  {
    assert(opt().has_value());
    if constexpr (std::is_same_v<TD, void>) {
      return opt().value();
    } else {
      return TD{buffer_, offset_ + opt().offset()};
    }
  }

  /// The field at `offset` in `buffer`; created by generated code.
  Optional_Direct(flat_buffer& buffer, std::uint32_t offset)
      : buffer_(buffer)
      , offset_(offset)
  {
  }
};

/// A read-only byte view of `str`, e.g. to pass as `vector<u8>`.
inline const Span<const uint8_t>
make_read_only_span(const std::string& str) noexcept
{
  auto const ptr = reinterpret_cast<const uint8_t*>(str.data());
  return {ptr, ptr + str.size()};
}

/// A read-only view of `v`'s elements.
template <typename T, typename Alloc>
inline const Span<const T>
make_read_only_span(const std::vector<T, Alloc>& v) noexcept
{
  return {v.data(), v.data() + v.size()};
}

/// A read-only view of `size` elements at `data`.
template <typename T>
inline const Span<const T> make_read_only_span(const T* data,
                                               const size_t size) noexcept
{
  return {data, data + static_cast<std::uint32_t>(size)};
}

/// A read-only view of `span`.
template <typename T>
inline const Span<const T> make_read_only_span(Span<T> span) noexcept
{
  return {span.begin(), span.end()};
}

/// A writable view of `v`'s elements.
template <typename T, typename Alloc>
inline Span<T> make_span(std::vector<T, Alloc>& v) noexcept
{
  return {v.data(), v.data() + v.size()};
}

/// Owns a flat_buffer (via shared_ptr) and exposes a zero-copy span view.
/// Used as the parameter type for 'out direct vector<T>' (primitive element)
/// RPC arguments so the receive buffer is never copied.
template<typename T>
class OwnedSpan
{
  std::shared_ptr<::nprpc::flat_buffer> buf_;
  T* first_ = nullptr;
  T* last_  = nullptr;

public:
  /// An empty span; `valid()` is false.
  OwnedSpan() = default;

  /// Views `span` inside `buf`, keeping `buf` alive.
  OwnedSpan(std::shared_ptr<::nprpc::flat_buffer> buf, Span<T> span) noexcept
      : buf_(std::move(buf))
      , first_(span.first)
      , last_(span.last)
  {
  }

  /// First element.
  T*       data()  const noexcept { return first_; }
  /// Number of elements.
  uint32_t size()  const noexcept { return static_cast<uint32_t>(last_ - first_); }
  /// First element.
  T*       begin() const noexcept { return first_; }
  /// One past the last element.
  T*       end()   const noexcept { return last_;  }

  /// Element `i`.
  T&       operator[](size_t i)       { return first_[i]; }
  /// Element `i`.
  const T& operator[](size_t i) const { return first_[i]; }

  /// Whether the span refers to a buffer.
  bool valid() const noexcept { return buf_ != nullptr; }

  /// Keeps the underlying buffer alive (e.g. for async use after the RPC call).
  std::shared_ptr<::nprpc::flat_buffer> buffer() const noexcept { return buf_; }

  /// A non-owning view of the same elements.
  operator Span<T>()       noexcept { return {first_, last_}; }
  /// A non-owning read-only view of the same elements.
  operator Span<const T>() const noexcept { return {first_, last_}; }
};

/// Generic zero-copy buffer owner for any flat Direct accessor type.
/// Used for 'out direct struct/array/string/optional' RPC arguments.
/// Stores the shared_ptr that keeps the receive buffer alive, plus the
/// absolute byte offset at which the Direct object lives.  The Direct
/// accessor TD is reconstructed cheaply on every call to get().
template<typename TD>
class OwnedDirect
{
  std::shared_ptr<::nprpc::flat_buffer> buf_;
  uint32_t offset_ = 0;

public:
  /// An empty owner; `valid()` is false.
  OwnedDirect() = default;

  /// Owns `buf`, with the accessor's data at `offset`.
  OwnedDirect(std::shared_ptr<::nprpc::flat_buffer> buf, uint32_t offset) noexcept
      : buf_(std::move(buf))
      , offset_(offset)
  {
  }

  /// Creates a Direct accessor view into the owned buffer. Very cheap
  /// (returns by value — TD only holds a buffer& and a uint32_t).
  TD get() noexcept { return TD(*buf_, offset_); }

  /// Whether a buffer is held.
  bool valid() const noexcept { return buf_ != nullptr; }

  /// The owned buffer, to share its lifetime.
  std::shared_ptr<::nprpc::flat_buffer> buffer() const noexcept { return buf_; }
};

// Trait for StreamReader.
namespace detail {

/// Type trait to detect OwnedDirect<D> at compile time.
template<typename T>
struct is_owned_direct : std::false_type {};
template<typename D>
struct is_owned_direct<OwnedDirect<D>> : std::true_type {};
template<typename T>
inline constexpr bool is_owned_direct_v = is_owned_direct<T>::value;

} // namespace detail

} // namespace nprpc::flat

#endif // NPRPC_FLAT_HPP_
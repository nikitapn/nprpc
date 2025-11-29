// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory
// https://www.youtube.com/watch?v=ERzENfQ51Ck

#pragma once

#ifndef NPRPC_FLAT_HPP_
#define NPRPC_FLAT_HPP_

#include <cassert>
#include <cstddef> // std::byte
#include <memory>
#include <cstdint>
#include <tuple>
#include <ostream>
#include <string_view>
#include <vector>
#include <optional>

#include <nprpc/buffer.hpp>

namespace nprpc::flat {

class Boolean {
	std::uint8_t value_;
public:
	void set(bool value) noexcept {
		value_ = value ? 0x01 : 0x00;
	}

	bool get() const noexcept {
		assert(value_ == 0x00 || value_ == 0x01);
		return value_ == 0x01 ? true : false;
	}

	Boolean& operator=(bool value) noexcept {
		set(value);
		return *this;
	}

	operator bool() const noexcept {
		return get();
	}
};

template<typename T>
struct TSpan {
	// Forward iterators for concept compliance
	using iterator = T*;
	using const_iterator = const T*;

	T* const first;
	T* const last;

	T* begin() {
		return first;
	}

	T* end() {
		return last;
	}

	const T* begin() const {
		return first;
	}
	
	const T* end() const {
		return last;
	}

	auto size() const noexcept {
		return static_cast<uint32_t>(last - first);
	}

	T& operator[](int i) {
		return first[i];
	}

	const T& operator[](int i) const {
		return first[i];
	}

	void* data() {
		return (void*)begin();
	}

	const void* data() const {
		return (const void*)begin();
	}
};

template<typename T>
struct Span : public TSpan<T> {};

template<>
struct Span<char> : public TSpan<char> {
	void operator=(const char* str) {
		for (char& c : *this) c = *str++;
	}
	operator std::string_view() const noexcept {
		return { this->first, this->size() };
	}
	operator std::string() const noexcept {
		return std::string{ this->first, this->size() };
	}
};

template<>
struct Span<const char> : public TSpan<const char> {
	operator std::string_view() const noexcept {
		return { this->first, this->size() };
	}
};

template<>
struct Span<uint8_t> : public TSpan<uint8_t> {
	void operator=(std::string_view str) {
		std::memcpy(this->data(), str.data(), str.size());
	}
};

inline std::ostream& operator << (std::ostream& os, const Span<char>& span) {
	for (auto& e : span) os << e;
	return os;
}

template<class T, class TD>
struct Span_ref {
	flat_buffer& buffer;
	const std::uint32_t first; // absolute
	const std::uint32_t last; // absolute

	struct Ptr_ref {
		flat_buffer& buffer;
		std::uint32_t offset;

		Ptr_ref& operator++() {
			offset += sizeof(T);
			return *this;
		}

		Ptr_ref operator++(int) {
			auto tmp = offset;
			offset += sizeof(T);
			return { buffer, tmp };
		}

		TD operator*() {
			assert(offset % alignof(T) == 0);
			return { buffer, offset };
		}

		bool operator==(const Ptr_ref& other) const noexcept { return offset == other.offset; }
		bool operator!=(const Ptr_ref& other) const noexcept { return offset != other.offset; }

		Ptr_ref(flat_buffer& b, std::uint32_t o)
			: buffer(b), offset(o)
		{
			assert(offset % alignof(T) == 0);
		}
	};

	void* data() {
		return (void*)(reinterpret_cast<std::byte*>(buffer.data().data()) + first);
	}

	void* data_end() {
		return (void*)(reinterpret_cast<std::byte*>(buffer.data().data()) + last);
	}

	Ptr_ref begin() { return { buffer, first }; }
	Ptr_ref end() { return { buffer, last }; }

	auto size() const noexcept { return (last - first) / sizeof(T); }

	Span_ref(flat_buffer& b, const std::tuple<std::uint32_t, std::uint32_t>& range)
		: buffer(b), first(std::get<0>(range)), last(std::get<1>(range))
	{
		assert(first % alignof(T) == 0);
	}
};

template<typename T, size_t Size>
class Array {
	std::aligned_storage_t<sizeof(T), alignof(T)> storage_[Size];

	T* data() noexcept {
		return std::launder(reinterpret_cast<T*>(&storage_[0]));
	}
	
	const T* data() const noexcept {
		return std::launder(reinterpret_cast<const T*>(&storage_[0]));
	}
public:
	auto begin() noexcept {
		return data();
	}

	auto end() noexcept {
		return data() + Size;
	}

	const auto begin() const noexcept {
		return data();
	}

	const auto end() const noexcept {
		return data() + Size;
	}

	operator Span<T>() noexcept { return { begin(), end() }; }
	operator Span<const T>() const noexcept { return { begin(), end() }; }

	std::tuple<std::uint32_t, std::uint32_t> range(void* base_ptr) const noexcept {
		auto data_offset_abs = ((std::byte*)this - (std::byte*)base_ptr);
		assert(data_offset_abs % alignof(T) == 0);
		return { static_cast<uint32_t>(data_offset_abs), static_cast<uint32_t>(data_offset_abs + Size * sizeof(T)) };
	}
};


// returns a new 'this' if the buffer is rellocated and an offset to allocated space from that 'this'
template<typename T>
[[nodiscard]] std::tuple<void*, std::uint32_t> _alloc(void* This, flat_buffer& buffer, uint32_t size) {
	if (!size) return std::make_tuple(This, 0u);

	auto this_ = reinterpret_cast<std::byte*>(This);
	const auto old_base = reinterpret_cast<std::byte*>(buffer.data().data());
	const auto this_offset = this_ - old_base;
	const auto current_size = buffer.data().size();

	auto rem = current_size % alignof(T);
	auto padding = rem ? alignof(T) - rem : 0;

	const auto size_bytes = size * sizeof(T);
	auto ptr = reinterpret_cast<std::byte*>(buffer.prepare(size_bytes + padding).data()) + padding;
	buffer.commit(size_bytes + padding);

	if (old_base != buffer.data().data()) {
		this_ = (std::byte*)buffer.data().data() + this_offset;
	}

	return std::make_tuple(static_cast<void*>(this_), (uint32_t)(size_t)(ptr - this_));
}


template<typename T>
class Vector {
	uint32_t offset_; // in bytes from this pointer
	uint32_t size_; // in elements
protected:
		[[nodiscard]] auto alloc(flat_buffer& buffer, uint32_t size) {
			auto [this_, offset] = _alloc<T>(this, buffer, size);
			static_cast<Vector<T>*>(this_)->offset_ = offset;
			static_cast<Vector<T>*>(this_)->size_ = size;
			return static_cast<Vector<T>*>(this_);
		}
public:
	auto size() const noexcept {
		return size_;
	}

	auto begin() noexcept {
		return (T*)(reinterpret_cast<std::byte*>(this) + offset_);
	}

	auto end() noexcept {
		return begin() + size_;
	}

	auto begin() const noexcept {
		return (const T*)(reinterpret_cast<const std::byte*>(this) + offset_);
	}

	auto end() const noexcept {
		return begin() + size_;
	}

	operator Span<T>() noexcept { return { begin(), end() }; }
	operator Span<const T>() const noexcept { return { begin(), end() }; }

	template<bool UseAssert = true>
	std::tuple<std::uint32_t, std::uint32_t> range(void* base_ptr) const noexcept {
		auto data_offset_abs = ((std::byte*)this - (std::byte*)base_ptr) + offset_;
		
		if constexpr (UseAssert) {
			assert(data_offset_abs % alignof(T) == 0);
		}

		return { static_cast<uint32_t>(data_offset_abs), static_cast<uint32_t>(data_offset_abs + size_ * sizeof(T)) };
	}

	bool check_size_align(void* base_ptr, uint32_t max_buffer_size) const noexcept {
		const auto [a, b] = range<false>(base_ptr);
		return (a % alignof(T) == 0 && b <= max_buffer_size);
	}

	Vector() = default;
	Vector(flat_buffer& buffer, std::uint32_t elements_size) {
		[[maybe_unused]] auto this_ = alloc(buffer, elements_size);
	}
};

template<typename T>
class Vector_Direct {
protected:
	flat_buffer& buffer_;
	std::uint32_t offset_;
	
	auto& v() noexcept {
		return *reinterpret_cast<Vector<T>*>((std::byte*)buffer_.data().data() + offset_);
	}
	auto& v() const noexcept {
		return *reinterpret_cast<const Vector<T>*>((const std::byte*)buffer_.data().data() + offset_);
	}
public:
	std::uint32_t size() const noexcept { return v().size(); }
	void length(size_t length) noexcept { new (&v()) Vector<T>(buffer_, static_cast<std::uint32_t>(length));}
	bool _check_size_align(uint32_t max_buffer_size) const noexcept { return v().check_size_align(buffer_.data().data(), max_buffer_size); }

	Vector_Direct(flat_buffer& buffer, std::uint32_t offset)
		: buffer_(buffer), offset_(offset) {}
};

template<typename T>
class Vector_Direct1 : public Vector_Direct<T> {
public:
	auto operator ()() noexcept { return (Span<T>)this->v(); }
	
	Vector_Direct1(flat_buffer& buffer, std::uint32_t offset)
		: Vector_Direct<T>(buffer, offset) {}
};

template<typename T, typename TD>
class Vector_Direct2 : public Vector_Direct<T> {
public:
	auto operator ()() noexcept { return Span_ref<T, TD>(this->buffer_, this->v().range(this->buffer_.data().data())); }
	
	Vector_Direct2(flat_buffer& buffer, std::uint32_t offset)
		: Vector_Direct<T>(buffer, offset) {}
};

class String
	: public Vector<char> {
public:
	String(flat_buffer& buffer, std::string_view str) {
		auto this_ = alloc(buffer, static_cast<std::uint32_t>(str.length()));
		std::memcpy(this_->begin(), str.data(), str.length());
	}

	String(flat_buffer& buffer, const char* str) 
		: String(buffer, std::string_view{ str, std::strlen(str) }) {}

	String(flat_buffer& buffer, const std::string& str)
		: String(buffer, std::string_view{ std::begin(str), std::end(str) }) {}
};

class String_Direct1 : public Vector_Direct1<char> {
public:
	void operator=(std::string_view str) noexcept {
		length(static_cast<std::uint32_t>(str.length()));
		auto span = this->operator()();
		std::copy(str.begin(), str.end(), span.begin());
	}

	String_Direct1(flat_buffer& buffer, std::uint32_t offset)
		: Vector_Direct1<char>(buffer, offset) {}
};

template<typename T>
class Optional {
	std::uint32_t offset_; // from this pointer
public:
	std::uint32_t offset() const noexcept {
		assert(has_value());
		return offset_;
	}

	bool has_value() const noexcept {
		return offset_ != 0;
	}

	T& value() noexcept {
		assert(has_value());
		return *reinterpret_cast<T*>(reinterpret_cast<std::byte*>(this) + offset_);
	}

	bool check_size_align(void* base_ptr, uint32_t max_buffer_size) const noexcept {
		if (!offset_) return true;
		auto const data_offset_abs = ((std::byte*)this - (std::byte*)base_ptr) + offset_;
		return ((data_offset_abs % alignof(T) == 0) && (data_offset_abs + sizeof(T) <= max_buffer_size));
	}

	Optional() = default;
	
	Optional(flat_buffer& buffer) {
		auto [this_, offset] = _alloc<T>(this, buffer, 1);
		static_cast<Optional<T>*>(this_)->offset_ = offset;
	}

	explicit Optional(int) {
		offset_ = 0;
	}
};

template<typename T, typename TD = void>
class Optional_Direct {
	flat_buffer& buffer_;
	std::uint32_t offset_;

	Optional<T>& opt() noexcept {
		return *reinterpret_cast<Optional<T>*>((std::byte*)buffer_.data().data() + offset_);
	}

	const Optional<T>& opt() const noexcept {
		return *reinterpret_cast<const Optional<T>*>((std::byte*)buffer_.data().data() + offset_);
	}
public:
	bool _check_size_align(uint32_t max_buffer_size) const noexcept { return opt().check_size_align(buffer_.data().data(), max_buffer_size); }

	void alloc() noexcept { new (&opt()) Optional<T>(buffer_); }
	void set_nullopt() noexcept { new (&opt()) Optional<T>(0); }
	bool has_value() const noexcept { return opt().has_value(); }

	using value_ret_t = std::conditional_t<std::is_same_v<TD, void>, T&, TD>;

	value_ret_t value() noexcept {
		assert(opt().has_value());
		if constexpr (std::is_same_v<TD, void>) {
			return opt().value();
		} else {
			return TD { buffer_, offset_ + opt().offset() };
		}
	}

	Optional_Direct(flat_buffer& buffer, std::uint32_t offset)
		: buffer_(buffer)
		, offset_(offset)
	{
	}
};

inline const Span<const uint8_t> make_read_only_span(const std::string& str) noexcept  {
	auto const ptr = reinterpret_cast<const uint8_t*>(str.data());
	return {ptr, ptr + str.size()};
}

template<typename T, typename Alloc>
inline const Span<const T> make_read_only_span(const std::vector<T, Alloc>& v) noexcept  {
	return {v.data(), v.data() + v.size()};
}

template<typename T>
inline const Span<const T> make_read_only_span(const T* data, const size_t size) noexcept  {
	return {data, data + static_cast<std::uint32_t>(size)};
}

template<typename T>
inline const Span<const T> make_read_only_span(Span<T> span) noexcept  {
	return {span.begin(), span.end()};
}

template<typename T, typename Alloc>
inline Span<T> make_span(std::vector<T, Alloc>& v) noexcept  {
	return {v.data(), v.data() + v.size()};
}

} // namespace nprpc::flat

#endif // NPRPC_FLAT_HPP_
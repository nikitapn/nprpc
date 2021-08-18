// Copyright (c) 2021 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory
// https://www.youtube.com/watch?v=ERzENfQ51Ck

#pragma once

#ifndef NPIDL_FLAT_HPP_
#define NPIDL_FLAT_HPP_

#include <cstddef> // std::byte
#include <memory>
#include <cstdint>
#include <tuple>
#include <ostream>
#include <string_view>
#include <boost/beast/core/flat_buffer.hpp>
#include <vector>

namespace flat {

template<typename T>
struct TSpan { // a pure accessor read/write access to an array of T
	T* const first;
	T* const last;

	T* begin() {
		//assert((size_t)first % alignof(T) == 0);
		return first;
	}
	T* end() {
		//assert((size_t)first % alignof(T) == 0);
		return last;
	}
	const T* begin() const {
		//assert((size_t)first % alignof(T) == 0);
		return first;
	}
	
	const T* end() const {
		//assert((size_t)first % alignof(T) == 0);
		return last;
	}

	size_t size() const noexcept {
		return last - first;
	}

	T& operator[](int i) {
		//expect([&] {return i < size(); }, Error_code::bad_span_index); 
		return first[i];
	}

	const T& operator[](int i) const {
		//expect([&] {return i < size(); }, Error_code::bad_span_index); 
		return first[i];
	}

	void* data() {
		return (void*)begin();
	}
};

template<typename T>
struct Span : public TSpan<T> {};

template<>
struct Span<char> : public TSpan<char> {
	void operator=(const char* str) { // convenience and optimization
		for (char& c : *this) c = *str++;
		// expect<check_truncation>([p] { return *p == 0; }, Error_code::truncation);
	}
	operator std::string_view() const noexcept {
		return { this->first, this->size() };
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
	void operator=(std::string_view str) { // convenience and optimization
		std::memcpy(this->data(), str.data(), str.size());
	}
};

inline std::ostream& operator << (std::ostream& os, const Span<char>& span) {
	for (auto& e : span) os << e;
	return os;
}

template<class T, class TD>
struct Span_ref {	// an ordinary span except for constructing accessors upon access
	boost::beast::flat_buffer& buffer;
	const size_t first; // absolute
	const size_t last; // absolute

	struct Ptr_ref {
		boost::beast::flat_buffer& buffer;
		size_t offset;

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
		} // return accessor
		bool operator==(const Ptr_ref& other) const noexcept { return offset == other.offset; }
		bool operator!=(const Ptr_ref& other) const noexcept { return offset != other.offset; }

		Ptr_ref(boost::beast::flat_buffer& b, size_t o)
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

	size_t size() const noexcept { return (last - first) / sizeof(T); }

	Span_ref(boost::beast::flat_buffer& b, const std::tuple<size_t, size_t>& range)
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
public:
	auto begin() noexcept {
		return data();
	}

	auto end() noexcept {
		return data() + Size;
	}

	operator Span<T>() { return { begin(), end() }; }

	std::tuple<size_t, size_t> range(void* base_ptr) const noexcept {
		auto data_offset_abs = ((std::byte*)this - (std::byte*)base_ptr);
		assert(data_offset_abs % alignof(T) == 0);
		return { data_offset_abs, data_offset_abs + Size * sizeof(T) };
	}
};


template<typename T>
class Vector {
	uint32_t offset_; // in bytes from this pointer
	uint32_t size_; // in elements
public:

	[[nodiscard]]
	auto alloc(boost::beast::flat_buffer& buffer, size_t size) {
		if (!size) {
			offset_ = -1;
			size_ = 0;
			return this;
		}

		auto old_base = reinterpret_cast<std::byte*>(buffer.data().data());
		auto this_ = reinterpret_cast<std::byte*>(this);
		auto this_offset = this_ - old_base;
		
		const auto current_size = buffer.data().size();

		auto rem = current_size % alignof(T);
		auto padding = rem ? alignof(T) - rem : 0;

		const auto size_bytes = size * sizeof(T);
		auto ptr = reinterpret_cast<std::byte*>(buffer.prepare(size_bytes + padding).data()) + padding;
		buffer.commit(size_bytes + padding);
		
		if (old_base != buffer.data().data()) {
			this_ = (std::byte*)buffer.data().data() + this_offset;
		}

		auto data_offset = (uint32_t)(size_t)(ptr - this_);
		reinterpret_cast<Vector<T>*>(this_)->offset_ = data_offset;
		reinterpret_cast<Vector<T>*>(this_)->size_ = (uint32_t)size;

		return reinterpret_cast<Vector<T>*>(this_);
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

	std::tuple<size_t, size_t> range(void* base_ptr) const noexcept {
		auto data_offset_abs = ((std::byte*)this - (std::byte*)base_ptr) + offset_;
		assert(data_offset_abs % alignof(T) == 0);
		return { data_offset_abs, data_offset_abs + size_ * sizeof(T) };
	}

	Vector() = default;
	Vector(boost::beast::flat_buffer& buffer, size_t elements_size) {
		[[maybe_unused]] auto this_ = alloc(buffer, elements_size);
	}
};

template<typename T>
class Vector_Direct {
protected:
	boost::beast::flat_buffer& buffer_;
	size_t offset_;
	Vector<T>& v() noexcept {
		return *reinterpret_cast<Vector<T>*>((std::byte*)buffer_.data().data() + offset_);
	}
public:
	void length(size_t length) noexcept { new (&v()) Vector<T>(buffer_, length);}

	Vector_Direct(boost::beast::flat_buffer& buffer, size_t offset)
		: buffer_(buffer)
		, offset_(offset)
	{
	}
};

template<typename T>
class Vector_Direct1 : public Vector_Direct<T> {
public:
	auto operator ()() noexcept { return (Span<T>)this->v(); }
	Vector_Direct1(boost::beast::flat_buffer& buffer, size_t offset)
		: Vector_Direct<T>(buffer, offset) {}
};

class String_Direct1 : public Vector_Direct1<char> {
public:
	void operator=(std::string_view str) noexcept {
		length(str.length());
		auto span = this->operator()();
		std::copy(str.begin(), str.end(), span.begin());
	}
	String_Direct1(boost::beast::flat_buffer& buffer, size_t offset)
		: Vector_Direct1<char>(buffer, offset) {}
};

template<typename T, typename TD>
class Vector_Direct2 : public Vector_Direct<T> {
public:
	auto operator ()() noexcept { return Span_ref<T, TD>(this->buffer_, this->v().range(this->buffer_.data().data())); }
	Vector_Direct2(boost::beast::flat_buffer& buffer, size_t offset)
		: Vector_Direct<T>(buffer, offset)
	{
	}
};

class String
	: public Vector<char> {
public:
	String(boost::beast::flat_buffer& buffer, const char* str) {
		auto len = std::strlen(str);
		auto this_ = alloc(buffer, len);
		std::memcpy(this_->begin(), str, len);
	}

	String(boost::beast::flat_buffer& buffer, const std::string& str) {
		auto this_ = alloc(buffer, str.length());
		std::memcpy(this_->begin(), str.data(), str.length());
	}
};

inline const ::flat::Span<const uint8_t> make_read_only_span(const std::string& str) noexcept  {
	auto const ptr = reinterpret_cast<const uint8_t*>(str.data());
	return {ptr, ptr + str.size()};
}

template<typename T, typename Alloc>
inline const ::flat::Span<const T> make_read_only_span(const std::vector<T, Alloc>& v) noexcept  {
	return {v.data(), v.data() + v.size()};
}

template<typename T>
inline const ::flat::Span<const T> make_read_only_span(const T* data, const size_t size) noexcept  {
	return {data, data + size};
}

template<typename T>
inline const ::flat::Span<const T> make_read_only_span(Span<T> span) noexcept  {
	return {span.begin(), span.end()};
}

} // namespace flat

#endif // NPIDL_FLAT_HPP_
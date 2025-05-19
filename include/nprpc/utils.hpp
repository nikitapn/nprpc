// Copyright (c) 2021-2025 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by
// LICENSING file in the topmost directory

#pragma once

#include <concepts>
#include <memory>

namespace nprpc {
namespace detail {

template<typename T> struct is_raw_or_smart_pointer : std::false_type {};
template<typename T> struct is_raw_or_smart_pointer<T*> : std::true_type {};
template<typename T> struct is_raw_or_smart_pointer<const T*> : std::true_type {};
template<typename T> struct is_raw_or_smart_pointer<std::unique_ptr<T>> : std::true_type {};
template<typename T> struct is_raw_or_smart_pointer<const std::unique_ptr<T>> : std::true_type {};
template<typename T> struct is_raw_or_smart_pointer<std::shared_ptr<T>> : std::true_type {};
template<typename T> struct is_raw_or_smart_pointer<const std::shared_ptr<T>> : std::true_type {};

template <typename T, typename U>
struct wrapper1 {
	typedef std::conditional_t<std::is_const_v<T>, const T, T> _T;

	static constexpr bool is_pointer = false;

	auto operator->() noexcept { return &ref_; }
	auto const operator->() const noexcept { return &ref_; }

	wrapper1(_T& ref) : ref_(ref) {}
	_T& ref_;
};

template <typename T>
struct wrapper1<T, std::true_type> {
	typedef std::conditional_t<std::is_const_v<T>, const T, T> _T;

	static constexpr bool is_pointer = true;

	auto operator->() noexcept { return ref_.get(); }
	auto const operator->() const noexcept { return ref_.get(); }

	wrapper1(_T& ref) : ref_(ref) {}
	_T& ref_;
};
}

template<typename T>
auto make_wrapper1(T& t) {
	return detail::wrapper1<T, typename detail::is_raw_or_smart_pointer<T>::type>(t);
}

template<typename T>
auto make_wrapper1(const T& t) {
	return detail::wrapper1<const T, typename detail::is_raw_or_smart_pointer<const T>::type>(t);
}

template<typename T>
concept IterableCollection = requires(T a) {
	typename T::iterator;
	requires std::forward_iterator<typename T::iterator>;
	{ a.size() }; { a.begin() }; { a.end() };
};

}

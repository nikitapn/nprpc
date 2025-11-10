// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

#pragma once

#include <nprpc/exception.hpp>

namespace nprpc {

template<typename T>
class ObjectPtr {
	T* obj_;

	void safe_add_ref() noexcept {
		if (obj_) obj_->add_ref();
	}

	void safe_release() noexcept {
		if (!obj_) return;
		try {
			obj_->release();
		} catch (Exception&) {
		}
	}
public:
	ObjectPtr() noexcept
		: obj_{nullptr}
	{
	}

	ObjectPtr(const ObjectPtr<T>& other) noexcept
		: obj_{other.obj_}
	{
		safe_add_ref();
	}

	explicit ObjectPtr(T* obj) noexcept
		: obj_{obj}
	{
	}

	ObjectPtr(ObjectPtr<T>&& other) noexcept
		: obj_{boost::exchange(other.obj_, nullptr)}
	{
	}

	ObjectPtr<T>& operator=(const ObjectPtr<T>& other) noexcept
	{
		obj_ = other.obj_;
		safe_add_ref();
		return *this;
	}

	ObjectPtr<T>& operator=(ObjectPtr<T>&& other) noexcept
	{
		obj_ = boost::exchange(other.obj_, nullptr);
		return *this;
	}

	void reset(T* obj = nullptr) noexcept
	{
		safe_release();
		obj_ = obj;
	}

	void force_reset(T* obj = nullptr) noexcept
	{
		if (obj_) delete obj_;
		obj_ = obj;
	}

	T* operator->() noexcept { return obj_; }
	operator bool() const noexcept { return !(obj_ == nullptr); }

	T* get() noexcept { return obj_; }
	T*& get_address_of() noexcept { return obj_; }

	~ObjectPtr()
	{
		reset(nullptr);
	}
};
} // namespace nprpc
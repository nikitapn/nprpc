// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <nprpc/exception.hpp>
#include <utility> // std::exchange

namespace nprpc {

/// Owning handle to a proxy (`Object` or a generated subclass), like a
/// `shared_ptr` whose count lives in the object: copies call `add_ref()`,
/// destruction and `reset()` call `release()`.
///
/// Constructing from a raw pointer adopts a reference the caller already
/// holds; it does not add one.
template <typename T> class ObjectPtr
{
  T* obj_;

  void safe_add_ref() noexcept
  {
    if (obj_)
      obj_->add_ref();
  }

  void safe_release() noexcept
  {
    if (!obj_)
      return;
    try {
      obj_->release();
    } catch (Exception&) {
    }
  }

public:
  /// An empty handle.
  ObjectPtr() noexcept
      : obj_{nullptr}
  {
  }

  /// Shares `other`'s object, adding a reference.
  ObjectPtr(const ObjectPtr<T>& other) noexcept
      : obj_{other.obj_}
  {
    safe_add_ref();
  }

  /// Adopts `obj` and the reference the caller holds on it.
  explicit ObjectPtr(T* obj) noexcept
      : obj_{obj}
  {
  }

  /// Takes `other`'s object, leaving `other` empty.
  ObjectPtr(ObjectPtr<T>&& other) noexcept
      : obj_{std::exchange(other.obj_, nullptr)}
  {
  }

  /// Shares `other`'s object, adding a reference, and releases the one
  /// held before.
  ObjectPtr<T>& operator=(const ObjectPtr<T>& other) noexcept
  {
    // Add before releasing: on self-assignment the release could otherwise
    // drop the last reference and destroy the object.
    T* incoming = other.obj_;
    if (incoming)
      incoming->add_ref();
    safe_release();
    obj_ = incoming;
    return *this;
  }

  /// Takes `other`'s object, leaving `other` empty, and releases the one
  /// held before.
  ObjectPtr<T>& operator=(ObjectPtr<T>&& other) noexcept
  {
    if (this != &other)
      reset(std::exchange(other.obj_, nullptr));
    return *this;
  }

  /// Releases the current object, if any, and adopts `obj`.
  void reset(T* obj = nullptr) noexcept
  {
    safe_release();
    obj_ = obj;
  }

  /// Deletes the current proxy without releasing it, e.g. after the
  /// runtime has shut down, and adopts `obj`.
  void force_reset(T* obj = nullptr) noexcept
  {
    if (obj_)
      delete obj_;
    obj_ = obj;
  }

  /// Calls a method on the object.
  T* operator->() noexcept { return obj_; }
  /// Whether the handle holds an object.
  operator bool() const noexcept { return !(obj_ == nullptr); }

  /// The raw pointer, still owned by this handle.
  T* get() noexcept { return obj_; }
  /// The stored pointer itself, for out-parameters that fill it in.
  T*& get_address_of() noexcept { return obj_; }

  ~ObjectPtr() { reset(nullptr); }
};
} // namespace nprpc
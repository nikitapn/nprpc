// Copyright (c) 2021-2026, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <utility>

#include <nprpc/export.hpp>

namespace nprpc {

/**
 * @brief An owned file descriptor: closed when the owner goes away.
 *
 * The C++ type of the IDL `fd`.  A servant receives one by value and owns
 * it; a proxy returns one for an `out` or return value.  An `in` argument is
 * a plain `int` borrowed for the duration of the call: the runtime duplicates
 * it, so the caller's descriptor is left as it was.
 *
 * Only a transport that can carry descriptors between processes can send
 * one.  Calling a method with an `fd` over any other throws before anything
 * is sent.
 */
class NPRPC_API UniqueFd
{
  int fd_ = -1;

public:
  /// No descriptor.
  UniqueFd() noexcept = default;
  /// Takes ownership of `fd`.
  explicit UniqueFd(int fd) noexcept
      : fd_(fd)
  {
  }

  UniqueFd(UniqueFd&& other) noexcept
      : fd_(std::exchange(other.fd_, -1))
  {
  }

  UniqueFd& operator=(UniqueFd&& other) noexcept
  {
    if (this != &other)
      reset(std::exchange(other.fd_, -1));
    return *this;
  }

  UniqueFd(const UniqueFd&) = delete;
  UniqueFd& operator=(const UniqueFd&) = delete;

  ~UniqueFd() { reset(); }

  /// A close-on-exec duplicate of `fd`, which stays open and stays the
  /// caller's.  Empty if `fd` is negative; throws std::system_error if the
  /// duplicate cannot be made.
  static UniqueFd dup(int fd);

  /// The descriptor, still owned; -1 if none.
  int get() const noexcept { return fd_; }
  bool valid() const noexcept { return fd_ >= 0; }
  explicit operator bool() const noexcept { return valid(); }

  /// Gives up ownership without closing.
  [[nodiscard]] int release() noexcept { return std::exchange(fd_, -1); }

  /// Closes the current descriptor, if any, and takes `fd`.
  void reset(int fd = -1) noexcept;
};

} // namespace nprpc

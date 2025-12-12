// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <string>

namespace nprpc::impl {

class SharedUUID
{
  struct Impl;
  std::unique_ptr<Impl> pimpl_;

  SharedUUID();
  SharedUUID(const SharedUUID&) = delete;
  SharedUUID& operator=(const SharedUUID&) = delete;

public:
  using uuid_array = std::array<std::uint8_t, 16>;

  static SharedUUID& instance();

  // Get current UUID value
  const uuid_array& get() const noexcept;

  // Generate new UUID and store it
  void generate_new();

  ~SharedUUID();
};

} // namespace nprpc::impl
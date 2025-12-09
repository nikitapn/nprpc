// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <stdexcept>

namespace nprpc {
class Exception : public std::runtime_error
{
public:
  explicit Exception(char const* const msg) noexcept : std::runtime_error(msg)
  {
  }

  explicit Exception(std::string const& msg) noexcept : std::runtime_error(msg)
  {
  }
};
} // namespace nprpc
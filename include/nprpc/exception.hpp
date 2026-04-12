// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <stdexcept>

namespace nprpc {
class Exception : public std::runtime_error
{
public:
  explicit Exception(char const* const msg) noexcept
      : std::runtime_error(msg)
  {
  }

  explicit Exception(std::string const& msg) noexcept
      : std::runtime_error(msg)
  {
  }
};

/// Thrown when an async RPC call is cancelled via std::stop_token.
class OperationCancelled : public Exception
{
public:
  OperationCancelled() : Exception("OperationCancelled") {}
};

} // namespace nprpc
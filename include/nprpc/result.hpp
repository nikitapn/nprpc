// Copyright (c) 2021-2026, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>

namespace nprpc {

enum class ErrorCode : int32_t {
  invalid_argument = 1,
  no_endpoint = 2,
  not_initialized = 3,
  timeout = 4,
  cancelled = 5,
  connection_failure = 6,
  out_of_memory = 7,
  internal_error = 8,
  unsupported_transport = 9,
};

/// An owned error value. Bounded diagnostics keep failure translation usable
/// after allocation failure; messages may be truncated to 511 bytes.
struct Error {
  ErrorCode code;
  int32_t native_code = 0;
  std::array<char, 64> native_category{};
  std::array<char, 512> message{};

  Error(ErrorCode code, const char* text, int32_t native = 0,
        const char* category = "") noexcept
      : code(code), native_code(native) {
    copy(message.data(), message.size(), text);
    copy(native_category.data(), native_category.size(), category);
  }

private:
  static void copy(char* out, std::size_t capacity, const char* text) noexcept {
    if (!text) return;
    const auto size = std::min(capacity - 1, std::strlen(text));
    std::memcpy(out, text, size);
    out[size] = '\0';
  }
};

template<class T> using Result = std::expected<T, Error>;

} // namespace nprpc

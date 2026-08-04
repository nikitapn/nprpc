// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT
//
// Lightweight logging for the Swift C++ bridge.
// Avoids depending on monorepo-private src/logging.hpp so the package can
// build against a system-installed libnprpc.

#pragma once

#include <format>
#include <iostream>
#include <string_view>

namespace nprpc_swift {

enum class BridgeLogLevel { warn, error };

inline void bridge_log(BridgeLogLevel level, std::string_view msg)
{
  const char* tag = level == BridgeLogLevel::error ? "E" : "W";
  std::clog << "[NPRPC/SWB] [" << tag << "] " << msg << std::endl;
}

template <typename... Args>
void bridge_log_error(std::format_string<Args...> fmt, Args&&... args)
{
  bridge_log(BridgeLogLevel::error, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void bridge_log_warn(std::format_string<Args...> fmt, Args&&... args)
{
  bridge_log(BridgeLogLevel::warn, std::format(fmt, std::forward<Args>(args)...));
}

} // namespace nprpc_swift

#define NPRPC_LOG_ERROR(...) ::nprpc_swift::bridge_log_error(__VA_ARGS__)
#define NPRPC_LOG_WARN(...)  ::nprpc_swift::bridge_log_warn(__VA_ARGS__)
// Bridge currently only uses ERROR/WARN; keep no-ops for accidental use.
#define NPRPC_LOG_INFO(...)  ((void)0)
#define NPRPC_LOG_DEBUG(...) ((void)0)
#define NPRPC_LOG_TRACE(...) ((void)0)

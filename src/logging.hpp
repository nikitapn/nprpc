// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

// Internal logging header - do not expose in public API

#include <chrono>
#include <ctime>
#include <format>
#include <iostream>
#include <memory>
#include <mutex>

namespace nprpc::impl {

enum class LogLevel {
  trace = 0,
  debug = 1,
  info = 2,
  warn = 3,
  error = 4,
  critical = 5,
  off = 6
};

// Simple logger that mimics spdlog's interface
class SimpleLogger
{
  std::string name_;
  LogLevel level_;
  std::mutex mutex_;

  const char* level_color(LogLevel lvl) const
  {
    switch (lvl) {
    case LogLevel::trace:
      return "\033[37m"; // white
    case LogLevel::debug:
      return "\033[36m"; // cyan
    case LogLevel::info:
      return "\033[32m"; // green
    case LogLevel::warn:
      return "\033[33m"; // yellow
    case LogLevel::error:
      return "\033[31m"; // red
    case LogLevel::critical:
      return "\033[1;31m"; // bold red
    default:
      return "\033[0m";
    }
  }

  const char* level_name(LogLevel lvl) const
  {
    switch (lvl) {
    case LogLevel::trace:
      return "trace";
    case LogLevel::debug:
      return "debug";
    case LogLevel::info:
      return "info";
    case LogLevel::warn:
      return "warn";
    case LogLevel::error:
      return "error";
    case LogLevel::critical:
      return "critical";
    default:
      return "unknown";
    }
  }

  std::string format_time() const
  {
    auto now = std::chrono::system_clock::now();
    auto time_t_val = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) %
              1000;

    std::tm tm;
    localtime_r(&time_t_val, &tm);

    // Format without ostringstream to avoid locale overhead
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
             tm.tm_min, tm.tm_sec, static_cast<int>(ms.count()));
    return buf;
  }

  template <typename... Args>
  void log_impl(LogLevel lvl, std::format_string<Args...> fmt, Args&&... args)
  {
    if (lvl < level_)
      return;

    std::lock_guard<std::mutex> lock(mutex_);

    // Format: [2025-12-09 15:30:45.123] [nprpc] [level] message
    std::clog << "[" << format_time() << "] "
              << "[" << name_ << "] "
              << "[" << level_color(lvl) << level_name(lvl) << "\033[0m] "
              << std::format(fmt, std::forward<Args>(args)...) << std::endl;
  }

public:
  SimpleLogger(const std::string& name, LogLevel level = LogLevel::info)
      : name_(name), level_(level)
  {
  }

  void set_level(LogLevel level) { level_ = level; }
  LogLevel level() const { return level_; }

  template <typename... Args>
  void trace(std::format_string<Args...> fmt, Args&&... args)
  {
    log_impl(LogLevel::trace, fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  void debug(std::format_string<Args...> fmt, Args&&... args)
  {
    log_impl(LogLevel::debug, fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  void info(std::format_string<Args...> fmt, Args&&... args)
  {
    log_impl(LogLevel::info, fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  void warn(std::format_string<Args...> fmt, Args&&... args)
  {
    log_impl(LogLevel::warn, fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  void error(std::format_string<Args...> fmt, Args&&... args)
  {
    log_impl(LogLevel::error, fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  void critical(std::format_string<Args...> fmt, Args&&... args)
  {
    log_impl(LogLevel::critical, fmt, std::forward<Args>(args)...);
  }
};

// Singleton logger instance
inline std::shared_ptr<SimpleLogger>& get_logger()
{
  static std::shared_ptr<SimpleLogger> logger =
      std::make_shared<SimpleLogger>("nprpc", LogLevel::info);
  return logger;
}

} // namespace nprpc::impl

// Internal logging macros - same interface as before
#define NPRPC_LOG_TRACE(...) nprpc::impl::get_logger()->trace(__VA_ARGS__)
#define NPRPC_LOG_DEBUG(...) nprpc::impl::get_logger()->debug(__VA_ARGS__)
#define NPRPC_LOG_INFO(...) nprpc::impl::get_logger()->info(__VA_ARGS__)
#define NPRPC_LOG_WARN(...) nprpc::impl::get_logger()->warn(__VA_ARGS__)
#define NPRPC_LOG_ERROR(...) nprpc::impl::get_logger()->error(__VA_ARGS__)
#define NPRPC_LOG_CRITICAL(...) nprpc::impl::get_logger()->critical(__VA_ARGS__)

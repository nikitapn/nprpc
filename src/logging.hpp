#pragma once

// Internal logging header - do not expose in public API

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <memory>
#include <mutex>

namespace nprpc::impl {

// Singleton logger instance
inline std::shared_ptr<spdlog::logger>& get_logger()
{
  static std::shared_ptr<spdlog::logger> logger = []() {
    auto l = spdlog::stdout_color_mt("nprpc");
    l->set_level(spdlog::level::info);
    l->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v");
    return l;
  }();
  return logger;
}

}  // namespace nprpc::impl

// Internal logging macros
#define NPRPC_LOG_TRACE(...)    nprpc::impl::get_logger()->trace(__VA_ARGS__)
#define NPRPC_LOG_DEBUG(...)    nprpc::impl::get_logger()->debug(__VA_ARGS__)
#define NPRPC_LOG_INFO(...)     nprpc::impl::get_logger()->info(__VA_ARGS__)
#define NPRPC_LOG_WARN(...)     nprpc::impl::get_logger()->warn(__VA_ARGS__)
#define NPRPC_LOG_ERROR(...)    nprpc::impl::get_logger()->error(__VA_ARGS__)
#define NPRPC_LOG_CRITICAL(...) nprpc::impl::get_logger()->critical(__VA_ARGS__)

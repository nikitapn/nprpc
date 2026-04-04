#pragma once

#include <nprpc/export.hpp>

#include <string>
#include <cstdint>

namespace nprpc::impl {

NPRPC_API uint32_t get_thread_id();
NPRPC_API std::string get_thread_name();
NPRPC_API const char* get_thread_name_cstr();
NPRPC_API bool set_thread_name(const std::string& name);

} // namespace nprpc::impl
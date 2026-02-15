// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include "logging.hpp"

namespace nprpc::impl {

NPRPC_API std::shared_ptr<SimpleLogger>& get_logger()
{
  static std::shared_ptr<SimpleLogger> logger =
      std::make_shared<SimpleLogger>("nprpc", LogLevel::info);
  return logger;
}

} // namespace nprpc::impl

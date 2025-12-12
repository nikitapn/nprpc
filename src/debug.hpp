// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#define NPRPC_ENABLE_HTTP3_TRACE 0

#if NPRPC_ENABLE_HTTP3_TRACE
#include <format>
#define NPRPC_HTTP3_TRACE(format_string, ...)                                  \
  std::clog << std::format("[HTTP/3][T] " format_string __VA_OPT__(, )         \
                               __VA_ARGS__)                                    \
            << std::endl;
#else
#define NPRPC_HTTP3_TRACE(format_string, ...)                                  \
  do {                                                                         \
  } while (0)
#endif
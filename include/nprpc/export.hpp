// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

// Macro to define module-specific export/import attributes
#define NPRPC_DEFINE_MODULE_EXPORT(MODULE_NAME, EXPORT_MACRO)                  \
  _Pragma("GCC diagnostic push")                                               \
      _Pragma("GCC diagnostic ignored \"-Wunused-macros\"")                    \
          EXPORT_MACRO _Pragma("GCC diagnostic pop")

#ifdef _MSC_VER
#ifdef NPRPC_EXPORTS
#define NPRPC_API __declspec(dllexport)
#else
#define NPRPC_API __declspec(dllimport)
#endif
#define NPRPC_EXPORT_ATTR __declspec(dllexport)
#define NPRPC_IMPORT_ATTR __declspec(dllimport)
#else
#if defined(__GNUC__) || defined(__clang__)
#ifdef NPRPC_EXPORTS
#define NPRPC_API __attribute__((visibility("default")))
#else
#define NPRPC_API __attribute__((visibility("default")))
#endif
#define NPRPC_EXPORT_ATTR __attribute__((visibility("default")))
#define NPRPC_IMPORT_ATTR __attribute__((visibility("default")))
#else
#define NPRPC_API
#define NPRPC_EXPORT_ATTR
#define NPRPC_IMPORT_ATTR
#endif
#endif

// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include "model.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace npdoc {

struct CppOptions {
  // Every header under these directories is parsed, and only declarations
  // located in them are reported.
  std::vector<std::filesystem::path> header_roots;
  // Headers whose path relative to their root starts with one of these are
  // skipped, e.g. "impl/".
  std::vector<std::string> exclude_prefixes;
  // Compiler arguments: -I, -D, -std, and -resource-dir so that libclang
  // finds its builtin headers.
  std::vector<std::string> clang_args;
  // Base for the `file` field of every symbol.
  std::filesystem::path root;
};

// Parses all headers as one translation unit and returns every public
// declaration in them: records, enums and enumerators, functions and
// methods, fields, aliases, namespace-scope variables and concepts.
// Namespaces named `detail` or `impl` and names starting with `_` are
// internal and skipped, as are the members of a coroutine `promise_type`.
// Throws std::runtime_error if parsing fails.
std::vector<Symbol> extract_cpp(const CppOptions& options);

} // namespace npdoc

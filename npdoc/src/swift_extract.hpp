// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include "model.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace npdoc {

// Reads a symbol graph the Swift compiler wrote with
//   -emit-symbol-graph -symbol-graph-minimum-access-level public
// and returns its symbols. Skipped: compiler-synthesized members (Equatable
// `==`, Codable inits, ...), underscore names, symbols with no source
// location (C declarations re-exported through C++ interop), and symbols in
// files whose path under `root` starts with one of `exclude_prefixes`.
// `root` is also the base for the `file` field. Throws std::runtime_error on
// unreadable input.
std::vector<Symbol> extract_swift(const std::filesystem::path& symbol_graph,
                                  const std::filesystem::path& root,
                                  const std::vector<std::string>& exclude_prefixes = {});

} // namespace npdoc

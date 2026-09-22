// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include "model.hpp"

#include <filesystem>
#include <vector>

namespace npdoc {

// Reads a `<file>.doc.json` written by `npidl --doc-json`. Messages,
// exceptions, enums, interfaces and variants become symbols, with their
// fields, items and methods as children; signatures are spelled in IDL.
// `root` is the base for the `file` field. Throws std::runtime_error on
// unreadable input.
std::vector<Symbol> extract_idl(const std::filesystem::path& doc_json,
                                const std::filesystem::path& root);

} // namespace npdoc

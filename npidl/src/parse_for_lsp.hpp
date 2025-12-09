// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// This file is a part of npsystem (Distributed Control System) and covered by
// LICENSING file in the topmost directory

#pragma once

#include <string>
#include <vector>

namespace npidl {

// Forward declarations
class Context;

// Error information from parser
struct ParseError {
  int line; // 1-based
  int col;  // 1-based
  std::string message;
};

// Parse in-memory content into an existing context (for LSP multi-file support)
// Returns true if parsing succeeded (no errors found)
// Errors vector is populated with all found errors
// Context is updated with parsed AST and imports
bool parse_for_lsp(Context& ctx,
                   const std::string& content,
                   std::vector<ParseError>& errors);

// Parse in-memory content into a throwaway context (for testing)
// Returns true if parsing succeeded (no errors found)
bool parse_string_for_testing(const std::string& content,
                              std::vector<ParseError>& errors);

} // namespace npidl

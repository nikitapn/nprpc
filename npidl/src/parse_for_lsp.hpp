// Copyright (c) 2025 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

#pragma once

#include <vector>
#include <string>

namespace npidl {

// Error information from parser
struct ParseError {
  int line;        // 1-based
  int col;         // 1-based  
  std::string message;
};

// Parse in-memory content for testing (error recovery enabled)
// Returns true if parsing succeeded (no errors found)
// Errors vector is populated with all found errors
bool parse_for_lsp(const std::string& content, std::vector<ParseError>& errors);

// Alias for clarity - same function, better name for testing
inline bool parse_string_for_testing(const std::string& content, std::vector<ParseError>& errors) {
  return parse_for_lsp(content, errors);
}

} // namespace npidl

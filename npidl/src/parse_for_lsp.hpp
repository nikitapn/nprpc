// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <string>
#include <vector>

namespace npidl {

// Forward declarations
class Context;
class ISourceProvider;

// Error information from parser
struct ParseError {
  int line; // 1-based
  int col;  // 1-based
  std::string message;
};

// Load built-in types (nprpc::detail::ObjectId, etc.) into an empty context.
void load_builtins(Context& ctx);

// Parse into an existing context. Always resets the context first so a
// subsequent edit/reparse cannot see the previous AST or symbol table.
// Returns true if parsing succeeded (no errors found).
bool parse_for_lsp(Context& ctx,
                   ISourceProvider& source,
                   std::vector<ParseError>& errors);

// Parse in-memory content into an existing context (for tests and LSP).
bool parse_for_lsp(Context& ctx,
                   const std::string& content,
                   std::vector<ParseError>& errors);

// Parse in-memory content into a throwaway context (for testing)
// Returns true if parsing succeeded (no errors found)
bool parse_string_for_testing(const std::string& content,
                              std::vector<ParseError>& errors);

} // namespace npidl

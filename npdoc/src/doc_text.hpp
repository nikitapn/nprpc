// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include "model.hpp"

#include <string>
#include <string_view>

namespace npdoc {

// A doc comment split into its body and the parts that the model keeps in
// separate fields.
struct DocParts {
  std::string doc;
  std::vector<Param> params;
  std::optional<std::string> returns;
};

// Removes `///`, `//!`, `/** */` and a block comment's leading `*` from a raw
// comment as libclang returns it, keeping the text's own indentation.
std::string strip_comment_markers(std::string_view raw);

// Doxygen commands (`@param`, `@return`, `@brief`, `@p x`, `@note`, ...) to
// fields and Markdown. Text inside ``` fences is left alone.
DocParts parse_doxygen(std::string_view text);

// Swift callouts (`- Parameter x:`, `- Parameters:` lists, `- Returns:`) to
// fields; other callouts stay in the body.
DocParts parse_swift(std::string_view text);

// First paragraph on one line, for listings. Empty if the doc opens with a
// code block.
std::string summary_of(std::string_view markdown);

} // namespace npdoc

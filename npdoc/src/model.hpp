// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <optional>
#include <string>
#include <vector>

namespace npdoc {

// One parameter of a function-like symbol. `type` is absent where the source
// does not state it separately from the signature (C++, Swift).
struct Param {
  std::string name;
  std::optional<std::string> type;
  std::optional<std::string> direction; // IDL only: "in" | "out"
  std::string doc;
  std::string doc_html;
};

// One documented (or documentable) declaration. The model is flat: the tree
// is recovered through `parent`, which is the `id` of the enclosing symbol.
struct Symbol {
  // Unique across languages: "<lang>:<qualified name>", plus the parameter
  // types for C++ functions so overloads stay apart.
  std::string id;
  std::string lang; // "cpp" | "swift" | "idl"
  // Normalized across languages: class, struct, union, enum, case, function,
  // method, constructor, operator, field, property, variable, typealias,
  // protocol, concept, interface, message, exception, variant, constant.
  std::string kind;
  std::string name;
  std::string qualified;
  std::optional<std::string> parent;
  // The declaration as the language spells it, without a body.
  std::string signature;
  // Markdown, with parameter and return docs moved out into the fields
  // below.
  std::string doc;
  // First paragraph of `doc` on one line, for listings.
  std::string summary;
  // `doc`, `summary` and `returns` rendered; summary without its <p>.
  std::string doc_html;
  std::string summary_html;
  std::optional<std::vector<Param>> params;
  std::optional<std::string> returns;
  std::optional<std::string> returns_html;
  std::optional<std::string> file; // relative to --root
  std::optional<int> line;
};

// A hand-written Markdown page, such as docs/BUILD.md.
struct Guide {
  std::string slug; // file stem: BUILD
  std::string title; // first `# ` heading, or the slug
  std::string file; // relative to --root
  std::string html;
};

struct Api {
  int format = 1;
  std::vector<Symbol> symbols;
  std::vector<Guide> guides;
};

} // namespace npdoc

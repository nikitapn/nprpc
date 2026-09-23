// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include "swift_extract.hpp"
#include "doc_text.hpp"
#include "json_io.hpp"

#include <glaze/glaze.hpp>

#include <map>
#include <set>
#include <stdexcept>

namespace fs = std::filesystem;

namespace npdoc {

namespace sg {

// The subset of the symbol graph format npdoc reads.
struct Position {
  int line = 0;
  int character = 0;
};
struct Fragment {
  std::string kind;
  std::string spelling;
};
struct DocLine {
  std::string text;
};
struct DocComment {
  std::vector<DocLine> lines;
};
struct Kind {
  std::string identifier;
};
struct Identifier {
  std::string precise;
};
struct Names {
  std::string title;
};
struct Location {
  std::string uri;
  Position position;
};
struct Symbol {
  Kind kind;
  Identifier identifier;
  std::vector<std::string> pathComponents;
  Names names;
  std::optional<DocComment> docComment;
  std::vector<Fragment> declarationFragments;
  std::optional<Location> location;
};
struct Relationship {
  std::string kind;
  std::string source;
  std::string target;
};
struct Module {
  std::string name;
};
struct Graph {
  Module module;
  std::vector<Symbol> symbols;
  std::vector<Relationship> relationships;
};

} // namespace sg

namespace {

std::string kind_name(std::string_view id)
{
  static const std::map<std::string_view, std::string_view> kinds{
      {"swift.class", "class"},
      {"swift.struct", "struct"},
      {"swift.enum", "enum"},
      {"swift.enum.case", "case"},
      {"swift.protocol", "protocol"},
      {"swift.method", "method"},
      {"swift.type.method", "method"},
      {"swift.func", "function"},
      {"swift.func.op", "operator"},
      {"swift.init", "constructor"},
      {"swift.property", "property"},
      {"swift.type.property", "property"},
      {"swift.var", "variable"},
      {"swift.typealias", "typealias"},
      {"swift.associatedtype", "typealias"},
      {"swift.subscript", "method"},
      {"swift.type.subscript", "method"},
      {"swift.macro", "function"},
  };
  const auto it = kinds.find(id);
  return std::string(it != kinds.end() ? it->second : id);
}

// Swift titles carry argument labels: `withPageHandler(_:)`.
std::string base_name(const std::string& title)
{
  return title.substr(0, title.find('('));
}

std::string file_of(const std::string& uri, const fs::path& root)
{
  std::string_view path = uri;
  if (path.starts_with("file://"))
    path.remove_prefix(7);
  return fs::path(path)
      .lexically_relative(fs::weakly_canonical(root))
      .generic_string();
}

} // namespace

std::vector<Symbol> extract_swift(const fs::path& symbol_graph,
                                  const fs::path& root,
                                  const std::vector<std::string>& exclude_prefixes)
{
  sg::Graph graph;
  read_json_file(symbol_graph, graph);

  const auto& module = graph.module.name;
  std::map<std::string, std::string> id_of_precise;

  auto keep = [&](const sg::Symbol& s) {
    if (s.identifier.precise.find("::SYNTHESIZED::") != std::string::npos)
      return false;
    if (!s.location)
      return false;
    const auto file = file_of(s.location->uri, root);
    for (const auto& prefix : exclude_prefixes)
      if (file.starts_with(prefix))
        return false;
    for (const auto& part : s.pathComponents)
      if (part.starts_with('_'))
        return false;
    return !s.pathComponents.empty();
  };

  std::set<std::string> used_ids;
  for (const auto& s : graph.symbols) {
    if (!keep(s))
      continue;
    std::string qualified = module;
    for (const auto& part : s.pathComponents)
      qualified += "." + part;
    std::string id = "swift:" + qualified;
    // Overloads that differ only in types share a title; the compiler's
    // mangled id is stable, so its tail tells them apart.
    if (!used_ids.insert(id).second) {
      const auto& precise = s.identifier.precise;
      id += "#" + precise.substr(precise.size() > 8 ? precise.size() - 8 : 0);
      used_ids.insert(id);
    }
    id_of_precise[s.identifier.precise] = std::move(id);
  }

  std::map<std::string, std::string> parent_of; // precise -> precise
  for (const auto& r : graph.relationships) {
    if (r.kind == "memberOf" || r.kind == "requirementOf" ||
        r.kind == "optionalRequirementOf")
      parent_of.emplace(r.source, r.target);
  }

  std::vector<Symbol> out;
  for (const auto& s : graph.symbols) {
    const auto id_it = id_of_precise.find(s.identifier.precise);
    if (id_it == id_of_precise.end())
      continue;

    Symbol sym;
    sym.lang = "swift";
    sym.id = id_it->second;
    sym.kind = kind_name(s.kind.identifier);
    sym.name = base_name(s.names.title);
    sym.qualified = module;
    for (const auto& part : s.pathComponents)
      sym.qualified += "." + base_name(part);
    for (const auto& f : s.declarationFragments)
      sym.signature += f.spelling;

    // Parent only if it is in this graph: members of extensions to other
    // modules' types have nothing to hang under.
    if (auto p = parent_of.find(s.identifier.precise); p != parent_of.end())
      if (auto pid = id_of_precise.find(p->second); pid != id_of_precise.end())
        sym.parent = pid->second;

    if (s.location) {
      sym.file = file_of(s.location->uri, root);
      sym.line = s.location->position.line + 1; // symbol graph lines are 0-based
    }

    if (s.docComment) {
      std::string text;
      for (const auto& line : s.docComment->lines) {
        if (!text.empty())
          text += '\n';
        text += line.text;
      }
      auto parts = parse_swift(text);
      sym.doc = std::move(parts.doc);
      sym.returns = std::move(parts.returns);
      if (!parts.params.empty())
        sym.params = std::move(parts.params);
    }
    sym.summary = summary_of(sym.doc);
    out.push_back(std::move(sym));
  }
  return out;
}

} // namespace npdoc

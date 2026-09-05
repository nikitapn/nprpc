// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include "ast.hpp"
#include "position_index.hpp"

#include <optional>

namespace npidl {

// Builds a PositionIndex from a parsed Context
class PositionIndexBuilder
{
  PositionIndex& index_;
  Context& ctx_;

public:
  PositionIndexBuilder(PositionIndex& index, Context& ctx)
      : index_(index)
      , ctx_(ctx)
  {
  }

  // Build the complete index from the context
  void build()
  {
    // Index imports
    for (auto* import : ctx_.imports) {
      if (import->import_line > 0) { // Has position info
        index_.add(import, PositionIndex::NodeType::Import, import->import_line,
                   import->import_col, import->import_line,
                   import->path_end_col);
      }
    }

    // Index interfaces
    for (auto* interface : ctx_.interfaces) {
      index_interface(interface);
    }

    // Index exceptions (they're stored separately but are actually structs)
    for (auto* exception : ctx_.exceptions) {
      index_struct(exception, true);
    }

    // Index all types from namespace (includes aliases, enums, and structs
    // not in exceptions list). Skip builtin namespaces so their positions
    // (from the embedded nprpc_base IDL) do not leak into the user document.
    index_namespace_types(ctx_.nm_cur()->root());

    // Finalize (sort) the index
    index_.finalize();
  }

private:
  void add_named(void* node,
                 PositionIndex::NodeType type,
                 const AstNodeWithPosition* named)
  {
    if (!named)
      return;
    if (named->name_range.is_valid()) {
      index_.add(node, type, named->name_range.start.line,
                 named->name_range.start.column, named->name_range.end.line,
                 named->name_range.end.column);
      return;
    }
    if (named->range.start.line > 0) {
      index_.add(node, type, named->range.start.line, named->range.start.column,
                 named->range.end.line, named->range.end.column);
    }
  }

  void index_namespace_types(Namespace* ns)
  {
    if (!ns || ns->is_builtin())
      return;

    // Index all types in this namespace
    for (const auto& [name, type] : ns->types()) {
      index_type(type);
    }

    // Recursively index child namespaces
    for (auto* child : ns->children()) {
      index_namespace_types(child);
    }
  }

  void index_type(AstTypeDecl* type)
  {
    if (!type)
      return;

    using FieldType = npidl::FieldType;

    switch (type->id) {
      // case FieldType::Optional: {
      //     auto* opt_type = static_cast<AstWrapType*>(type)->type;
      //     // Index the optional type itself
      //     if (has_position(opt_type)) {
      //         index_.add(
      //             opt_type,
      //             PositionIndex::NodeType::Optional,
      //             opt_type->range.start.line,
      //             opt_type->range.start.column,
      //             opt_type->range.end.line,
      //             opt_type->range.end.column
      //         );
      //     }
      //     // Also index the wrapped type
      //     index_type(opt_type->type);
      //     break;
      // }

    case FieldType::Alias: {
      auto* alias = static_cast<AstAliasDecl*>(type);
      add_named(alias, PositionIndex::NodeType::Alias, alias);
      break;
    }
    case FieldType::Enum: {
      auto* e = static_cast<AstEnumDecl*>(type);
      add_named(e, PositionIndex::NodeType::Enum, e);
      const auto n = std::min(e->items.size(), e->item_name_ranges.size());
      for (size_t i = 0; i < n; ++i) {
        const auto& r = e->item_name_ranges[i];
        if (!r.is_valid())
          continue;
        index_.add(e, PositionIndex::NodeType::EnumValue, r.start.line,
                   r.start.column, r.end.line, r.end.column);
      }
      break;
    }
    case FieldType::Struct: {
      auto* s = static_cast<AstStructDecl*>(type);
      // Only index if not already indexed (exceptions are indexed
      // separately)
      if (has_position(s) && !s->is_exception()) {
        index_struct(s, false);
      }
      break;
    }
    case FieldType::Interface: {
      // Interfaces are indexed separately, skip
      break;
    }
    default:
      // Other types (fundamental, vector, etc.) don't have position info
      break;
    }
  }

  void index_interface(AstInterfaceDecl* ifs)
  {
    add_named(ifs, PositionIndex::NodeType::Interface, ifs);

    // Index functions within interface
    for (auto* fn : ifs->fns) {
      index_function(fn);
    }
  }

  void index_struct(AstStructDecl* s, bool is_exception = false)
  {
    if (s && s->is_builtin)
      return;

    add_named(s,
              is_exception ? PositionIndex::NodeType::Exception
                           : PositionIndex::NodeType::Struct,
              s);

    // Index fields within struct
    for (auto* field : s->fields) {
      index_field(field);
    }
  }

  void index_type_refs(std::vector<TypeRefSite>& sites)
  {
    for (auto& site : sites) {
      if (!site.range.is_valid())
        continue;
      if (site.is_keyword) {
        index_.add(&site, PositionIndex::NodeType::Keyword,
                   site.range.start.line, site.range.start.column,
                   site.range.end.line, site.range.end.column, false);
        continue;
      }
      if (!site.type)
        continue;
      auto nt = get_type_node_type(site.type);
      if (!nt)
        continue;
      index_.add(site.type, *nt, site.range.start.line, site.range.start.column,
                 site.range.end.line, site.range.end.column, false);
    }
  }

  void index_function(AstFunctionDecl* fn)
  {
    // Index the function *name* only. The full signature range overlaps
    // parameter tokens and made single-line methods highlight as one blob.
    add_named(fn, PositionIndex::NodeType::Function, fn);
    index_type_refs(fn->ret_type_refs);

    // Index parameters
    for (auto* arg : fn->args) {
      if (has_position(arg)) {
        index_.add(arg, PositionIndex::NodeType::Parameter,
                   arg->range.start.line, arg->range.start.column,
                   arg->range.end.line, arg->range.end.column);

        if (!arg->type_refs.empty()) {
          index_type_refs(arg->type_refs);
        } else if (arg->type_ref_range.is_valid() && arg->type) {
          auto nt = get_type_node_type(arg->type);
          if (nt) {
            index_.add(arg->type, *nt, arg->type_ref_range.start.line,
                       arg->type_ref_range.start.column,
                       arg->type_ref_range.end.line,
                       arg->type_ref_range.end.column, false);
          }
        }
      }
    }
  }

  void index_field(AstFieldDecl* field)
  {
    if (!has_position(field))
      return;

    index_.add(field, PositionIndex::NodeType::Field, field->range.start.line,
               field->range.start.column, field->range.end.line,
               field->range.end.column);

    if (!field->type_refs.empty()) {
      index_type_refs(field->type_refs);
    } else if (field->type_ref_range.is_valid() && field->type) {
      auto nt = get_type_node_type(field->type);
      if (nt) {
        index_.add(field->type, *nt, field->type_ref_range.start.line,
                   field->type_ref_range.start.column,
                   field->type_ref_range.end.line,
                   field->type_ref_range.end.column, false);
      }
    }
  }

  // Named user types only. Fundamentals/string/void must not be indexed as
  // Alias — that tag was a fallback and stole go-to-definition from enums.
  std::optional<PositionIndex::NodeType> get_type_node_type(AstTypeDecl* type)
  {
    using FieldType = npidl::FieldType;
    if (!type)
      return std::nullopt;
    switch (type->id) {
    case FieldType::Struct:
      return static_cast<AstStructDecl*>(type)->is_exception()
                 ? PositionIndex::NodeType::Exception
                 : PositionIndex::NodeType::Struct;
    case FieldType::Interface:
      return PositionIndex::NodeType::Interface;
    case FieldType::Enum:
      return PositionIndex::NodeType::Enum;
    case FieldType::Alias:
      return PositionIndex::NodeType::Alias;
    case FieldType::Optional:
      return PositionIndex::NodeType::Optional;
    default:
      return std::nullopt;
    }
  }

  // Helper to check if a node has position information
  template <typename T> bool has_position(T* node)
  {
    return node && node->range.start.line > 0;
  }
};

} // namespace npidl

// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include "ast.hpp"
#include "position_index.hpp"

namespace npidl {

// Builds a PositionIndex from a parsed Context
class PositionIndexBuilder
{
  PositionIndex& index_;
  Context& ctx_;

public:
  PositionIndexBuilder(PositionIndex& index, Context& ctx)
      : index_(index), ctx_(ctx)
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
    // not in exceptions list)
    index_namespace_types(ctx_.nm_cur()->root());

    // Finalize (sort) the index
    index_.finalize();
  }

private:
  void index_namespace_types(Namespace* ns)
  {
    if (!ns)
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
      if (has_position(alias)) {
        index_.add(alias, PositionIndex::NodeType::Alias,
                   alias->range.start.line, alias->range.start.column,
                   alias->range.end.line, alias->range.end.column);
      }
      break;
    }
    case FieldType::Enum: {
      auto* e = static_cast<AstEnumDecl*>(type);
      if (has_position(e)) {
        index_.add(e, PositionIndex::NodeType::Enum, e->range.start.line,
                   e->range.start.column, e->range.end.line,
                   e->range.end.column);
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
    if (!has_position(ifs))
      return;

    index_.add(ifs, PositionIndex::NodeType::Interface, ifs->range.start.line,
               ifs->range.start.column, ifs->range.end.line,
               ifs->range.end.column);

    // Index functions within interface
    for (auto* fn : ifs->fns) {
      index_function(fn);
    }
  }

  void index_struct(AstStructDecl* s, bool is_exception = false)
  {
    if (!has_position(s))
      return;

    index_.add(s,
               is_exception ? PositionIndex::NodeType::Exception
                            : PositionIndex::NodeType::Struct,
               s->range.start.line, s->range.start.column, s->range.end.line,
               s->range.end.column);

    // Index fields within struct
    for (auto* field : s->fields) {
      index_field(field);
    }
  }

  void index_function(AstFunctionDecl* fn)
  {
    if (!has_position(fn))
      return;

    index_.add(fn, PositionIndex::NodeType::Function, fn->range.start.line,
               fn->range.start.column, fn->range.end.line,
               fn->range.end.column);

    // Index parameters
    for (auto* arg : fn->args) {
      if (has_position(arg)) {
        index_.add(arg, PositionIndex::NodeType::Parameter,
                   arg->range.start.line, arg->range.start.column,
                   arg->range.end.line, arg->range.end.column);

        // Also index the parameter's type reference if it has a
        // position
        if (arg->type_ref_range.is_valid() && arg->type) {
          index_.add(
              arg->type, get_type_node_type(arg->type),
              arg->type_ref_range.start.line, arg->type_ref_range.start.column,
              arg->type_ref_range.end.line, arg->type_ref_range.end.column);
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

    // Also index the type reference if it has a position
    if (field->type_ref_range.is_valid() && field->type) {
      index_.add(
          field->type, get_type_node_type(field->type),
          field->type_ref_range.start.line, field->type_ref_range.start.column,
          field->type_ref_range.end.line, field->type_ref_range.end.column);
    }
  }

  // Helper to get NodeType for a type declaration
  PositionIndex::NodeType get_type_node_type(AstTypeDecl* type)
  {
    using FieldType = npidl::FieldType;
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
      // For fundamental types, wrapped types, etc. - shouldn't happen for
      // user types
      return PositionIndex::NodeType::Alias; // fallback
    }
  }

  // Helper to check if a node has position information
  template <typename T> bool has_position(T* node)
  {
    return node && node->range.start.line > 0;
  }
};

} // namespace npidl

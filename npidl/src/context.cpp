// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include "ast.hpp"

namespace npidl {

void Context::set_module_name(std::vector<std::string>&& name_parts)
{
  // When parsing builtins, create child namespaces under global root.
  // The builtins stay in global namespace structure.
  if (parsing_builtins_) {
    // Create namespace hierarchy as children of global root
    for (auto& part : name_parts) {
      auto existing = nm_cur_->find_child(part);
      if (existing) {
        nm_cur_ = existing;
      } else {
        nm_cur_ = nm_cur_->push(std::move(part));
      }
    }
    // Don't set module_name or module_level for builtins
    return;
  }

  // For user modules, create namespace hierarchy under global root,
  // but also set nm_root_ to point to the module's root for code generation.
  module_level = static_cast<int>(name_parts.size());
  for (auto& part : name_parts) {
    if (!module_name.empty())
      module_name += '_';
    module_name += part;
  }

  // Create module namespace hierarchy as child of global root
  // This keeps builtins (nprpc::) accessible from global root
  for (auto& part : name_parts) {
    auto existing = nm_cur_->find_child(part);
    if (existing) {
      nm_cur_ = existing;
    } else {
      nm_cur_ = nm_cur_->push(std::move(part));
    }
  }

  // Set nm_root_ to the innermost module namespace for code generation
  nm_root_ = nm_cur_;
}

AstStructDecl& Context::get_struct_by_path(std::string_view path) const
{
  auto split = [](std::string_view str, std::string_view delim) {
    std::vector<std::string_view> parts;
    size_t start = 0;
    while (start < str.size()) {
      size_t pos = str.find(delim, start);
      if (pos == std::string_view::npos) {
        parts.push_back(str.substr(start));
        break;
      }
      parts.push_back(str.substr(start, pos - start));
      start = pos + delim.size();
    }
    return parts;
  };

  auto parts = split(path, "::");
  Namespace* current = nm_global_;
  for (size_t i = 0; i < parts.size(); ++i) {
    const auto& part = parts[i];
    if (i == parts.size() - 1) {
      // Last part - look for struct type
      auto type = current->find_type(part, true);
      if (!type)
        throw std::runtime_error("Struct not found: " + std::string(path));
      if (type->id != FieldType::Struct)
        throw std::runtime_error("Type is not a struct: " + std::string(path));
      return *static_cast<AstStructDecl*>(type);
    } else {
      // Intermediate part - look for namespace
      auto child = current->find_child(part);
      if (!child)
        throw std::runtime_error("Namespace not found: " + std::string(part));
      current = child;
    }
  }
  throw std::runtime_error("Invalid path: " + std::string(path));
}

} // namespace npidl
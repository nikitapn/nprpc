// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include "ast.hpp"

#include <algorithm>
#include <cctype>

namespace npidl {

std::string Context::make_base_name(const std::filesystem::path& file_path)
{
  std::string base_name = file_path.filename().replace_extension().string();
  std::transform(base_name.begin(), base_name.end(), base_name.begin(),
                 [](char c) { return c == '.' ? '_' : ::tolower(c); });
  return base_name;
}

void Context::init_file_stack(std::filesystem::path file_path)
{
  std::string base_name = make_base_name(file_path);
  file_stack_.clear();
  file_stack_.push_back(FileContext{
      .file_path = std::move(file_path),
      .base_name = std::move(base_name),
      .namespace_at_entry = nm_global_,
  });
}

void Context::set_file_path(std::filesystem::path file_path)
{
  std::string base_name = make_base_name(file_path);
  if (file_stack_.empty()) {
    file_stack_.push_back(FileContext{
        .file_path = std::move(file_path),
        .base_name = std::move(base_name),
        .namespace_at_entry = nm_global_,
    });
    return;
  }
  file_stack_.front().file_path = std::move(file_path);
  file_stack_.front().base_name = std::move(base_name);
}

void Context::reset()
{
  std::filesystem::path path = "<in-memory>";
  if (!file_stack_.empty())
    path = file_stack_.front().file_path;
  reset(std::move(path));
}

void Context::reset(std::filesystem::path file_path)
{
  pool_.reset();
  delete nm_global_;

  nm_global_ = new Namespace(nullptr, "<root>");
  nm_root_ = nm_global_;
  nm_cur_ = nm_global_;
  exception_id_last = -1;
  parsing_builtins_ = false;
  module_name.clear();
  module_level = 0;
  structs_with_helpers_.clear();
  affa_list.clear();
  m_struct_n_ = 0;
  exceptions.clear();
  builtin_exceptions.clear();
  interfaces.clear();
  imports.clear();
  builtin_types_info_ = {};
  init_file_stack(std::move(file_path));
}

Context::~Context()
{
  pool_.reset();
  delete nm_global_;
  nm_global_ = nullptr;
}

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
      nm_cur_->mark_builtin();
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
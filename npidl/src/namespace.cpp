// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include "ast.hpp"
#include <expected>
#include <iostream>

namespace npidl {

Namespace* Namespace::find_common_ancestor(Namespace* a, Namespace* b)
{
  Namespace* common = nullptr;
  auto path_a = a->path(), path_b = b->path();
  auto it_a = path_a.begin(), it_b = path_b.begin();
  while (it_a != path_a.end() && it_b != path_b.end() && (*it_a) == (*it_b)) {
    common = *it_a;
    ++it_a;
    ++it_b;
  }
  return common;
}

int Namespace::substract(Namespace* from, Namespace* what)
{
  Namespace* common = find_common_ancestor(from, what);
  assert(common && "They should at least have the global namespace in common");

  if (common->name() == "<root>") {
    // Completly different namespaces - return full path of "what" including global namespace
    return 0;
  }

  int level = 0;
  while (common) {
    common = common->parent();
    ++level;
  }

  return level;
}

Namespace* Namespace::push(std::string&& s) noexcept
{
  children_.push_back(new Namespace(this, std::move(s)));
  return children_.back();
}

AstTypeDecl* Namespace::find_type(std::string_view str,
                                  bool only_this_namespace)
{
  if (auto it =
          std::find_if(types_.begin(), types_.end(),
                       [&str](auto const& pair) { return pair.first == str; });
      it != types_.end()) {
    return it->second;
  }

  if (only_this_namespace == false && parent())
    return parent()->find_type(str, false);

  return nullptr; // throw_error("Unknown type: \"" + str + "\"");
}

Namespace* Namespace::find_child(std::string_view str)
{
  if (auto it = std::find_if(children_.begin(), children_.end(),
                             [&str](auto nm) { return nm->name() == str; });
      it != children_.end()) {
    return *it;
  }
  return nullptr;
}

std::expected<bool, std::string> Namespace::add(const std::string& name, AstTypeDecl* type)
{
  if (std::find_if(std::begin(types_), std::end(types_),
      [&name](const auto& pair) { return pair.first == name; }) != std::end(types_))
  {
    const auto nm_name = this->name().empty() ? "<global>" : this->name();
    return std::unexpected("Type redefinition: \"" + name + "\" in namespace \"" + nm_name + "\"");
  }
  types_.push_back({name, type});
  return true;
}

std::string Namespace::construct_path(std::string delim, int start_level) const noexcept
{
  auto this_path = path();
  if (this_path.empty())
    return {};

  // Remove global namespace from path
  // if (this_path[0]->name() == "<root>")
  //   this_path.erase(this_path.begin());

  if (this_path.empty())
    return {};

  auto begin = this_path.begin();
  if (start_level != -1) {
    assert(std::distance(this_path.begin(), this_path.end()) >= start_level);
    std::advance(begin, start_level);
  }

  if (begin == this_path.end())
    return {};

  std::string result;
  if ((*begin)->name() == "<root>") {
    result = delim; // Start with global namespace
    ++begin;
  }

  for (auto it = begin; it != std::end(this_path); it = std::next(it))
    result += (*it)->name() + (it + 1 != std::end(this_path) ? delim : "");

  return result;
}

void Namespace::add_constant(std::string&& name, AstNumber&& number)
{
  if (std::find_if(std::begin(constants_), std::end(constants_),
                   [&name](const auto& pair) { return pair.first == name; }) !=
      std::end(constants_)) {
    throw std::runtime_error("constant redefinition");
  }
  constants_.emplace_back(std::move(name), std::move(number));
}

AstNumber* Namespace::find_constant(std::string_view name)
{
  auto it =
      std::find_if(std::begin(constants_), std::end(constants_),
                   [&name](const auto& pair) { return pair.first == name; });
  return it != std::end(constants_) ? &it->second : nullptr;
}

void Namespace::print_debug(int indent) const noexcept {
  std::string indent_str(indent, ' ');
  std::cout << indent_str << "Namespace: " << (name_.empty() ? "<root>" : name_) << "\n";
  for (const auto& [name, type] : types_) {
    std::cout << indent_str << "  Type: " << name << " (FieldType: " << static_cast<int>(type->id) << ")\n";
  }
  for (const auto& child : children_) {
    child->print_debug(indent + 2);
  }
}

} // namespace npidl
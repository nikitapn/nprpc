#include "ast.hpp"
#include <iostream>

std::vector<Namespace*> Namespace::path() noexcept {
    std::vector<Namespace*> result;
    auto ptr = this;
    while (ptr) {
      result.push_back(ptr);
      ptr = ptr->parent();
    }
    std::reverse(result.begin(), result.end());
    return result;
}

std::vector<const Namespace*> Namespace::path() const noexcept {
    std::vector<const Namespace*> result;
    auto ptr = this;
    while (ptr) {
      result.push_back(ptr);
      ptr = ptr->parent();
    }
    std::reverse(result.begin(), result.end());
    return result;
}

std::pair<Namespace*, int> Namespace::substract(Namespace* from, Namespace* what) {
  auto A = from->path();
  auto B = what->path();
  // A - B
  Namespace* common = nullptr;
  auto it_a = A.begin();
  auto it_b = B.begin();
  // Comparing namespaces by name for simplicity of the test
  // In real code we should compare by pointers
  int level = 0;
  while (it_a != A.end() && it_b != B.end() && (*it_a)->name() == (*it_b)->name()) {
    common = *it_b;
    ++level;
    ++it_a;
    ++it_b;
  }

  if (common == nullptr)
    return std::make_pair(*it_b, level);

  if(it_b == B.end() && it_a != A.end())
    return std::make_pair(nullptr, level);

  if (it_b == B.end() && it_a == A.end())
    return std::make_pair(nullptr, level);

  if (common && it_b != B.end())
    common = *it_b;

  return std::make_pair(common, level);
}

Namespace* Namespace::push(std::string&& s) noexcept {
  children_.push_back(new Namespace(this, std::move(s)));
  return children_.back();
}

AstTypeDecl* Namespace::find_type(const std::string& str, bool only_this_namespace) {
  if (auto it = std::find_if(types_.begin(), types_.end(),
    [&str](auto const& pair) { return pair.first == str; }); it != types_.end())
  {
    return it->second;
  }

  if (only_this_namespace == false && parent())
    return parent()->find_type(str, false);

  return nullptr; // throw_error("Unknown type: \"" + str + "\"");
}

Namespace* Namespace::find_child(const std::string& str) {
  if (auto it = std::find_if(children_.begin(), children_.end(),
    [&str](auto nm) {return nm->name() == str; }); it != children_.end()) {
    return *it;
  }
  return nullptr;
}

void Namespace::add(const std::string& name, AstTypeDecl* type) {
  if (std::find_if(std::begin(types_), std::end(types_),
    [&name](const auto& pair) { return pair.first == name; }) != std::end(types_))
  {
    throw "type redefinition";
  }
  types_.push_back({ name, type });
}

std::string Namespace::construct_path(std::string delim, int level) const noexcept {
  auto this_path = path();
  if (this_path.empty())
    return {};

  // Remove global namespace from path
  if (this_path[0]->name().empty())
    this_path.erase(this_path.begin());

  if (this_path.empty())
    return {};

  auto begin = this_path.begin();
  if (level != -1) {
    assert(std::distance(this_path.begin(), this_path.end()) >= level);
    std::advance(begin, level);
  }

  if (begin == this_path.end())
    return {};

  std::string result;
  for (auto it = begin; it != std::end(this_path); it = std::next(it)) {
    auto ptr = *it;
    result = (result.empty() ? "" : result + delim) + ptr->name();
  }
  return result;
}

void Namespace::add_constant(std::string&& name, AstNumber&& number) {
  if (std::find_if(std::begin(constants_), std::end(constants_),
    [&name](const auto& pair) { return pair.first == name; }) != std::end(constants_))
  {
    throw std::runtime_error("constant redefinition");
  }
  constants_.emplace_back(std::move(name), std::move(number));
}

AstNumber* Namespace::find_constant(const std::string& name) {
  auto it = std::find_if(std::begin(constants_), std::end(constants_),
    [&name](const auto& pair) { return pair.first == name; });
  return it != std::end(constants_) ? &it->second : nullptr;
}
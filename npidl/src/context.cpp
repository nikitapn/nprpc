#include "ast.hpp"

namespace npidl {

void Context::set_module_name(std::vector<std::string>&& name_parts) {
  // When parse sees module declaration, it changes module name
  // and also root namespace name. Module name is optional,
  // so it can be empty.
  module_level = static_cast<int>(name_parts.size());
  for (auto& part : name_parts) {
    if (!module_name.empty())
      module_name += '_';
    module_name += part;
  }

  if (name_parts.size() == 1) {
    nm_root_->change_name(std::move(name_parts[0]));
  } else {
    Namespace* prev = nullptr;
    for (size_t i = 0; i < name_parts.size() - 1; ++i) {
      auto temp = new Namespace(nullptr, std::move(name_parts[i]));
      if (prev)
        prev->push(temp);
      prev = temp;
    }
    prev->push(nm_root_);
    nm_root_->change_name(std::move(*name_parts.rbegin()));
  }
}

} // namespace npidl
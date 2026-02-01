// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include "builder.hpp"

namespace npidl::builders {

void Builder::emit_arguments_structs(std::function<void(AstStructDecl*)> emitter)
{
  always_full_namespace(true);
  for (auto& [unused, s] : ctx_->affa_list)
    emitter(s);
  always_full_namespace(false);
}

} // namespace npidl::builders

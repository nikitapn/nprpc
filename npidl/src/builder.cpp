// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include "builder.hpp"
#include "ast.hpp"

namespace npidl::builders {

void Builder::emit_arguments_structs(std::function<void(AstStructDecl*)> emitter)
{
  always_full_namespace(true);
  for (auto& [unused, s] : ctx_->affa_list)
    emitter(s);
  always_full_namespace(false);
}

void BuildGroup::generate_argument_structs(AstInterfaceDecl* ifs)
{
  for (auto& fn : ifs->fns)
    args_builder_.make_arguments_structs(fn);
}

} // namespace npidl::builders

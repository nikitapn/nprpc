// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include "arguments_builder.hpp"
#include "utils.hpp"
#include <algorithm>

namespace npidl::builders {

void ArgumentsStructBuilder::make_arguments_structs(AstFunctionDecl* fn)
{
  if (fn->arguments_structs_have_been_made)
    return;

  auto& in_args = fn->in_args;
  auto& out_args = fn->out_args;

  // Note: Streaming functions don't have traditional return values - they
  // return a StreamReader on the client side, not output parameters.
  if (!fn->is_void() && !fn->is_stream) {
    auto ret = new AstFunctionArgument();
    ret->name = "ret_val";
    ret->modifier = ArgumentModifier::Out;
    ret->type = fn->ret_value;
    out_args.push_back(ret);
  }

  for (auto arg : fn->args) {
    if (arg->modifier == ArgumentModifier::In)
      in_args.push_back(arg);
    else
      out_args.push_back(arg);
  }

  fn->in_s = make_struct(fn, in_args);
  fn->out_s = make_struct(fn, out_args);

  fn->arguments_structs_have_been_made = true;
}

AstStructDecl* ArgumentsStructBuilder::make_struct(
    AstFunctionDecl* fn,
    std::vector<AstFunctionArgument*>& args)
{
  if (args.empty())
    return nullptr;

  auto s = new AstStructDecl();
  s->name = ctx_->current_file() + "_M" + std::to_string(++ctx_->m_struct_n_);
  s->exception_id = -1; // Mark as non-exception

  std::transform(args.begin(), args.end(), std::back_inserter(s->fields),
                 [ix = 0, s, fn](AstFunctionArgument* arg) mutable {
                   auto f = new AstFieldDecl();
                   f->name = "_" + std::to_string(++ix);
                   f->type = arg->type;
                   s->flat &= is_flat(arg->type);
                   f->function_argument = true;
                   f->input_function_argument = (arg->modifier == ArgumentModifier::In);
                   f->function_name = fn->name;
                   f->function_argument_name = arg->name;
                   return f;
                 });

  calc_struct_size_align(s);
  auto const id = s->get_function_struct_id();

  if (auto p = ctx_->affa_list.get(id); p) {
    --ctx_->m_struct_n_;
    delete s;
    s = p;
  } else {
    ctx_->affa_list.put(id, s);
  }

  return s;
}

} // namespace npidl::builders

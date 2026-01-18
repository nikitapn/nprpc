// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include "builder.hpp"

namespace npidl::builders {
// Null builder that doesn't generate any output (for LSP parsing)
// May be not used at all in the end, but kept for completeness
class NullBuilder : public Builder
{
public:
  NullBuilder(Context* ctx)
      : Builder(ctx)
  {
  }

  void finalize() override {}
  void emit_namespace_begin() override {}
  void emit_namespace_end() override {}
  void emit_interface(AstInterfaceDecl*) override {}
  void emit_struct(AstStructDecl*) override {}
  void emit_exception(AstStructDecl*) override {}
  void emit_enum(AstEnumDecl*) override {}
  void emit_constant(const std::string&, AstNumber*) override {}
  void emit_using(AstAliasDecl*) override {}

  Builder* clone(Context* ctx) const override { return new NullBuilder(ctx); }
};
} // namespace npidl::builders
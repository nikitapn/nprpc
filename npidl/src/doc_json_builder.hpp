// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include "builder.hpp"
#include <filesystem>
#include <sstream>

namespace npidl::builders {

// Writes `<file>.doc.json`: every declaration of an IDL file with its `///`
// documentation and its signature spelled in IDL, for documentation tooling.
// The generated-code builders carry the same docs into C++/TS/Swift; this is
// the language-neutral copy.
class DocJsonBuilder : public Builder
{
  std::filesystem::path out_dir_;
  std::ostringstream decls_;
  bool first_decl_ = true;

  // Opens the next element of the "declarations" array with the fields every
  // declaration shares; the caller adds its own fields and closes the object.
  void begin_decl(std::string_view kind, const AstNodeWithPosition& node);

public:
  void emit_constant(const std::string& name, AstNumber* number) override;
  void emit_struct(AstStructDecl* s) override;
  void emit_exception(AstStructDecl* s) override;
  void emit_namespace_begin() override {}
  void emit_namespace_end() override {}
  void emit_interface(AstInterfaceDecl* ifs) override;
  void emit_using(AstAliasDecl* u) override;
  void emit_enum(AstEnumDecl* e) override;
  void emit_variant(AstVariantDecl* v) override;
  void finalize() override;

  Builder* clone(Context* ctx) const override
  {
    return new DocJsonBuilder(ctx, out_dir_);
  }

  DocJsonBuilder(Context* ctx, std::filesystem::path out_dir)
      : Builder(ctx)
      , out_dir_(std::move(out_dir))
  {
  }
};

} // namespace npidl::builders

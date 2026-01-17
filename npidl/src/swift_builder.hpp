// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include "builder.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>

namespace npidl::builders {

class SwiftBuilder : public Builder
{
public:
  struct _ns {
    const SwiftBuilder& builder;
    Namespace* nm;
  };

private:
  friend std::ostream& operator<<(std::ostream&, const SwiftBuilder::_ns&);

  std::filesystem::path out_dir_;
  std::stringstream out;
  
  // Helper methods for type emission
  void emit_type(AstTypeDecl* type, std::ostream& os);
  void emit_fundamental_type(TokenId id, std::ostream& os);
  void emit_parameter_type(AstFunctionArgument* arg, std::ostream& os);
  
  // Struct/Exception generation
  void emit_struct2(AstStructDecl* s, bool is_exception);
  void emit_field(AstFieldDecl* f, std::ostream& os);
  
  // Interface/Protocol generation
  void emit_protocol(AstInterfaceDecl* ifs);
  void emit_client_proxy(AstInterfaceDecl* ifs);
  void emit_servant_base(AstInterfaceDecl* ifs);
  void emit_swift_trampolines(AstInterfaceDecl* ifs);  // C trampolines for Swift servants
  
  // Marshalling support
  void emit_marshal_function(AstStructDecl* s);
  void emit_unmarshal_function(AstStructDecl* s);
  
  // Utilities
  _ns ns(Namespace* nm) const;
  std::string swift_type_name(const std::string& name) const;
  std::string swift_method_name(const std::string& name) const;
  
  auto emit_type(AstTypeDecl* type)
  {
    return OstreamWrapper{
        [type, this](std::ostream& os) { this->emit_type(type, os); }};
  }

public:
  // C++ bridge generation (called from CppBuilder)
  static void emit_cpp_swift_bridge_header(AstInterfaceDecl* ifs, std::ostream& oh, Context* ctx);
  static void emit_cpp_swift_bridge_impl(AstInterfaceDecl* ifs, std::ostream& oc, Context* ctx);
  static void emit_cpp_marshal_functions(AstStructDecl* s, std::ostream& oc, Context* ctx);
  virtual void emit_constant(const std::string& name, AstNumber* number) override;
  virtual void emit_struct(AstStructDecl* s) override;
  virtual void emit_exception(AstStructDecl* s) override;
  virtual void emit_namespace_begin() override;
  virtual void emit_namespace_end() override;
  virtual void emit_interface(AstInterfaceDecl* ifs) override;
  virtual void emit_using(AstAliasDecl* u) override;
  virtual void emit_enum(AstEnumDecl* e) override;
  virtual void finalize() override;
  virtual Builder* clone(Context* ctx) const override
  {
    return new SwiftBuilder(ctx, out_dir_);
  }

  SwiftBuilder(Context* ctx, std::filesystem::path out_dir);
};

} // namespace npidl::builders

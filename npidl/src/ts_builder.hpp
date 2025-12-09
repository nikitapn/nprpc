// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include "builder.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>

namespace npidl::builders {

class TSBuilder : public Builder
{
public:
  struct _ns {
    const TSBuilder& builder;
    Namespace* nm;
  };

private:
  friend std::ostream& operator<<(std::ostream&, const TSBuilder::_ns&);

  std::filesystem::path out_dir_;
  std::stringstream out;
  std::stringstream spans;

  void emit_parameter_type_for_proxy_call_r(AstTypeDecl* type,
                                            std::ostream& os,
                                            bool input);
  void emit_parameter_type_for_proxy_call(AstFunctionArgument* arg,
                                          std::ostream& os);
  void emit_parameter_type_for_servant(AstFunctionArgument* arg,
                                       std::ostream& os);

  void assign_from_ts_type(AstTypeDecl* type,
                           std::string op1,
                           std::string op2,
                           bool from_iterator = false);
  void assign_from_flat_type(AstTypeDecl* type,
                             std::string op1,
                             std::string op2,
                             bool from_iterator = false,
                             bool top_object = true,
                             bool direct = false);

  void emit_type(AstTypeDecl* type, std::ostream& os);
  void emit_struct2(AstStructDecl* s, bool is_exception);
  void emit_struct_helpers();
  void emit_variable(AstTypeDecl* type, std::string name, std::ostream& os);

  // New marshalling approach
  void emit_heap_views();
  void emit_marshal_function(AstStructDecl* s);
  void emit_unmarshal_function(AstStructDecl* s);
  void emit_field_marshal(AstFieldDecl* f,
                          int& offset,
                          const std::string& data_name,
                          bool is_generated_arg_struct = false);
  void emit_field_unmarshal(AstFieldDecl* f,
                            int& offset,
                            const std::string& result_name,
                            bool has_endpoint = false);

  _ns ns(Namespace* nm) const;

  auto emit_type(AstTypeDecl* type)
  {
    return OstreamWrapper{
        [type, this](std::ostream& os) { this->emit_type(type, os); }};
  }

  std::string get_helper_function_name(AstStructDecl* s, bool from_flat) const
  {
    std::stringstream ss;
    ss << "helpers."
       << (from_flat ? "assign_from_flat_" : "assign_from_ts_") + s->name;
    return ss.str();
  }

public:
  virtual void emit_constant(const std::string& name, AstNumber* number);
  virtual void emit_struct(AstStructDecl* s);
  virtual void emit_exception(AstStructDecl* s);
  virtual void emit_namespace_begin();
  virtual void emit_namespace_end();
  virtual void emit_interface(AstInterfaceDecl* ifs);
  virtual void emit_using(AstAliasDecl* u);
  virtual void emit_enum(AstEnumDecl* e);
  virtual void finalize();
  virtual Builder* clone(Context* ctx) const
  {
    return new TSBuilder(ctx, out_dir_);
  }

  TSBuilder(Context* ctx, std::filesystem::path out_dir);
};

} // namespace npidl::builders
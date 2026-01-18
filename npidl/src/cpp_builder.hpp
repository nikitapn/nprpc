// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include "builder.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>

namespace npidl::builders {

class CppBuilder : public Builder
{
public:
  struct _ns {
    CppBuilder& builder;
    Namespace* nm;
  };

private:
  friend std::ostream& operator<<(std::ostream&, const CppBuilder::_ns&);

  std::filesystem::path out_path_;

  std::stringstream oh;
  std::stringstream ocpp;
  std::stringstream oc;

  std::unordered_map<AstFunctionDecl*, std::string> proxy_arguments_;
  std::string export_macro_name_;

  BlockDepth bd;

  void emit_parameter_type_for_proxy_call_r(AstTypeDecl* type,
                                            std::ostream& os,
                                            bool input);
  void emit_parameter_type_for_proxy_call(AstFunctionArgument* arg,
                                          std::ostream& os);

  void emit_parameter_type_for_servant_callback_r(AstTypeDecl* type,
                                                  std::ostream& os,
                                                  const bool input);
  void emit_parameter_type_for_servant_callback(AstFunctionArgument* arg,
                                                std::ostream& os);

  void assign_from_cpp_type(AstTypeDecl* type,
                            std::string op1,
                            std::string op2,
                            std::ostream& os,
                            bool from_iterator = false,
                            bool top_type = false,
                            bool direct_type = false);
  void assign_from_flat_type(AstTypeDecl* type,
                             std::string op1,
                             std::string op2,
                             std::ostream& os,
                             bool from_iterator = false,
                             bool top_object = false);

  void emit_type(AstTypeDecl* type, std::ostream& os);
  void emit_flat_type(AstTypeDecl* type, std::ostream& os);
  void emit_direct_type(AstTypeDecl* type, std::ostream& os);

  void emit_accessors(const std::string& flat_name,
                      AstFieldDecl* f,
                      std::ostream& os);

  enum class Target { Regular, Exception, FunctionArgument };
  void emit_struct2(AstStructDecl* s, std::ostream& os, Target target);
  void emit_helpers();
  void emit_struct_helpers();
  void emit_safety_checks();
  void emit_safety_checks_r(AstTypeDecl* type,
                            std::string op,
                            std::ostream& os,
                            bool from_iterator = false,
                            bool top_type = false);

  void proxy_call(AstFunctionDecl* fn);
  void proxy_async_call(AstFunctionDecl* fn);
  void proxy_udp_call(AstFunctionDecl* fn); // Fire-and-forget UDP call
  void
  proxy_unreliable_call(AstFunctionDecl* fn); // Fire-and-forget for non-UDP
                                              // (e.g., QUIC DATAGRAM)
  void proxy_udp_reliable_call(
      AstFunctionDecl* fn); // Blocking reliable UDP call with ACK
  void proxy_udp_reliable_async_call(
      AstFunctionDecl* fn); // Async reliable UDP call with ACK
  void proxy_stream_call(AstFunctionDecl* fn); // Streaming call
  std::string_view proxy_arguments(AstFunctionDecl* fn);
  static void emit_function_arguments(
      AstFunctionDecl* fn,
      std::ostream& os,
      std::function<void(AstFunctionArgument*, std::ostream& os)> emitter);

  _ns ns(Namespace* nm);

  auto emit_type(AstTypeDecl* type)
  {
    return OstreamWrapper{
        [type, this](std::ostream& os) { this->emit_type(type, os); }};
  }

  auto emit_flat_type(AstTypeDecl* type)
  {
    return OstreamWrapper{
        [type, this](std::ostream& os) { this->emit_flat_type(type, os); }};
  }

public:
  virtual void emit_constant(const std::string& name, AstNumber* number) override;
  virtual void emit_struct(AstStructDecl* s) override;
  virtual void emit_exception(AstStructDecl* s) override;
  virtual void emit_using(AstAliasDecl* u) override;
  virtual void emit_enum(AstEnumDecl* e) override;
  virtual void emit_namespace_begin() override;
  virtual void emit_namespace_end() override;
  virtual void emit_interface(AstInterfaceDecl* ifs) override;
  virtual void finalize() override;
  virtual Builder* clone(Context* ctx) const override
  {
    return new CppBuilder(ctx, out_path_);
  }

  CppBuilder(Context* ctx, std::filesystem::path out_path);
};

} // namespace npidl::builders
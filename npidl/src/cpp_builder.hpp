// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include "builder.hpp"
#include <filesystem>
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
  std::vector<AstFunctionDecl*> stream_codec_fns_; // collected for nprpc_stream codecs at file end

  BlockDepth bd;

  void emit_parameter_type_for_proxy_call_r(AstTypeDecl* type,
                                            std::ostream& os,
                                            bool input);

  void emit_parameter_type_for_proxy_call(AstFunctionArgument* arg,
                                          std::ostream& os);

  void emit_parameter_type_for_proxy_call_direct(AstFunctionArgument* arg,
                                                 std::ostream& os);

  /// Emits the proxy-side zero-copy type for an 'out direct' argument type:
  /// OwnedSpan<T> for vector<primitive/enum>, OwnedDirect<TD> for all others.
  void emit_owned_direct_type(AstTypeDecl* type, std::ostream& os);

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

  /// Shared output-side logic for proxy_call
  /// Emits the buf_ptr / Direct-out setup and all out-arg assignments,
  /// including OwnedSpan/OwnedDirect for 'direct' annotated arguments.
  void emit_proxy_out_assignments(AstFunctionDecl* fn, bool use_co_return = false);

  void emit_accessors(const std::string& flat_name,
                      AstFieldDecl* f,
                      std::ostream& os);

  void emit_struct2(AstStructDecl* s, std::ostream& os, Target target);
  void emit_helpers();
  void emit_struct_helpers();
  void emit_safety_checks();
  void emit_safety_checks_r(AstTypeDecl* type,
                            std::string op,
                            std::ostream& os,
                            bool from_iterator = false,
                            bool top_type = false);

  // Emits the nprpc_stream::deserialize<T> specialisation for a stream
  // method whose element type is a non-fundamental struct/complex type.
  // Called once per such stream function from emit_interface.
  void emit_stream_deserialize(AstFunctionDecl* fn);
  void emit_stream_serialize(AstFunctionDecl* fn);

  void proxy_call(AstFunctionDecl* fn);
  void proxy_call_coro(AstFunctionDecl* fn);  // coroutine variant
  void proxy_async_call(AstFunctionDecl* fn);
  void proxy_unreliable_call(AstFunctionDecl* fn); // Fire-and-forget
  void proxy_stream_call(AstFunctionDecl* fn); // Server-streaming call
  void proxy_client_stream_call(AstFunctionDecl* fn);
  void proxy_bidi_stream_call(AstFunctionDecl* fn);
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
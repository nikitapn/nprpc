// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include <algorithm>
#include <cassert>
#include <cctype>
#include <ios>
#include <iostream>
#include <set>
#include <fstream>
#include <string_view>

#include <boost/container/small_vector.hpp>

#include "cpp_builder.hpp"
#include "utils.hpp"

// clang-format off

namespace npidl::builders {

using std::placeholders::_1;
using std::placeholders::_2;

namespace {

std::string make_safety_check_name(AstStructDecl* s)
{
  auto name = s->get_function_struct_id();
  for (auto& ch : name) {
    if (!std::isalnum(static_cast<unsigned char>(ch)))
      ch = '_';
  }
  return name;
}

} // namespace

std::ostream& operator<<(std::ostream& os, const CppBuilder::_ns& ns)
{
  if (ns.builder.always_full_namespace_) {
    os << ns.nm->construct_path("::", 0) << "::";
    return os;
  }

  int level = Namespace::substract(ns.builder.ctx_->nm_cur(), ns.nm);
  const auto path = ns.nm->construct_path("::", level);
  if (path.size() == 0 || (path.size() == 2 && path[0] == ':' && path[1] == ':')) {
    return os;
  }

  return os << path << "::";
}

static std::string_view fundamental_to_cpp(TokenId id)
{
  using namespace std::string_view_literals;
  switch (id) {
  case TokenId::Boolean:
    return "bool"sv;
  case TokenId::Int8:
    return "int8_t"sv;
  case TokenId::UInt8:
    return "uint8_t"sv;
  case TokenId::Int16:
    return "int16_t"sv;
  case TokenId::UInt16:
    return "uint16_t"sv;
  case TokenId::Int32:
    return "int32_t"sv;
  case TokenId::UInt32:
    return "uint32_t"sv;
  case TokenId::Int64:
    return "int64_t"sv;
  case TokenId::UInt64:
    return "uint64_t"sv;
  case TokenId::Float32:
    return "float"sv;
  case TokenId::Float64:
    return "double"sv;
  default:
    assert(false);
    return ""sv;
  }
}

static std::string_view fundamental_to_flat(TokenId id)
{
  using namespace std::string_view_literals;
  switch (id) {
  case TokenId::Boolean:
    return "::nprpc::flat::Boolean"sv;
  default:
    return fundamental_to_cpp(id);
  }
}

static AstTypeDecl* canonical_stream_codec_type(AstTypeDecl* type)
{
  while (type != nullptr && type->id == FieldType::Alias)
    type = calias(type)->get_real_type();
  return type;
}

void CppBuilder::emit_type(AstTypeDecl* type, std::ostream& os)
{
  switch (type->id) {
  case FieldType::Fundamental:
    os << fundamental_to_cpp(cft(type)->token_id);
    break;
  case FieldType::Struct:
    os << ns(cflat(type)->nm) << cflat(type)->name;
    break;
  case FieldType::Vector:
    os << "std::vector<";
    emit_type(cvec(type)->type, os);
    os << ">";
    break;
  case FieldType::String:
    os << "std::string";
    break;
  case FieldType::Array:
    os << "std::array<";
    emit_type(car(type)->type, os);
    os << ',' << car(type)->length << '>';
    break;
  case FieldType::Optional:
    os << "std::optional<";
    emit_type(copt(type)->type, os);
    os << ">";
    break;
  case FieldType::Void:
    os << "void";
    break;
  case FieldType::Object:
    os << "::nprpc::ObjectId";
    break;
  case FieldType::Alias:
    os << ns(calias(type)->nm) << calias(type)->name;
    break;
  case FieldType::Enum:
    os << ns(cenum(type)->nm) << cenum(type)->name;
    break;
  case FieldType::Variant:
    os << ns(cvar(type)->nm) << cvar(type)->name;
    break;
  default:
    assert(false);
  }
}

void CppBuilder::emit_flat_type(AstTypeDecl* type, std::ostream& os)
{
  switch (type->id) {
  case FieldType::Fundamental:
    os << fundamental_to_flat(cft(type)->token_id);
    break;
  case FieldType::Struct:
    os << ns(cflat(type)->nm) << "flat::" << cflat(type)->name;
    break;
  case FieldType::Vector:
    os << "::nprpc::flat::Vector<";
    emit_flat_type(cvec(type)->type, os);
    os << ">";
    break;
  case FieldType::String:
    os << "::nprpc::flat::String";
    break;
  case FieldType::Array:
    os << "::nprpc::flat::Array<";
    emit_flat_type(car(type)->type, os);
    os << ',' << car(type)->length << '>';
    break;
  case FieldType::Optional:
    os << "::nprpc::flat::Optional<";
    emit_flat_type(copt(type)->type, os);
    os << ">";
    break;
  case FieldType::Object:
    os << "::nprpc::detail::flat::ObjectId";
    break;
  case FieldType::Alias: {
    emit_flat_type(calias(type)->get_real_type(), os);
    break;
  }
  case FieldType::Enum:
    os << ns(cenum(type)->nm) << cenum(type)->name;
    break;
  case FieldType::Variant:
    os << ns(cvar(type)->nm) << "flat::" << cvar(type)->name;
    break;
  default:
    assert(false);
  }
}

CppBuilder::_ns CppBuilder::ns(Namespace* nm) { return {*this, nm}; }

void CppBuilder::emit_parameter_type_for_proxy_call_r(AstTypeDecl* type,
                                                      std::ostream& os,
                                                      bool input)
{
  switch (type->id) {
  case FieldType::Fundamental:
    os << fundamental_to_cpp(cft(type)->token_id);
    break;
  case FieldType::Struct:
    os << ns(cflat(type)->nm) << cflat(type)->name;
    break;
  case FieldType::Vector:
    if (input) {
      os << "::nprpc::flat::Span<const ";
      emit_parameter_type_for_proxy_call_r(cvec(type)->type, os, input);
      os << ">";
    } else {
      os << "std::vector<";
      emit_parameter_type_for_proxy_call_r(cvec(type)->type, os, input);
      os << ">";
    }
    break;
  case FieldType::String:
    os << "std::string";
    break;
  case FieldType::Array:
    os << "std::array<";
    emit_parameter_type_for_proxy_call_r(car(type)->type, os, input);
    os << ',' << car(type)->length << '>';
    break;
  case FieldType::Optional:
    os << "std::optional<";
    emit_parameter_type_for_proxy_call_r(copt(type)->type, os, input);
    os << ">";
    break;
  case FieldType::Enum:
    os << ns(cenum(type)->nm) << cenum(type)->name;
    break;
  case FieldType::Alias:
    os << ns(calias(type)->nm) << calias(type)->name;
    break;
  case FieldType::Void:
    os << "void";
    break;
  case FieldType::Object:
    if (input)
      os << "ObjectId";
    else
      os << "Object*";
    break;
  case FieldType::Variant:
    os << ns(cvar(type)->nm) << cvar(type)->name;
    break;
  default:
    assert(false);
  }
}

void CppBuilder::emit_parameter_type_for_proxy_call(AstFunctionArgument* arg, std::ostream& os)
{
  const bool input = (arg->modifier == ArgumentModifier::In);

  if (input && arg->type->id != FieldType::Fundamental && arg->type->id != FieldType::Vector)
    os << "const ";

  emit_parameter_type_for_proxy_call_r(arg->type, os, input);

  if (!input || (arg->type->id != FieldType::Fundamental && arg->type->id != FieldType::Vector))
    os << '&';
}

void CppBuilder::emit_owned_direct_type(AstTypeDecl* type, std::ostream& os)
{
  // Resolve alias to its underlying type before any classification.
  if (type->id == FieldType::Alias)
    type = calias(type)->get_real_type();

  // A bare fundamental or enum has no flat-buffer Direct type and is tiny —
  // zero-copy makes no sense.  Warn and fall back to a plain C++ reference.
  if (type->id == FieldType::Fundamental || type->id == FieldType::Enum) {
    std::cerr << "npidl warning: 'direct' on a fundamental/enum out parameter "
                 "has no effect and will be treated as a regular 'out'.\n";
    emit_type(type, os);
    return;
  }

  // Primitive/enum vector → OwnedSpan (most common zero-copy case)
  if (type->id == FieldType::Vector || type->id == FieldType::Array) {
    auto wt = cwt(type)->real_type();
    if (wt->id == FieldType::Fundamental || wt->id == FieldType::Enum) {
      os << "::nprpc::flat::OwnedSpan<";
      emit_flat_type(wt, os);
      os << ">";
      return;
    }
  }
  // Everything else → OwnedDirect<TD> where TD is the corresponding _Direct type
  os << "::nprpc::flat::OwnedDirect<";
  emit_direct_type(type, os);
  os << ">";
}

void CppBuilder::emit_parameter_type_for_proxy_call_direct(AstFunctionArgument* arg, std::ostream& os)
{
  // Only called for 'out direct' arguments.
  assert(arg->modifier == ArgumentModifier::Out);
  emit_owned_direct_type(arg->type, os);
}

void CppBuilder::emit_parameter_type_for_servant_callback_r(AstTypeDecl* type,
                                                            std::ostream& os,
                                                            const bool input)
{
  switch (type->id) {
  case FieldType::Fundamental:
    os << fundamental_to_flat(cft(type)->token_id);
    break;
  case FieldType::Struct:
    emit_flat_type(type, os);
    os << "_Direct";
    break;
  case FieldType::Array:
  case FieldType::Vector: {
    auto wt = cwt(type)->real_type();
    if (input || type->id == FieldType::Array) {
      if (wt->id == FieldType::Fundamental || wt->id == FieldType::Enum) {
        os << "::nprpc::flat::Span<";
        emit_flat_type(wt, os);
        os << ">";
      } else if (wt->id == FieldType::Struct) {
        os << "::nprpc::flat::Span_ref<";
        emit_flat_type(wt, os);
        os << ", ";
        emit_flat_type(wt, os);
        os << "_Direct>";
      }
    } else {
      if (wt->id == FieldType::Fundamental || wt->id == FieldType::Enum) {
        os << "/*out*/::nprpc::flat::Vector_Direct1<";
        emit_parameter_type_for_servant_callback_r(wt, os, input);
        os << ">";
      } else if (wt->id == FieldType::Struct) {
        os << "/*out*/::nprpc::flat::Vector_Direct2<";
        emit_flat_type(wt, os);
        os << ", ";
        emit_parameter_type_for_servant_callback_r(wt, os, input);
        os << ">";
      }
    }
    break;
  }
  case FieldType::String:
    if (input) {
      os << "::nprpc::flat::Span<char>";
    } else {
      os << "::nprpc::flat::String_Direct1";
    }
    break;
  case FieldType::Optional:
    if (copt(type)->real_type()->id == FieldType::Struct) {
      os << "::nprpc::flat::Optional_Direct<" << emit_flat_type(copt(type)->type) << ", "
         << emit_flat_type(copt(type)->type) << "_Direct>";
    } else {
      os << "::nprpc::flat::Optional_Direct<";
      emit_flat_type(copt(type)->type, os);
      os << ">";
    }
    break;
  case FieldType::Object:
    if (input) {
      os << "::nprpc::Object*";
    } else {
      os << "::nprpc::detail::flat::ObjectId_Direct";
    }
    break;
  case FieldType::Enum:
    os << ns(cenum(type)->nm) << cenum(type)->name;
    break;
  case FieldType::Alias: {
    auto rt = calias(type)->get_real_type();
    if (!input) {
      if (rt->id == FieldType::Array || rt->id == FieldType::Vector) {
        // At least for now, only arrays/vectors need special handling
        emit_parameter_type_for_servant_callback_r(calias(type)->get_real_type(), os, input);
      } else {
        os << ns(calias(type)->nm) << calias(type)->name;
      }
    } else {
      if (rt->id == FieldType::Fundamental) {
        os << ns(calias(type)->nm) << calias(type)->name;
      } else {
        emit_parameter_type_for_servant_callback_r(calias(type)->get_real_type(), os, input);
      }
    }
    break;
  }
  case FieldType::Variant: {
    auto* v = cvar(type);
    os << ns(v->nm) << "flat::" << v->name << "_Direct";
    break;
  }
  case FieldType::Interface:
    assert(false);
    break;
  default:
    assert(false);
    break;
  }
}

void CppBuilder::emit_parameter_type_for_servant_callback(AstFunctionArgument* arg,
                                                          std::ostream& os)
{
  auto const input = arg->modifier == ArgumentModifier::In;

  emit_parameter_type_for_servant_callback_r(arg->type, os, input);

  // These are always passed as Direct types, so no need for reference
  if (!input &&
      arg->type->id != FieldType::Vector &&
      arg->type->id != FieldType::Array &&
      arg->type->id != FieldType::Object &&
      arg->type->id != FieldType::String &&
      arg->type->id != FieldType::Optional &&
      arg->type->id != FieldType::Struct &&
      arg->type->id != FieldType::Variant)
  {
    os << '&';
  }
}

void CppBuilder::emit_direct_type(AstTypeDecl* type, std::ostream& os)
{
  switch (type->id) {
  case FieldType::Fundamental:
    os << "void";
    break;

  case FieldType::String:
    os << "::nprpc::flat::String_Direct1";
    break;

  case FieldType::Array:
  case FieldType::Vector: {
    auto wt = cwt(type)->real_type();

    if (type->id == FieldType::Array)
      os << "::nprpc::flat::Array_Direct";
    else if (type->id == FieldType::Vector)
      os << "::nprpc::flat::Vector_Direct";

    auto const is_primitive = (wt->id == FieldType::Fundamental || wt->id == FieldType::Enum);

    os << (is_primitive ? '1' : '2');
    os << '<';
    emit_flat_type(wt, os);
    if (!is_primitive) {
      os << ',';
      emit_direct_type(wt, os);
    }
    os << '>';
    break;
  }

  case FieldType::Struct:
    os << ns(cflat(type)->nm) << "flat::" << cflat(type)->name << "_Direct";
    break;

  case FieldType::Optional: {
    auto wt = cwt(type)->real_type();
    os << "::nprpc::flat::Optional_Direct<";
    emit_flat_type(wt, os);
    os << ',';
    emit_direct_type(wt, os);
    os << '>';
    break;
  }

  default:
    assert(false);
    break;
  }
}

void CppBuilder::emit_accessors(const std::string& flat_name, AstFieldDecl* f, std::ostream& os)
{
  switch (f->type->id) {
  case FieldType::Fundamental: {
    auto type_name = fundamental_to_flat(cft(f->type)->token_id);
    os << "  const " << type_name << "& " << f->name << "() const noexcept { return base()." << f->name << ";}\n";
    os << "  " << type_name << "& " << f->name << "() noexcept { return base()." << f->name << ";}\n";
    break;
  }

  case FieldType::Struct:
    os << "  auto " << f->name << "() noexcept { return " << ns(cflat(f->type)->nm)
       << "flat::" << cflat(f->type)->name << "_Direct(buffer_, offset_ + offsetof(" << flat_name << ", " << f->name << ")); }\n";
    break;

  case FieldType::Vector:
    os << "  void " << f->name << "(std::uint32_t elements_size) { new (&base()." << f->name << ") ::nprpc::flat::Vector<";
    emit_flat_type(static_cast<AstWrapType*>(f->type)->type, os);
    os << ">(buffer_, elements_size); }\n";

    os << "  auto " << f->name << "_d() noexcept { return ";
    emit_direct_type(f->type, os);
    os << "(buffer_, offset_ + offsetof(" << flat_name << ", " << f->name << ")); }\n";
    [[fallthrough]];
  case FieldType::Array: {
    auto wt = cwt(f->type)->real_type();
    if (wt->id == FieldType::Fundamental || wt->id == FieldType::Enum) {
      os << "  auto " << f->name << "() noexcept { return (::nprpc::flat::Span<";
      emit_flat_type(wt, os);
      os << ">)base()." << f->name << "; }\n";
      os << "  const auto " << f->name << "() const noexcept { return (::nprpc::flat::Span<const ";
      emit_flat_type(wt, os);
      os << ">)base()." << f->name << "; }\n";
    } else if (wt->id == FieldType::Struct) {
      os << "  auto " << f->name << "() noexcept { return ::nprpc::flat::Span_ref<";
      emit_flat_type(wt, os);
      os << ", ";
      emit_direct_type(wt, os);
      os << ">(buffer_, base()." << f->name << ".range(buffer_.data().data())); }\n";
    }
    break;
  }

  case FieldType::String:
    os << "  void " << f->name << "(const char* str) { new (&base()." << f->name << ") ::nprpc::flat::String(buffer_, str); }\n";
    os << "  void " << f->name << "(const std::string& str) { new (&base()." << f->name << ") ::nprpc::flat::String(buffer_, str); }\n";
    os << "  auto " << f->name << "() noexcept { return (::nprpc::flat::Span<char>)base()." << f->name << "; }\n";
    os << "  auto " << f->name << "() const noexcept { return (::nprpc::flat::Span<const char>)base()." << f->name << "; }\n";
    os << "  auto " << f->name << "_d() noexcept { return ::nprpc::flat::String_Direct1(buffer_, offset_ + offsetof(" << flat_name << ", " << f->name << ")); }\n";
    break;

  case FieldType::Optional: {
    os << "  auto " << f->name << "() noexcept { return ";
    emit_direct_type(f->type, os);
    os << "(buffer_, offset_ + offsetof(" << flat_name << ", " << f->name << ")); }\n";
    break;
  }

  case FieldType::Object:
    os << "  auto " << f->name
       << "() noexcept { return ::nprpc::detail::flat::ObjectId_Direct(buffer_, offset_ + offsetof(" << flat_name << ", " << f->name << ")); }\n";
    break;

  case FieldType::Alias: {
    auto temp = std::make_unique<AstFieldDecl>();
    temp->name = f->name;
    temp->type = calias(f->type)->get_real_type();
    emit_accessors(flat_name, temp.get(), os);
    break;
  }

  case FieldType::Enum: {
    auto e = cenum(f->type);
    os << "  const " << ns(e->nm) << e->name << "& " << f->name
       << "() const noexcept { return base()." << f->name << ";}\n";
    os << "  " << ns(e->nm) << e->name << "& " << f->name << "() noexcept { return base()."
       << f->name << ";}\n";
    break;
  }
  case FieldType::Variant: {
    auto* v = cvar(f->type);
    os << "  auto " << f->name << "() noexcept { return "
       << ns(v->nm) << "flat::" << v->name
       << "_Direct(buffer_, offset_ + offsetof(" << flat_name << ", " << f->name << ")); }\n";
    break;
  }
  default:
    assert(false);
    break;
  }
}

void CppBuilder::assign_from_cpp_type(AstTypeDecl* type,
                                      std::string op1,
                                      std::string op2,
                                      std::ostream& os,
                                      bool from_iterator,
                                      bool top_type,
                                      bool /* direct_type */)
{
  using namespace std::string_view_literals;
  auto accessor = top_type ? "."sv : "()."sv;

  switch (type->id) {
  case FieldType::Fundamental:
    assert(top_type == false);
    os << bd << op1 << "() = " << op2 << ";\n";
    break;

  case FieldType::Struct: {
    auto s = cflat(type);
    if (s->flat) {
      os << bd << "memcpy(" << op1 << (top_type ? "" : "()") << ".__data(), &" << op2 << ", "
         << s->size << ");\n";
    } else {
      for (auto field : s->fields) {
        assign_from_cpp_type(field->type,
                             op1 + (top_type ? "." : (from_iterator ? "." : "().")) + field->name,
                             op2 + (from_iterator ? "->" : ".") + field->name, os);
      }
    }
    break;
  }

  case FieldType::Vector:
    if (top_type) {
      os << bd << op1 << ".length(static_cast<uint32_t>(" << op2 << ".size()));\n";
    } else {
      os << bd << op1 << "(static_cast<uint32_t>(" << op2 << ".size()));\n";
    }
    [[fallthrough]];
  case FieldType::Array: {
    auto wt = cwt(type)->type;
    // Vectors need parentheses to call the .data() method, while arrays don't
    const std::string_view vec_accessor =
      type->id == FieldType::Array ? accessor : "().";
    if (is_flat(wt)) {
      auto [size, align] = get_type_size_align(wt);
      os << bd << "memcpy(" << op1 << vec_accessor << "data(), " << op2 << ".data(), " << op2 << ".size() * "
         << size << ");\n";
    } else if (wt->id == FieldType::String) {
      os << bd << "{\n"
         << (bd += 1) << "auto vdir = " << op1 << "_d();\n"
         << bd << "auto it = " << op2 << ".begin();\n"
         << bd << "auto span = vdir();\n"
         << bd << "for (auto e : span) {\n";
      os << (bd += 1) << "e.length(it->size());\n";
      os << "e = *it;\n";

      os << bd << "++it;\n" << (bd -= 1) << "}\n" << (bd -= 1) << "}\n";
    } else {
      os << bd << "{\n"
         << (bd += 1) << "auto span = " << op1 << (type->id == FieldType::Array ? ";\n" : "();\n")
         << bd << "auto it = " << op2 << ".begin();\n"
         << bd << "for (auto e : span) {\n";

      os << (bd += 1) << "auto __ptr = ::nprpc::make_wrapper1(*it);\n";

      assign_from_cpp_type(wt, "  e", "__ptr", os, true);

      os << bd << "++it;\n" << (bd -= 1) << "}\n" << (bd -= 1) << "}\n";
    }
    break;
  }

  case FieldType::Enum:
    assert(top_type == false);
    os << bd << op1 << "() = " << op2 << ";\n";
    break;

  case FieldType::Alias:
    // For alias types top_type should be forwarded as is
    assign_from_cpp_type(calias(type)->type, op1, op2, os, from_iterator, top_type);
    break;

  case FieldType::String:
    if (top_type) {
      os << bd << op1 << " = " << op2 << ";\n";
    } else {
      os << bd << op1 << '(' << op2 << ");\n";
    }
    break;

  case FieldType::Optional: {
    auto bd0 = bd++;

    os << bd0 << "if (" << op2 << ") {\n";
    os << bd << op1 << accessor << "alloc();\n";

    auto wt = copt(type)->real_type();
    auto* direct_wt = wt->id == FieldType::Alias ? calias(wt)->get_real_type() : wt;
    if (direct_wt->id == FieldType::Struct || direct_wt->id == FieldType::Vector ||
        direct_wt->id == FieldType::Array || direct_wt->id == FieldType::String) {
      os << bd << "auto value_dir = " << op1 << accessor << "value();\n";
      assign_from_cpp_type(wt, "value_dir", op2 + ".value()", os, false, true, true);
    } else {
      assign_from_cpp_type(wt, op1 + std::string(accessor) + "value", op2 + ".value()", os, false,
                           false);
    }

    os << bd0 << "} else { \n";
    os << bd << op1 << accessor << "set_nullopt();\n";
    os << bd0 << "}\n";

    bd = bd0;

    break;
  }

  case FieldType::Object:
    assert(top_type == false);
    os << bd << "{\n";
    os << bd << "  " << "auto tmp = " << op1 << "();\n";
    os << bd << "  "
       << "::nprpc::detail::helpers::ObjectId::to_flat("
          "tmp, "
       << op2
       << ".get_data()"
          ");\n";
    os << bd << "}\n";
    break;

  case FieldType::Variant: {
    auto* v = cvar(type);
    const auto bd0 = bd;
    os << bd0 << "{\n";
    const std::string_view vd_access = top_type ? "." : "().";
    // If top_type is true, op1 is already a Direct variable; otherwise call op1() to get one
    if (top_type) {
      os << bd + 1 << "auto& vd = " << op1 << ";\n";
    } else {
      os << bd + 1 << "auto vd = " << op1 << "();\n";
    }
    os << bd + 1 << "vd.set_kind(static_cast<std::uint32_t>(" << op2 << ".kind));\n";
    os << bd + 1 << "std::visit([&](auto&& val) {\n";
    bd = bd + 2;
    os << bd << "using T = std::decay_t<decltype(val)>;\n";
    for (size_t i = 0; i < v->arms.size(); ++i) {
      auto& arm = v->arms[i];
      auto* atype = arm.type;
      if (atype->id == FieldType::Alias) atype = calias(atype)->get_real_type();
      // if constexpr branch: match on the C++ arm type
      // Use elaborated type specifier (struct Foo) to avoid name lookup
      // ambiguity with helpers::Foo namespaces that share the same local name.
      os << (bd - 1) << (i == 0 ? "if" : "} else if") << " constexpr (std::is_same_v<T, ";
      if (atype->id == FieldType::Struct) os << "struct ";
      emit_type(atype, os);
      os << ">) {\n";
      // Allocate flat storage for the arm in the growing buffer
      os << bd << "vd.alloc_arm(sizeof(";
      emit_flat_type(atype, os);
      os << "), alignof(";
      emit_flat_type(atype, os);
      os << "));\n";
      auto* direct_atype = atype->id == FieldType::Alias ? calias(atype)->get_real_type() : atype;
      if (direct_atype->id == FieldType::Struct || direct_atype->id == FieldType::Vector ||
          direct_atype->id == FieldType::Array || direct_atype->id == FieldType::String) {
        os << bd << "auto arm_d = vd.value_" << arm.name << "();\n";
        assign_from_cpp_type(atype, "arm_d", "val", os, false, true);
      } else {
        // Fundamental/Enum: value_armName() returns T&, pass as callable
        assign_from_cpp_type(atype, "vd.value_" + arm.name, "val", os, false, false);
      }
    }
    if (!v->arms.empty()) os << (bd - 1) << "}\n";
    bd = bd0;
    os << bd + 1 << "}, " << op2 << ".value);\n";
    os << bd0 << "}\n";
    bd = bd0;
    break;
  }

  default:
    assert(false);
    break;
  }
}

void CppBuilder::assign_from_flat_type(AstTypeDecl* type,
                                       std::string op1,
                                       std::string op2,
                                       std::ostream& os,
                                       bool from_iterator,
                                       bool top_object)
{
  switch (type->id) {
  case FieldType::Fundamental: {
    // assert(top_object == false);
    auto ft = cft(type);
    os << bd << op1 << " = " << (ft->token_id == TokenId::Boolean ? "(bool)" : "") << op2
       << "();\n";
    break;
  }

  case FieldType::Struct: {
    auto s = cflat(type);
    if (s->flat) {
      os << bd << "memcpy(&" << op1 << ", " << op2 << (top_object ? "." : "().") << "__data(), "
         << s->size << ");\n";
    } else {
      for (auto field : s->fields) {
        assign_from_flat_type(
            field->type, op1 + '.' + field->name,
            op2 + (top_object ? "." : (from_iterator ? "." : "().")) + field->name, os);
      }
    }
    break;
  }

  case FieldType::Array:
  case FieldType::Vector: {
    auto wt = static_cast<AstWrapType*>(type)->type;
    auto const size = std::get<0>(get_type_size_align(wt));

    bool is_string = (wt->id == FieldType::String);

    os << bd << "{\n" << bd + 1 << "auto span = " << op2 << (is_string ? "_d()();\n" : "();\n");

    if (type->id == FieldType::Vector) {
      os << bd + 1 << op1 << ".resize(span.size());\n";
    }

    if (is_flat(wt)) {
      os << bd + 1 << "memcpy(" << op1 << ".data(), span.data(), " << size << " * span.size());\n";
    } else {
      auto its = "it" + (bd + 1).str();
      os << bd + 1 << "auto " << its << " = " << "std::begin(" << op1 << ");\n";
      os << bd + 1 << "for (auto e : span) {\n";

      auto bd0 = bd;
      bd = bd + 2;
      assign_from_flat_type(wt, "(*" + its + ")", "e", os, true, false);
      bd = bd0;
      os << bd + 2 << "++" << its << ";\n";
      os << bd + 1 << "}\n";
    }

    os << bd << "}\n";
    break;
  }

  case FieldType::String:
    os << bd << op1 << " = (std::string_view)" << op2 << "();\n";
    break;

  case FieldType::Optional: {
    auto wt = copt(type)->real_type();
    auto* direct_wt = wt->id == FieldType::Alias ? calias(wt)->get_real_type() : wt;

    auto const bd0 = bd;

    os << bd0 << "{\n";

    os << bd + 1 << "auto opt = " << op2 << "();\n";
    os << bd + 1 << "if (opt.has_value()) {\n";
    os << bd + 2 << op1 << " = std::decay<decltype(" << op1 << ")>::type::value_type{};\n";
    os << bd + 2 << "auto& value_to = " << op1 << ".value();\n";

    bd = bd + 2;
    if (direct_wt->id == FieldType::Struct || direct_wt->id == FieldType::Vector ||
        direct_wt->id == FieldType::Array || direct_wt->id == FieldType::String) {
      os << bd << "auto value_from = opt.value();\n";
      assign_from_flat_type(wt, "value_to", "value_from", os, false, true);
    } else {
      assign_from_flat_type(wt, "value_to", "opt.value", os, false, false);
    }
    bd = bd - 2;

    os << bd + 1 << "} else { \n";
    os << bd + 2 << op1 << " = std::nullopt;\n";
    os << bd + 1 << "}\n";

    os << bd0 << "}\n";

    bd = bd0;

    break;
  }

  case FieldType::Enum:
    os << bd << op1 << " = " << op2 << "();\n";
    break;

  case FieldType::Alias:
    assign_from_flat_type(calias(type)->get_real_type(), op1, op2, os, from_iterator);
    break;

  case FieldType::Object:
    if (top_object) {
      os << bd << op1 << " = ::nprpc::impl::create_object_from_flat(" << op2
         << "(), this->get_endpoint());\n";
    } else {
      os << bd << op1 << ".assign_from_direct(" << op2 << "());\n";
    }
    break;

  case FieldType::Variant: {
    auto* v = cvar(type);
    const auto bd0 = bd;
    os << bd0 << "{\n";
    os << bd + 1 << "auto vd = " << op2 << "();\n";
    os << bd + 1 << "switch (static_cast<" << ns(v->nm) << v->name << "::Kind>(vd.kind())) {\n";
    for (size_t i = 0; i < v->arms.size(); ++i) {
      auto& arm = v->arms[i];
      auto* atype = arm.type;
      if (atype->id == FieldType::Alias) atype = calias(atype)->get_real_type();
      os << bd + 1 << "case " << ns(v->nm) << v->name << "::Kind::" << arm.name << ": {\n";
      // Declare arm_val of the C++ type.
      // Use elaborated type specifier (struct Foo) to avoid name lookup
      // ambiguity with helpers::Foo namespaces that share the same local name.
      os << bd + 2;
      if (atype->id == FieldType::Struct) os << "struct ";
      emit_type(atype, os);
      os << " arm_val{};\n";
      // Follow the Optional pattern:
      // complex types (Struct/Vector/Array/String) → get Direct wrapper, recurse with top_object=true
      // simple types (Fundamental/Enum)            → pass callable name,  recurse with top_object=false
      auto* direct_atype = atype->id == FieldType::Alias ? calias(atype)->get_real_type() : atype;
      if (direct_atype->id == FieldType::Struct || direct_atype->id == FieldType::Vector ||
          direct_atype->id == FieldType::Array || direct_atype->id == FieldType::String) {
        os << bd + 2 << "auto arm_d = vd.value_" << arm.name << "();\n";
        bd = bd + 2;
        assign_from_flat_type(atype, "arm_val", "arm_d", os, false, true);
        bd = bd0;
      } else {
        bd = bd + 2;
        assign_from_flat_type(atype, "arm_val", "vd.value_" + arm.name, os, false, false);
        bd = bd0;
      }
      os << bd + 2 << op1 << " = {" << ns(v->nm) << v->name << "::Kind::" << arm.name << ", std::move(arm_val)};\n";
      os << bd + 2 << "break;\n";
      os << bd + 1 << "}\n";
    }
    os << bd + 1 << "}\n";
    os << bd0 << "}\n";
    bd = bd0;
    break;
  }

  default:
    assert(false);
    break;
  }
}

void CppBuilder::emit_struct2(AstStructDecl* s, std::ostream& os, Target target)
{
  auto make_struct = [s, this, &os]<typename T>(T&& fn) {
    os << "struct " << s->name << " {\n";
    for (auto const f : s->fields) {
      os << "  ";
      fn(f->type, os);
      os << ' ' << f->name << ";\n";
    }
    os << "};\n\n";
  };

  if (target == Target::Regular) {
    make_struct(std::bind(
        static_cast<void (CppBuilder::*)(AstTypeDecl*, std::ostream&)>(&CppBuilder::emit_type),
        this, _1, _2));
  } else if (target == Target::Exception) {
    os << "class " << s->name
       << " : public ::nprpc::Exception {\n"
          "public:\n";

    if (s->fields.size() > 1) {
      std::for_each(next(begin(s->fields)), end(s->fields), [this, &os](auto f) {
        os << "  ";
        emit_type(f->type, os);
        os << " " << f->name << ";\n";
      });
      os << '\n';
    }

    os << "  " << s->name << "() : ::nprpc::Exception(\"" << s->name << "\") {} \n";

    if (s->fields.size() > 1) {
      os << "  " << s->name << '(';
      std::for_each(next(begin(s->fields)), end(s->fields),
                    [this, &os, ix = 1ul, size = s->fields.size()](auto f) mutable {
                      emit_type(f->type, os);
                      os << " _" << f->name;
                      if (++ix < size)
                        os << ", ";
                    });

      os << ")\n"
            "    : ::nprpc::Exception(\""
         << s->name << "\")\n";

      std::for_each(next(begin(s->fields)), end(s->fields), [this, &os](auto f) mutable {
        os << "    , " << f->name << "(_" << f->name << ")\n";
      });

      os << "  {\n"
            "  }\n";
    }

    os << "};\n\n";
  }

  if (target != Target::FunctionArgument)
    os << "namespace flat {\n";

  make_struct(std::bind(
      static_cast<void (CppBuilder::*)(AstTypeDecl*, std::ostream&)>(&CppBuilder::emit_flat_type),
      this, _1, _2));

  auto const accessor_name = s->name + "_Direct";

  os << "class " << accessor_name
     << " {\n"
        "  ::nprpc::flat_buffer& buffer_;\n"
        "  const std::uint32_t offset_;\n\n"
        "  auto& base() noexcept { return *reinterpret_cast<"
     << s->name
     << "*>(reinterpret_cast<std::byte*>(buffer_.data().data()) + offset_); "
        "}\n"
        "  auto const& base() const noexcept { return "
        "*reinterpret_cast<const "
     << s->name
     << "*>(reinterpret_cast<const std::byte*>(buffer_.data().data()) + "
        "offset_); }\n"
        "public:\n"
        "  uint32_t offset() const noexcept { return offset_; }\n"
        "  void* __data() noexcept { return (void*)&base(); }\n"
        "  "
     << accessor_name
     << "(::nprpc::flat_buffer& buffer, std::uint32_t offset)\n"
        "    : buffer_(buffer)\n"
        "    , offset_(offset)\n"
        "  {\n"
        "  }\n";

  for (auto& f : s->fields) {
    emit_accessors(s->name, f, os);
  }

  os << "};\n";

  if (target != Target::FunctionArgument)
    os << "} // namespace flat\n";

  os << '\n';
}

void CppBuilder::emit_constant(const std::string& name, AstNumber* number)
{
  oh << "constexpr auto " << name << " = ";
  std::visit(overloads
             {
                 [&](int32_t x) { oh << x; },
                 [&](int64_t x) { oh << x; },
                 [&](float x) { oh << x << 'f'; },
                 [&](double x) { oh << x; },
                 [&](bool x) { oh << std::ios::boolalpha << x << std::ios::dec; },
             },
             number->value);
  oh << ";\n";
}

void CppBuilder::emit_struct(AstStructDecl* s) { 
  emit_struct2(s, oh, Target::Regular);
}

void CppBuilder::emit_exception(AstStructDecl* s)
{
  assert(s->is_exception());
  emit_struct2(s, oh, Target::Exception);
}

void CppBuilder::finalize()
{
  auto filename = ctx_->get_file_path().filename();
  auto const header_file_path = filename.replace_extension(".hpp");
  auto const cpp_file_path = filename.replace_extension(".cpp");

  std::ofstream ofs_hpp(out_path_ / header_file_path, std::ios::binary);
  std::ofstream ofs_cpp(out_path_ / cpp_file_path, std::ios::binary);

  if (!ofs_hpp || !ofs_cpp) {
    throw std::runtime_error("Could not create output file...");
  }

  auto make_guard = [](const std::string& file) {
    std::string r(file);
    std::transform(r.begin(), r.end(), r.begin(),
                   [](char c) { return c == '.' ? '_' : ::toupper(c); });
    return r;
  };

  auto h1 = make_guard("__NPRPC_" + ctx_->current_file() + "_HPP__");

  ofs_hpp << "#ifndef " << h1
          << "\n"
             "#define "
          << h1
          << "\n\n"
             "#include <cstring>\n"
             "#include <variant>\n"
             "#include <nprpc/flat.hpp>\n";

  if (!ctx_->is_nprpc_base()) {
    ofs_hpp << "#include <nprpc/nprpc.hpp>\n";
    ofs_hpp << "#include <nprpc/bidi_stream.hpp>\n";
    ofs_hpp << "#include <nprpc/stream_writer.hpp>\n";
    ofs_hpp << "#include <nprpc/stream_reader.hpp>\n\n";
  } else {
    ofs_hpp << "#include <nprpc/exception.hpp>\n";
    ofs_hpp << "#include <string_view>\n\n";
  }

  // Generate module-specific export macro
  auto module_upper = make_guard(ctx_->current_file());
  export_macro_name_ = module_upper + "_API";

  ofs_hpp << "// Module export macro\n"
             "#ifdef NPRPC_EXPORTS\n"
             "#  define "
          << export_macro_name_
          << " NPRPC_EXPORT_ATTR\n"
             "#else\n"
             "#  define "
          << export_macro_name_
          << " NPRPC_IMPORT_ATTR\n"
             "#endif\n\n";

  const bool is_module = !ctx_->module().empty();

  ofs_cpp << "#include \"";

  ofs_cpp << ctx_->current_file() << ".hpp\"\n";

  ofs_cpp << "#include <nprpc/impl/nprpc_impl.hpp>\n\n"
             "void "
          << ctx_->current_file() << "_throw_exception(::nprpc::flat_buffer& buf);\n\n";

  if (is_module) {
    ofs_hpp << "namespace " << ctx_->nm_root()->to_cpp17_namespace() << " {\n\n";
    ofs_cpp << "namespace " << ctx_->nm_root()->to_cpp17_namespace() << " {\n\n";
  }

  emit_helpers();
  emit_struct_helpers();

  std::stringstream ss;

  ss << "namespace {\n";
  emit_arguments_structs(
      std::bind(&CppBuilder::emit_struct2, this, _1, std::ref(ss), Target::FunctionArgument));
  ocpp << ss.str() << "\n";
  emit_safety_checks();
  ocpp << "} // \n\n" << oc.str();

  if (is_module) {
    oh << "} // module " << ctx_->nm_root()->to_cpp17_namespace() << "\n";
    ocpp << "} // module " << ctx_->nm_root()->to_cpp17_namespace() << "\n";
  }

  // Emit nprpc_stream::deserialize<T> / serialize<T> specialisations outside any IDL namespace.
  // They must be at file scope so the primary templates in stream_reader.hpp / stream_writer.hpp
  // are visible and the specialisations end up in ::nprpc_stream, not in the IDL namespace.
  std::set<std::string> emitted_stream_deserializers;
  std::set<std::string> emitted_stream_serializers;
  auto stream_codec_type_key = [this](AstTypeDecl* type) {
    std::stringstream ss;
    auto bd_saved = bd;
    bd = 1;
    always_full_namespace(true);
    emit_type(type, ss);
    always_full_namespace(false);
    bd = bd_saved;
    return ss.str();
  };
  for (auto fn : stream_codec_fns_) {
    auto* sd = fn->stream_decl;
    if (sd == nullptr)
      continue;

    if (auto* out_type = canonical_stream_codec_type(sd->stream_out_type());
        out_type != nullptr && out_type->id != FieldType::Fundamental &&
        out_type->id != FieldType::Enum && !sd->direct) {
      auto key = stream_codec_type_key(out_type);
      if (emitted_stream_deserializers.insert(key).second)
        emit_stream_deserialize(out_type, false);
    }

    if (auto* in_type = canonical_stream_codec_type(sd->stream_in_type());
        in_type != nullptr && in_type->id != FieldType::Fundamental &&
        in_type->id != FieldType::Enum && !sd->direct) {
      auto key = stream_codec_type_key(in_type);
      if (emitted_stream_deserializers.insert(key).second)
        emit_stream_deserialize(in_type, false);
    }

    if (auto* in_type = canonical_stream_codec_type(sd->stream_in_type());
        in_type != nullptr && in_type->id != FieldType::Fundamental &&
        in_type->id != FieldType::Enum && !sd->direct) {
      auto key = stream_codec_type_key(in_type);
      if (emitted_stream_serializers.insert(key).second)
        emit_stream_serialize(in_type, false);
    }

    if (auto* out_type = canonical_stream_codec_type(sd->stream_out_type());
        out_type != nullptr && out_type->id != FieldType::Fundamental &&
        out_type->id != FieldType::Enum && !sd->direct) {
      auto key = stream_codec_type_key(out_type);
      if (emitted_stream_serializers.insert(key).second)
        emit_stream_serialize(out_type, false);
    }
  }

  oh << "\n#endif";

  // Emit exception throwing function
  auto& exs = ctx_->exceptions;
  if (!exs.empty()) {
    ocpp << "\n"
            "void "
         << ctx_->current_file()
         << "_throw_exception(::nprpc::flat_buffer& buf) { \n"
            "  switch(*(uint32_t*)( (char*)buf.data().data() + "
            "sizeof(::nprpc::impl::Header)) ) {\n";

    always_full_namespace(true);

    for (auto ex : exs) {
      ocpp << "  case " << ex->exception_id
           << ":\n"
              "  {\n"
              "    "
           << ns(ex->nm) << "flat::" << ex->name
           << "_Direct ex_flat(buf, sizeof(::nprpc::impl::Header));\n"
              "    "
           << ns(ex->nm) << ex->name << " ex;\n";

      for (size_t i = 1; i < ex->fields.size(); ++i) {
        auto f = ex->fields[i];
        assign_from_flat_type(f->type, "ex." + f->name, "ex_flat." + f->name, ocpp, false, true);
      }

      ocpp << "    throw ex;\n"
              "  }\n";
    }

    always_full_namespace(false);

    ocpp << "  default:\n"
            "    throw std::runtime_error(\"unknown rpc exception\");\n"
            "  }\n"
            "}\n";
  }

  ofs_hpp << oh.str();
  ofs_cpp << ocpp.str();
}

void CppBuilder::emit_safety_checks_r(
    AstTypeDecl* type, std::string op, std::ostream& os, bool /* from_iterator */, bool top_type)
{
  switch (type->id) {
  case FieldType::Struct: {
    auto s = cflat(type);

    // Generate total flat size check only once for the top-level wrapper _M struct
    // and skip for nested structs, since they will be checked as part of the parent struct's size check
    if (top_type) {
      os << "  if (static_cast<std::uint32_t>(buf.size()) < " << op << ".offset() + " << s->size
         << ") goto check_failed;\n";
    }

    if (s->flat)
      break;

    for (auto field : s->fields) {
      auto ftr = field->type;
      if (ftr->id == FieldType::Alias)
        ftr = calias(ftr)->get_real_type();

      if (ftr->id == FieldType::Vector || ftr->id == FieldType::Optional ||
          ftr->id == FieldType::Struct || ftr->id == FieldType::String ||
          ftr->id == FieldType::Object)
      {
        os << bd << "{\n";
        auto str = op + "." + field->name +
                   (ftr->id == FieldType::Vector || ftr->id == FieldType::String ? "_d()" : "()");
        bd += 1;
        emit_safety_checks_r(field->type, str, os, false, ftr->id != FieldType::Struct);
        bd -= 1;
        os << bd << "}\n";
      } else if (ftr->id == FieldType::Array) {
        if (is_flat(car(ftr)->type))
          continue;
        // Need to check each element in the array
        // TODO: Test it
        os << bd << "{\n";
        auto str = op + "." + field->name + "()";
        bd += 1;
        os << bd << "auto span = " << str << ";\n";
        os << bd << "for (auto e : span) {\n";
        emit_safety_checks_r(car(ftr)->type, "e", os, true, false);
        os << bd << "}\n";
        bd -= 1;
      }
    }

    break;
  }

  case FieldType::String:
    os << bd << "if(!" << op
       << "._check_size_align(static_cast<std::uint32_t>(buf.size()))) "
          "goto "
          "check_failed;\n";
    break;

  case FieldType::Vector: {
    auto wt = cwt(type)->type;

    os << bd << "if(!" << op
       << "._check_size_align(static_cast<std::uint32_t>(buf.size()))) "
          "goto "
          "check_failed;\n";

    if (is_flat(wt))
      break;

    os << bd << "{\n"
       << bd + 1 << "auto span = " << op << "();\n"
       << bd + 1 << "for (auto e : span) {\n";
    emit_safety_checks_r(wt, "e", os, true, true);
    os << bd + 1 << "}\n" << bd << "}\n";

    break;
  }

  case FieldType::Optional: {
    auto wt = cwt(type)->type;
    os << bd << "if(!" << op
       << "._check_size_align(static_cast<std::uint32_t>(buf.size()))) "
          "goto "
          "check_failed;\n";

    if (is_flat(wt))
      break;

    os << bd << "if ( " << op << ".has_value() ) {\n"
       << bd + 1 << "auto value = " << op << ".value();\n";
    emit_safety_checks_r(wt, "value", os, true, true);
    os << bd << "}\n";

    break;
  }

  case FieldType::Object:
    emit_safety_checks_r(ctx_->get_builtin_types_info().object_id_struct, op, os, false, false);
    break;

  case FieldType::Array:
    break;

  default:
    break;
    // assert(false);
  }
}

void CppBuilder::emit_safety_checks()
{
  std::set<struct_id_t> set;

  for (auto ifs : ctx_->interfaces) {
    if (ifs->trusted)
      continue;

    for (auto fn : ifs->fns) {
      auto s = fn->in_s;

      if (!s)
        continue;

      auto const id = s->get_function_struct_id();
      if (set.find(id) != set.end())
        continue;
      set.emplace(id);

      auto const name = make_safety_check_name(s);

      ocpp << "bool check_" << name << "(::nprpc::flat_buffer& buf, " << fn->in_s->name
           << "_Direct& ia" << ") {\n";

      emit_safety_checks_r(s, "ia", ocpp, false, true);

      ocpp << "  return true;\n"
              "check_failed:\n"
              "  return false;\n"
              "}\n";
    }
  }
}

void CppBuilder::emit_namespace_begin()
{
  oh << "namespace " << ctx_->nm_cur()->name() << " { \n";
  oc << "namespace " << ctx_->nm_cur()->name() << " { \n";
}

void CppBuilder::emit_namespace_end()
{
  oh << "} // namespace " << ctx_->nm_cur()->name() << "\n\n";
  oc << "} // namespace " << ctx_->nm_cur()->name() << "\n\n";
}

void CppBuilder::emit_helpers()
{
  bool need_helpers = false;
  for (auto& [unused, s] : ctx_->affa_list) {
    if (s->is_builtin)
      continue;
    
    bool has_non_fundamental = false;
    for (auto f : s->fields) {
      assert(f->function_argument);
      if (is_fundamental(f->type) || f->type->id == FieldType::String || f->type->id == FieldType::Object)
        continue;
      has_non_fundamental = true;
      break;
    }

    if (has_non_fundamental) {
      need_helpers = true;
      break;
    }
  }

  if (!need_helpers)
    return;

  always_full_namespace(true);
  oh << "namespace helper {\n";
  
  for (auto& [unused, s] : ctx_->affa_list) {
    if (s->is_builtin)
      continue;

    for (auto f : s->fields) {
      assert(f->function_argument);
      if (is_fundamental(f->type) || f->type->id == FieldType::String ||
          f->type->id == FieldType::Object)
        continue;

      if (f->input_function_argument) {
        if (f->type->id == FieldType::Struct) {
          oh << "inline void assign_from_flat_" << f->function_name << "_"
             << f->function_argument_name << "(";
          emit_parameter_type_for_servant_callback_r(f->type, oh, false);
          oh << "& src, ";
          emit_parameter_type_for_proxy_call_r(f->type, oh, false);
          oh << "& dest) {\n";
          assign_from_flat_type(f->type, "dest", "src", oh, false, true);
          oh << "}\n";
        }

      } else {
        if (f->function_argument_name == "ret_val")
          continue;
        if (f->type->id == FieldType::Struct) {
          oh << "inline void assign_from_cpp_" << f->function_name << "_"
             << f->function_argument_name << "(";
          emit_parameter_type_for_servant_callback_r(f->type, oh, false);
          oh << "& dest, const ";
          emit_parameter_type_for_proxy_call_r(f->type, oh, false);
          oh << "& src) {\n";
          assign_from_cpp_type(f->type, "dest", "src", oh, false, true);
          oh << "}\n";
        } else { // Itearable
          oh << "template<::nprpc::IterableCollection T>\n"
                "void assign_from_cpp_"
             << f->function_name << "_" << f->function_argument_name << "(";
          emit_parameter_type_for_servant_callback_r(f->type, oh, false);
          oh << "& dest, const T & src) {\n";
          assign_from_cpp_type(f->type, "dest", "src", oh, false, true);
          oh << "}\n";
        }
      }
    }
  }

  oh << "}\n";
  always_full_namespace(false);
}

void CppBuilder::emit_struct_helpers()
{
  bool need_helpers = false;
  for (auto s : ctx_->get_structs_with_helpers()) {
    if (s->is_builtin)
      continue;
    need_helpers = true;
    break;
  }

  if (!need_helpers)
    return;

  for (auto s : ctx_->get_structs_with_helpers()) {
    if (s->is_builtin)
      continue;

    oh << "namespace " << ns(s->nm) << "helpers::" << s->name << " {\n";
    bd = 1;
    // auto saved_namespace = ctx_->set_namespace(s->nm);
    oh << "inline struct ";
    emit_parameter_type_for_proxy_call_r(s, oh, false);
    oh << " from_flat(";
    emit_parameter_type_for_servant_callback_r(s, oh, false);
    oh << "& src) {\n  struct ";
    emit_parameter_type_for_proxy_call_r(s, oh, false);
    oh << " result;\n";
    assign_from_flat_type(s, "result", "src", oh, false, true);
    oh << "  return result;\n";
    oh << "}\n";

    oh << "inline void to_flat(";
    emit_parameter_type_for_servant_callback_r(s, oh, false);
    oh << "& dest, const struct ";
    emit_parameter_type_for_proxy_call_r(s, oh, false);
    oh << "& src) {\n";
    assign_from_cpp_type(s, "dest", "src", oh, false, true);
    oh << "}\n";
    // ctx_->set_namespace(saved_namespace);
    oh << "} // namespace " << ns(s->nm) << "helpers::" << s->name << "\n\n";
    bd = 0;
  }
}

void CppBuilder::emit_function_arguments(
    AstFunctionDecl* fn,
    std::ostream& os,
    std::function<void(AstFunctionArgument*, std::ostream& os)> emitter)
{
  os << "(";
  bool first = true;
  for (auto arg : fn->args) {
    if (arg->type->id == FieldType::Stream)
      continue;
    if (!first)
      os << ", ";
    first = false;
    emitter(arg, os);
    os << " " << arg->name;
  }
  os << ')';
};

void CppBuilder::emit_proxy_out_assignments(AstFunctionDecl* fn, bool use_co_return)
{
  assert(fn->out_s);

  // Fundamentals/enums with 'direct' are demoted to regular out params; they
  // must not trigger the buffer-ownership move.
  auto is_real_direct = [](const AstFunctionArgument* a) {
    auto* t = a->type;
    if (t->id == FieldType::Alias) t = calias(t)->get_real_type();
    return a->direct &&
           t->id != FieldType::Fundamental &&
           t->id != FieldType::Enum;
  };
  bool has_direct_out = std::any_of(fn->out_args.begin(), fn->out_args.end(), is_real_direct);

  if (has_direct_out) {
    // Move ownership of the receive buffer so Owned* wrappers can keep it alive.
    oc << "  auto buf_ptr = std::make_shared<::nprpc::flat_buffer>(std::move(buf));\n";
    oc << "  " << fn->out_s->name << "_Direct out(*buf_ptr, sizeof(::nprpc::impl::Header));\n";
  } else {
    oc << "  " << fn->out_s->name << "_Direct out(buf, sizeof(::nprpc::impl::Header));\n";
  }

  int ix = fn->is_void() ? 0 : 1;
  bd = 2;
  for (auto out : fn->args) {
    if (out->modifier == ArgumentModifier::In)
      continue;
    const int field_idx = ++ix;
    const std::string field_ref = "out._" + std::to_string(field_idx);

    // Resolve alias for all type-id checks in this block.
    auto* eff_type = out->type;
    if (eff_type->id == FieldType::Alias) eff_type = calias(eff_type)->get_real_type();

    if (out->direct && eff_type->id != FieldType::Fundamental && eff_type->id != FieldType::Enum) {
      // Zero-copy path: wrap the owned buffer.
      const bool is_prim_vec =
          (eff_type->id == FieldType::Vector || eff_type->id == FieldType::Array) &&
          (cwt(eff_type)->real_type()->id == FieldType::Fundamental ||
           cwt(eff_type)->real_type()->id == FieldType::Enum);

      if (is_prim_vec) {
        // OwnedSpan<T> — constructed from Span<T> returned by _N()
        oc << "  " << out->name << " = ::nprpc::flat::OwnedSpan<";
        emit_flat_type(cwt(eff_type)->real_type(), oc);
        oc << ">(buf_ptr, " << field_ref << "());\n";
      } else {
        // OwnedDirect<TD> — constructed from the absolute offset of the
        // Direct accessor.  Types with a separate _d() accessor (Vector of
        // structs, Array, String) expose it via _N_d().offset(); the rest
        // (Struct, Optional) via _N().offset().
        const bool has_d_accessor =
            (eff_type->id == FieldType::Vector ||
             eff_type->id == FieldType::Array  ||
             eff_type->id == FieldType::String);
        oc << "  " << out->name << " = ::nprpc::flat::OwnedDirect<";
        emit_direct_type(out->type, oc);
        oc << ">(buf_ptr, " << field_ref;
        oc << (has_d_accessor ? "_d().offset()" : "().offset()");
        oc << ");\n";
      }
    } else {
      assign_from_flat_type(out->type, out->name, field_ref, oc, false,
                            eff_type->id == FieldType::Object);
    }
  }

  if (!fn->is_void()) {
    oc << bd;
    emit_type(fn->ret_value, oc);
    oc << " __ret_value;\n";
    assign_from_flat_type(fn->ret_value, "__ret_value", "out._1", oc, false,
                          fn->ret_value->id == FieldType::Object);
    oc << (use_co_return ? "  co_return __ret_value;\n" : "  return __ret_value;\n");
  }
}

void CppBuilder::proxy_call(AstFunctionDecl* fn)
{
  oc << "  session->send_receive(buf, this->get_timeout());\n"
        "  auto std_reply = ::nprpc::impl::handle_standart_reply(buf);\n";

  if (fn->ex)
    oc << "  if (std_reply == 1) " << ctx_->current_file() << "_throw_exception(buf);\n";

  if (!fn->out_s) {
    oc << "  if (std_reply != 0) {\n"
          "    throw ::nprpc::Exception(\"Unknown Error\");\n"
          "  }\n";
  } else {
    oc << "  if (std_reply != -1) {\n"
          "    throw ::nprpc::Exception(\"Unknown Error\");\n"
          "  }\n";
    emit_proxy_out_assignments(fn);
  }
}

void CppBuilder::emit_declared_exception_reply(AstFunctionDecl* fn,
                                               std::ostream& os,
                                               const std::string& indent)
{
  auto declared_exceptions = fn->exceptions;
  if (declared_exceptions.empty() && fn->ex)
    declared_exceptions.push_back(fn->ex);

  if (declared_exceptions.empty())
    return;

  const auto offset = size_of_header;
  os << indent << "}\n";
  for (auto* ex : declared_exceptions) {
    const auto initial_size = offset + ex->size;

    always_full_namespace(true);
    os << indent << "catch(" << ns(ex->nm) << ex->name << "& e) {\n"
       << indent << "  assert(ctx.tx_buffer != nullptr);\n"
       << indent << "  auto& obuf = *ctx.tx_buffer;\n"
       << indent << "  obuf.consume(obuf.size());\n"
       << indent << "  if (!::nprpc::impl::g_rpc->prepare_zero_copy_buffer(ctx, obuf, "
       << initial_size << "))\n"
       << indent << "    obuf.prepare(" << initial_size << ");\n"
       << indent << "  obuf.commit(" << initial_size << ");\n"
       << indent << "  " << ns(ex->nm) << "flat::" << ex->name << "_Direct oa(obuf,"
       << offset << ");\n"
       << indent << "  oa.__ex_id() = " << ex->exception_id << ";\n";
    always_full_namespace(false);

    for (size_t i = 1; i < ex->fields.size(); ++i) {
      auto mb = ex->fields[i];
      assign_from_cpp_type(mb->type, "oa." + mb->name, "e." + mb->name, os);
    }

    os << indent << "  auto* out_header = static_cast<::nprpc::impl::Header*>(obuf.data().data());\n"
       << indent << "  out_header->size = static_cast<uint32_t>(obuf.size());\n"
       << indent << "  out_header->msg_id = ::nprpc::impl::MessageId::Exception;\n"
       << indent << "  out_header->msg_type = ::nprpc::impl::MessageType::Answer;\n"
       << indent << "  out_header->request_id = static_cast<const ::nprpc::impl::Header*>(ctx.rx_buffer->cdata().data())->request_id;\n"
       << indent << "}\n";
  }
}

void CppBuilder::emit_stream_proxy_reply_handling(AstFunctionDecl* fn)
{
  if (fn->ex)
    oc << "  if (std_reply == 1) " << ctx_->current_file() << "_throw_exception(buf);\n";
  oc << "  if (std_reply != 0) { throw ::nprpc::Exception(\"Unknown Error\"); }\n";
}

void CppBuilder::proxy_call_coro(AstFunctionDecl* fn)
{
  oc << "  co_await session->send_receive_coro(buf, this->get_timeout(), std::move(st));\n"
        "  auto std_reply = ::nprpc::impl::handle_standart_reply(buf);\n";

  if (fn->ex)
    oc << "  if (std_reply == 1) " << ctx_->current_file() << "_throw_exception(buf);\n";

  if (!fn->out_s) {
    oc << "  if (std_reply != 0) {\n"
          "    throw ::nprpc::Exception(\"Unknown Error\");\n"
          "  }\n";
  } else {
    oc << "  if (std_reply != -1) {\n"
          "    throw ::nprpc::Exception(\"Unknown Error\");\n"
          "  }\n";
    emit_proxy_out_assignments(fn, true);
  }
}

void CppBuilder::proxy_unreliable_call(AstFunctionDecl* fn)
{
  oc << "  ::nprpc::impl::g_rpc->send_unreliable(this->get_endpoint(), "
        "std::move(buf));\n";
}

// Forward declaration — defined after emit_interface in this translation unit.
static void emit_stream_reader_type(
    CppBuilder& b, AstStreamDecl* sd, std::ostream& os,
    std::function<void(AstTypeDecl*, std::ostream&)> emitType,
    std::function<void(AstTypeDecl*, std::ostream&)> emitDirectType);

static void emit_stream_writer_type(
    CppBuilder& b, AstTypeDecl* type, std::ostream& os,
    std::function<void(AstTypeDecl*, std::ostream&)> emitType)
{
  os << "::nprpc::StreamWriter<";
  emitType(type, os);
  os << ">";
}

static void emit_stream_proxy_return_type(CppBuilder& b,
                                          AstFunctionDecl* fn,
                                          std::ostream& os,
                                          std::function<void(AstTypeDecl*, std::ostream&)> emitType,
                                          std::function<void(AstTypeDecl*, std::ostream&)> emitDirectType)
{
  auto* sd = fn->stream_decl;
  assert(sd != nullptr);

  switch (fn->stream_kind) {
  case StreamKind::Server:
    emit_stream_reader_type(b, sd, os, emitType, emitDirectType);
    break;
  case StreamKind::Client:
    emit_stream_writer_type(b, sd->stream_in_type(), os, emitType);
    break;
  case StreamKind::Bidi:
    os << "std::pair<";
    emit_stream_writer_type(b, sd->stream_in_type(), os, emitType);
    os << ", ";
    emit_stream_reader_type(b, sd, os, emitType, emitDirectType);
    os << ">";
    break;
  default:
    assert(false);
  }
}

void CppBuilder::proxy_stream_call(AstFunctionDecl* fn)
{
  auto* sd = fn->stream_decl;
  oc << "  auto session = "
        "::nprpc::impl::g_rpc->get_session(this->get_endpoint());\n";
  oc << "  auto stream_id = ::nprpc::impl::StreamManager::generate_stream_id();\n";

  // Create StreamReader BEFORE sending StreamInit to avoid race condition
  // where chunks arrive before the reader is registered
  oc << "  ";
  emit_stream_reader_type(*this, sd, oc,
    [this](AstTypeDecl* t, std::ostream& os){ emit_type(t, os); },
    [this](AstTypeDecl* t, std::ostream& os){ emit_direct_type(t, os); });
  oc << " reader(session->ctx(), stream_id);\n";

  const auto args_offset = get_stream_init_arguments_offset();
  const auto fixed_size = args_offset + (fn->in_s ? fn->in_s->size : 0);
  const auto capacity = fixed_size + (fn->in_s ? (fn->in_s->flat ? 0 : 128) : 0);

  // Prepare StreamInit message
  oc << "  ::nprpc::flat_buffer buf;\n"
        "  buf.prepare(" << capacity << ");\n"
        "  buf.commit(" << fixed_size << ");\n";

  oc << "  auto* header = "
        "static_cast<::nprpc::impl::Header*>(buf.data().data());\n"
        "  header->msg_id = ::nprpc::impl::MessageId::StreamInitialization;\n"
        "  header->msg_type = ::nprpc::impl::MessageType::Request;\n";

  oc << "  ::nprpc::impl::flat::StreamInit_Direct init(buf, "
        "sizeof(::nprpc::impl::Header));\n"
        "  init.stream_id() = stream_id;\n"
        "  init.poa_idx() = this->poa_idx();\n"
        "  init.interface_idx() = interface_idx_;\n"
        "  init.object_id() = this->object_id();\n"
        "  init.func_idx() = "
     << fn->idx << ";\n";
  oc << "  init.stream_kind() = ::nprpc::impl::StreamKind::Server;\n";

  // Serialize input arguments
  if (fn->in_s) {
    oc << "  " << fn->in_s->name << "_Direct _(buf," << args_offset << ");\n";
    int ix = 0;
    for (auto in : fn->args) {
      if (in->modifier == ArgumentModifier::Out)
        continue;
      bd = 1;
      assign_from_cpp_type(in->type, "_._" + std::to_string(++ix), in->name, oc);
    }
  }

  oc << "  header->size = static_cast<uint32_t>(buf.size());\n";

  // Wait for the init ACK so stream initialization failures propagate to the caller.
  oc << "  session->send_receive(buf, this->get_timeout());\n";
  oc << "  auto std_reply = ::nprpc::impl::handle_standart_reply(buf);\n";
  emit_stream_proxy_reply_handling(fn);
  if (!fn->is_reliable)
    oc << "  session->ctx().stream_manager->set_reader_unreliable(stream_id, true);\n";
  oc << "  session->ctx().stream_manager->defer_stream_start(stream_id);\n";
  oc << "  session->ctx().stream_manager->on_reply_sent();\n";

  // Return the already-created reader
  oc << "  return reader;\n";
}

  void CppBuilder::proxy_client_stream_call(AstFunctionDecl* fn)
  {
    auto* sd = fn->stream_decl;
    oc << "  auto session = "
      "::nprpc::impl::g_rpc->get_session(this->get_endpoint());\n";
    oc << "  auto stream_id = ::nprpc::impl::StreamManager::generate_stream_id();\n";
    oc << "  ";
    emit_stream_writer_type(*this, sd->stream_in_type(), oc,
    [this](AstTypeDecl* t, std::ostream& os){ emit_type(t, os); });
    oc << " writer(session->ctx(), stream_id);\n";

    const auto args_offset = get_stream_init_arguments_offset();
    const auto fixed_size = args_offset + (fn->in_s ? fn->in_s->size : 0);
    const auto capacity = fixed_size + (fn->in_s ? (fn->in_s->flat ? 0 : 128) : 0);

    oc << "  ::nprpc::flat_buffer buf;\n"
      "  buf.prepare(" << capacity << ");\n"
      "  buf.commit(" << fixed_size << ");\n";

    oc << "  auto* header = "
      "static_cast<::nprpc::impl::Header*>(buf.data().data());\n"
      "  header->msg_id = ::nprpc::impl::MessageId::StreamInitialization;\n"
      "  header->msg_type = ::nprpc::impl::MessageType::Request;\n";

    oc << "  ::nprpc::impl::flat::StreamInit_Direct init(buf, "
      "sizeof(::nprpc::impl::Header));\n"
      "  init.stream_id() = stream_id;\n"
      "  init.poa_idx() = this->poa_idx();\n"
      "  init.interface_idx() = interface_idx_;\n"
      "  init.object_id() = this->object_id();\n"
      "  init.func_idx() = "
       << fn->idx << ";\n"
      "  init.stream_kind() = ::nprpc::impl::StreamKind::Client;\n";

    if (fn->in_s) {
      oc << "  " << fn->in_s->name << "_Direct _(buf," << args_offset << ");\n";
      int ix = 0;
      for (auto in : fn->args) {
    if (in->modifier == ArgumentModifier::Out || in == fn->stream_arg)
      continue;
    bd = 1;
    assign_from_cpp_type(in->type, "_._" + std::to_string(++ix), in->name, oc);
      }
    }

    oc << "  header->size = static_cast<uint32_t>(buf.size());\n";
    oc << "  session->send_receive(buf, this->get_timeout());\n";
    oc << "  auto std_reply = ::nprpc::impl::handle_standart_reply(buf);\n";
    emit_stream_proxy_reply_handling(fn);
    oc << "  session->ctx().stream_manager->defer_stream_start(stream_id);\n";
    oc << "  session->ctx().stream_manager->on_reply_sent();\n";
    oc << "  return writer;\n";
  }

  void CppBuilder::proxy_bidi_stream_call(AstFunctionDecl* fn)
  {
    auto* sd = fn->stream_decl;
    oc << "  auto session = "
      "::nprpc::impl::g_rpc->get_session(this->get_endpoint());\n";
    oc << "  auto stream_id = ::nprpc::impl::StreamManager::generate_stream_id();\n";
    oc << "  ";
    emit_stream_writer_type(*this, sd->stream_in_type(), oc,
    [this](AstTypeDecl* t, std::ostream& os){ emit_type(t, os); });
    oc << " writer(session->ctx(), stream_id);\n";
    oc << "  ";
    emit_stream_reader_type(*this, sd, oc,
    [this](AstTypeDecl* t, std::ostream& os){ emit_type(t, os); },
    [this](AstTypeDecl* t, std::ostream& os){ emit_direct_type(t, os); });
    oc << " reader(session->ctx(), stream_id);\n";

    const auto args_offset = get_stream_init_arguments_offset();
    const auto fixed_size = args_offset + (fn->in_s ? fn->in_s->size : 0);
    const auto capacity = fixed_size + (fn->in_s ? (fn->in_s->flat ? 0 : 128) : 0);

    oc << "  ::nprpc::flat_buffer buf;\n"
      "  buf.prepare(" << capacity << ");\n"
      "  buf.commit(" << fixed_size << ");\n";

    oc << "  auto* header = "
      "static_cast<::nprpc::impl::Header*>(buf.data().data());\n"
      "  header->msg_id = ::nprpc::impl::MessageId::StreamInitialization;\n"
      "  header->msg_type = ::nprpc::impl::MessageType::Request;\n";

    oc << "  ::nprpc::impl::flat::StreamInit_Direct init(buf, "
      "sizeof(::nprpc::impl::Header));\n"
      "  init.stream_id() = stream_id;\n"
      "  init.poa_idx() = this->poa_idx();\n"
      "  init.interface_idx() = interface_idx_;\n"
      "  init.object_id() = this->object_id();\n"
      "  init.func_idx() = "
       << fn->idx << ";\n"
      "  init.stream_kind() = ::nprpc::impl::StreamKind::Bidi;\n";

    if (fn->in_s) {
      oc << "  " << fn->in_s->name << "_Direct _(buf," << args_offset << ");\n";
      int ix = 0;
      for (auto in : fn->args) {
    if (in->modifier == ArgumentModifier::Out)
      continue;
    bd = 1;
    assign_from_cpp_type(in->type, "_._" + std::to_string(++ix), in->name, oc);
      }
    }

    oc << "  header->size = static_cast<uint32_t>(buf.size());\n";
    oc << "  session->send_receive(buf, this->get_timeout());\n";
    oc << "  auto std_reply = ::nprpc::impl::handle_standart_reply(buf);\n";
    emit_stream_proxy_reply_handling(fn);
    if (!fn->is_reliable)
      oc << "  session->ctx().stream_manager->set_reader_unreliable(stream_id, true);\n";
    oc << "  session->ctx().stream_manager->defer_stream_start(stream_id);\n";
    oc << "  session->ctx().stream_manager->on_reply_sent();\n";
    oc << "  return { std::move(writer), std::move(reader) };\n";
  }

void CppBuilder::proxy_async_call(AstFunctionDecl* fn)
{
  // Fire-and-forget: no reply expected, no handler parameter.
  oc << "  ::nprpc::impl::g_rpc->call_async(this->get_endpoint(), "
        "std::move(buf), std::nullopt, get_timeout());\n";
}

std::string_view CppBuilder::proxy_arguments(AstFunctionDecl* fn)
{
  if (auto it = proxy_arguments_.find(fn); it != proxy_arguments_.end())
    return it->second;

  std::stringstream ss;
  // All methods (sync, async fire-and-forget, coro) use the same plain
  // argument list — no handler parameter.
  emit_function_arguments(fn, ss, [this](AstFunctionArgument* arg, std::ostream& os) {
    if (arg->type->id == FieldType::Stream)
      return;
    if (arg->modifier == ArgumentModifier::Out && arg->direct) {
      emit_parameter_type_for_proxy_call_direct(arg, os);
      os << '&';
    } else {
      emit_parameter_type_for_proxy_call(arg, os);
    }
  });

  return proxy_arguments_.emplace(fn, ss.str()).first->second;
}

// Helper: emit the StreamReader<...> return type for the proxy declaration/definition.
// For non-direct: StreamReader<ElemType>  (value type; C++ ergonomic)
// For direct:     StreamReader<::nprpc::flat::OwnedDirect<ElemType_Direct>>  (zero-copy)
static void emit_stream_reader_type(
  CppBuilder& b, AstStreamDecl* sd, std::ostream& os,
    std::function<void(AstTypeDecl*, std::ostream&)> emitType,
    std::function<void(AstTypeDecl*, std::ostream&)> emitDirectType)
{
  os << "::nprpc::StreamReader<";
  if (sd->direct) {
    os << "::nprpc::flat::OwnedDirect<";
    emitDirectType(sd->type, os);
    os << ">";
  } else {
    emitType(sd->type, os);
  }
  os << ">";
}

static void emit_stream_reader_value_type(
  AstTypeDecl* type, bool direct, std::ostream& os,
  std::function<void(AstTypeDecl*, std::ostream&)> emitType,
  std::function<void(AstTypeDecl*, std::ostream&)> emitDirectType)
{
  os << "::nprpc::StreamReader<";
  if (direct) {
    os << "::nprpc::flat::OwnedDirect<";
    emitDirectType(type, os);
    os << ">";
  } else {
    emitType(type, os);
  }
  os << ">";
}

void CppBuilder::emit_stream_deserialize(AstTypeDecl* stream_type, bool direct)
{
  if (stream_type == nullptr)
    return;
  // Only needed for non-fundamental, non-direct element types.
  auto* elem = stream_type;
  if (elem->id == FieldType::Alias) elem = calias(elem)->get_real_type();
  if (elem->id == FieldType::Fundamental || elem->id == FieldType::Enum) return;
  if (direct) return; // OwnedDirect path needs no deserializer

  // Fully-qualified names are required because we emit inside namespace nprpc_stream.
  auto bd_saved = bd; bd = 1;
  always_full_namespace(true);

  oh << "namespace nprpc_stream {\n";
  oh << "template<>\n";
  oh << "inline ";
  emit_type(stream_type, oh);
  oh << " deserialize<";
  emit_type(stream_type, oh);
  oh << ">(::nprpc::flat_buffer& buf) {\n";

  if (elem->id == FieldType::Array) {
    auto* arr = car(elem);
    auto* wt = arr->real_type();
    auto const [elem_size, elem_align] = get_type_size_align(wt);
    (void)elem_align;

    oh << "  ::nprpc::impl::flat::StreamChunk_Direct __chunk(buf, sizeof(::nprpc::impl::Header));\n";
    oh << "  ";
    emit_type(stream_type, oh);
    oh << " __result{};\n";

    if (is_flat(wt)) {
      oh << "  std::memcpy(__result.data(), __chunk.data().data(), "
         << arr->length << " * " << elem_size << ");\n";
    } else {
      oh << "  auto __span = __chunk.data();\n";
      oh << "  ::nprpc::flat_buffer __elem_buf;\n";
      oh << "  auto __mb = __elem_buf.prepare(__span.size());\n";
      oh << "  std::memcpy(__mb.data(), __span.data(), __span.size());\n";
      oh << "  __elem_buf.commit(__span.size());\n";
      oh << "  auto& __arr = *reinterpret_cast<::nprpc::flat::Array<";
      emit_flat_type(wt, oh);
      oh << ", " << arr->length << ">*>(__elem_buf.data().data());\n";
      oh << "  auto __items = ::nprpc::flat::Span_ref<";
      emit_flat_type(wt, oh);
      oh << ", ";
      emit_direct_type(wt, oh);
      oh << ">(__elem_buf, __arr.range(__elem_buf.data().data()));\n";
      oh << "  size_t __index = 0;\n";
      oh << "  for (auto __item : __items) {\n";
      bd = 2;
      assign_from_flat_type(wt, "__result[__index]", "__item", oh, true, false);
      bd = 1;
      oh << "    ++__index;\n";
      oh << "  }\n";
    }

    oh << "  return __result;\n";
    oh << "}\n} // namespace nprpc_stream\n\n";

    always_full_namespace(false);
    bd = bd_saved;
    return;
  }

  oh << "  ::nprpc::impl::flat::StreamChunk_Direct __chunk(buf, sizeof(::nprpc::impl::Header));\n";
  oh << "  auto __span = __chunk.data();\n";
  oh << "  ::nprpc::flat_buffer __elem_buf;\n";
  oh << "  auto __mb = __elem_buf.prepare(__span.size());\n";
  oh << "  std::memcpy(__mb.data(), __span.data(), __span.size());\n";
  oh << "  __elem_buf.commit(__span.size());\n";
  oh << "  ";
  emit_type(stream_type, oh);
  oh << " __result;\n";
  oh << "  ";
  emit_direct_type(stream_type, oh);
  oh << " __d(__elem_buf, 0);\n";
  // top_object=true: __d is the Direct object itself, use . not ().
  assign_from_flat_type(stream_type, "__result", "__d", oh, false, true);
  oh << "  return __result;\n";
  oh << "}\n} // namespace nprpc_stream\n\n";

  always_full_namespace(false);
  bd = bd_saved;
}

void CppBuilder::emit_stream_serialize(AstTypeDecl* stream_type, bool direct)
{
  if (stream_type == nullptr)
    return;
  // Only needed for non-fundamental, non-direct element types.
  auto* elem = stream_type;
  if (elem->id == FieldType::Alias) elem = calias(elem)->get_real_type();
  if (elem->id == FieldType::Fundamental || elem->id == FieldType::Enum) return;
  if (direct) return;

  auto bd_saved = bd; bd = 1;
  always_full_namespace(true);

  auto root_size = 8;
  auto extra_capacity = 128;
  if (stream_type->id == FieldType::Struct) {
    auto* s = cflat(stream_type);
    root_size = s->size;
    extra_capacity = s->flat ? 0 : 128;
  }

  oh << "namespace nprpc_stream {\n";
  oh << "template<>\n";
  oh << "inline ::nprpc::flat_buffer serialize<";
  emit_type(stream_type, oh);
  oh << ">(const ";
  emit_type(stream_type, oh);
  oh << "& value) {\n";

  if (elem->id == FieldType::Array) {
    auto* arr = car(elem);
    auto* wt = arr->real_type();
    auto const [elem_size, elem_align] = get_type_size_align(wt);
    auto initial_capacity = arr->length * elem_size;
    if (!is_flat(wt))
      initial_capacity += 128 * arr->length;

    oh << "  ::nprpc::flat_buffer __buf;\n";
    oh << "  __buf.prepare(" << initial_capacity << ");\n";
    oh << "  __buf.commit(" << arr->length * elem_size << ");\n";
    oh << "  auto& __arr = *reinterpret_cast<::nprpc::flat::Array<";
    emit_flat_type(wt, oh);
    oh << ", " << arr->length << ">*>(__buf.data().data());\n";

    if (is_flat(wt)) {
      oh << "  auto __span = static_cast<::nprpc::flat::Span<";
      emit_flat_type(wt, oh);
      oh << ">>(__arr);\n";
      oh << "  std::memcpy(__span.data(), value.data(), " << arr->length << " * " << elem_size << ");\n";
    } else {
      oh << "  auto __items = ::nprpc::flat::Span_ref<";
      emit_flat_type(wt, oh);
      oh << ", ";
      emit_direct_type(wt, oh);
      oh << ">(__buf, __arr.range(__buf.data().data()));\n";
      oh << "  auto __it = value.begin();\n";
      oh << "  for (auto __item : __items) {\n";
      bd = 2;
      oh << bd << "auto __ptr = ::nprpc::make_wrapper1(*__it);\n";
      assign_from_cpp_type(wt, "  __item", "__ptr", oh, true);
      oh << bd << "++__it;\n";
      bd = 1;
      oh << "  }\n";
    }

    oh << "  return __buf;\n";
    oh << "}\n} // namespace nprpc_stream\n\n";

    always_full_namespace(false);
    bd = bd_saved;
    return;
  }

  oh << "  ::nprpc::flat_buffer __buf;\n";
  oh << "  __buf.prepare(" << root_size;
  if (extra_capacity > 0)
    oh << " + " << extra_capacity;
  oh << ");\n";
  oh << "  __buf.commit(" << root_size << ");\n";
  oh << "  ";
  emit_direct_type(stream_type, oh);
  oh << " __d(__buf, 0);\n";
  // top_type=true: __d is the Direct object itself, use . not ().
  assign_from_cpp_type(stream_type, "__d", "value", oh, false, true);
  oh << "  return __buf;\n";
  oh << "}\n} // namespace nprpc_stream\n\n";

  always_full_namespace(false);
  bd = bd_saved;
}

void CppBuilder::emit_interface(AstInterfaceDecl* ifs)
{
  // Servant definition
  oh << "class " << export_macro_name_ << " I" << ifs->name << "_Servant\n";
  if (ifs->plist.size()) {
    oh << "  : public I" << ifs->plist[0]->name << "_Servant\n";
    for (size_t i = 1; i < ifs->plist.size(); ++i) {
      oh << "  , public I" << ifs->plist[i]->name << "_Servant\n";
    }
    oh << "{\n";
  } else {
    oh << "  : public virtual ::nprpc::ObjectServant\n{\n";
  }

  oh << "public:\n"
        "  static std::string_view _get_class() noexcept { return \""
     << ctx_->current_file() << '/' << ctx_->nm_cur()->full_idl_namespace() << '.' << ifs->name
     << "\"; }\n"
        "  std::string_view get_class() const noexcept override { return I"
     << ifs->name
     << "_Servant::_get_class(); }\n"
        "  void dispatch(::nprpc::SessionContext& ctx, [[maybe_unused]] bool "
        "from_parent) override;\n";

  for (auto fn : ifs->fns) {
    oh << "  virtual ";
    if (fn->is_stream) {
      switch (fn->stream_kind) {
      case StreamKind::Server:
        emit_stream_writer_type(*this, fn->stream_decl->stream_out_type(), oh,
          [this](AstTypeDecl* t, std::ostream& os){ emit_type(t, os); });
        break;
      case StreamKind::Client:
      case StreamKind::Bidi:
        oh << "::nprpc::Task<>";
        break;
      default:
        assert(false);
      }
    } else {
      emit_type(fn->ret_value, oh);
    }
    oh << ' ' << fn->name << " ";
    if (fn->is_stream && fn->stream_kind != StreamKind::Server) {
      oh << '(';
      bool first = true;
      for (auto arg : fn->args) {
        if (arg->type->id == FieldType::Stream)
          continue;
        if (!first)
          oh << ", ";
        first = false;
        emit_type(arg->type, oh);
        oh << ' ' << arg->name;
      }
      if (fn->stream_kind == StreamKind::Client) {
        if (!first)
          oh << ", ";
        emit_stream_reader_type(*this, fn->stream_decl, oh,
          [this](AstTypeDecl* t, std::ostream& os){ emit_type(t, os); },
          [this](AstTypeDecl* t, std::ostream& os){ emit_direct_type(t, os); });
        oh << ' ' << fn->stream_arg->name;
      } else if (fn->stream_kind == StreamKind::Bidi) {
        if (!first)
          oh << ", ";
        oh << "::nprpc::BidiStream<";
        emit_type(fn->stream_decl->stream_in_type(), oh);
        oh << ", ";
        emit_type(fn->stream_decl->stream_out_type(), oh);
        oh << "> stream";
      }
      oh << ')';
    } else {
      emit_function_arguments(
          fn, oh, std::bind(&CppBuilder::emit_parameter_type_for_servant_callback, this, _1, _2));
    }
    oh << " = 0;\n";
  }

  oh << "};\n\n";

  // Proxy definition
  oh << "class " << export_macro_name_ << " " << ifs->name << "\n";

  if (ifs->plist.size()) {
    oh << "  : public " << ifs->plist[0]->name << "\n";
    for (size_t i = 1; i < ifs->plist.size(); ++i) {
      oh << "  , public " << ifs->plist[i]->name << "\n";
    }
    oh << "{\n";
  } else {
    oh << "  : public virtual ::nprpc::Object\n{\n";
  }

  oh << "  const uint8_t interface_idx_;\n"
        "public:\n"
        "  using servant_t = I"
     << ifs->name << "_Servant;\n\n";

  if (ifs->plist.empty()) {
    oh << "  " << ifs->name << "(uint8_t interface_idx) : interface_idx_(interface_idx) {}\n";
  } else {
    oh << "  " << ifs->name << "(uint8_t interface_idx)\n";

    auto count_all = [](AstInterfaceDecl* /* ifs_inherited */, int& n) { ++n; };

    int n = 1;
    for (auto parent : ifs->plist) {
      oh << (n == 1 ? "    : " : "    , ") << parent->name << "(interface_idx + " << n << ")\n";
      dfs_interface(std::bind(count_all, _1, std::ref(n)), parent);
    }

    oh << "    , interface_idx_(interface_idx)\n"
          "  {\n"
          "  }\n";
  }

  // functions definitions
  for (auto& fn : ifs->fns) {
    oh << "  ";
    if (fn->is_stream) {
      emit_stream_proxy_return_type(*this, fn, oh,
        [this](AstTypeDecl* t, std::ostream& os){ emit_type(t, os); },
        [this](AstTypeDecl* t, std::ostream& os){ emit_direct_type(t, os); });
    } else {
      emit_type(fn->ret_value, oh);
    }
    oh << ' ' << fn->name << " ";
    oh << proxy_arguments(fn) << ";\n";
    // Coroutine variant for reliable, non-async, non-stream TCP methods
    if (!fn->is_async && !fn->is_stream && fn->is_reliable) {
      oh << "  ::nprpc::Task<";
      emit_type(fn->ret_value, oh);
      oh << "> " << fn->name << "Async ";
      // Append the stop_token to the Async overload; proxy_arguments already
      // emits the closing ')' so we need a separate emission here.
      {
        auto args = proxy_arguments(fn);
        // args ends with ')'; strip it and append the stop_token default arg.
        oh << args.substr(0, args.size() - 1);
        if (args.size() > 2) oh << ", "; // non-empty arg list
        oh << "std::stop_token st = {});\n";
      }
    }
  }

  oh << "};\n\n";

  // .CPP file marshall/unmarshall stuff below
  // auto const nm = ctx_->nm_cur()->to_cpp17_namespace();

  for (auto fn : ifs->fns) {
    // Collect stream functions for nprpc_stream codec emission at file end (outside any namespace)
    if (fn->is_stream) {
      stream_codec_fns_.push_back(fn);
    }

    if (fn->is_stream) {
      emit_stream_proxy_return_type(*this, fn, oc,
        [this](AstTypeDecl* t, std::ostream& os){ emit_type(t, os); },
        [this](AstTypeDecl* t, std::ostream& os){ emit_direct_type(t, os); });
    } else {
      emit_type(fn->ret_value, oc);
    }
    oc << ' ' << ns(ctx_->nm_cur()) << ifs->name << "::" << fn->name;
    oc << proxy_arguments(fn) << " {\n";

    // Stream methods use different protocol flow
    if (fn->is_stream) {
      switch (fn->stream_kind) {
      case StreamKind::Server:
        proxy_stream_call(fn);
        break;
      case StreamKind::Client:
        proxy_client_stream_call(fn);
        break;
      case StreamKind::Bidi:
        proxy_bidi_stream_call(fn);
        break;
      default:
        assert(false);
      }
      oc << "}\n\n";
      continue;
    }

    // For sync reliable calls bind the TLS bump arena to avoid malloc/realloc
    // during request serialization.  Async calls skip this because the buffer
    // is moved into the io_context and the calling thread can reset the arena
    // immediately after returning — which would corrupt in-flight data.
    if (fn->is_reliable && !fn->is_async) {
      oc << "  auto& __arena = ::nprpc::impl::tls_bump_arena();\n"
            "  __arena.reset();\n"
            "  ::nprpc::flat_buffer buf;\n"
            "  buf.set_arena(&__arena);\n";
    } else {
      oc << "  ::nprpc::flat_buffer buf;\n";
    }

    const auto fixed_size = get_arguments_offset() + (fn->in_s ? fn->in_s->size : 0);
    const auto capacity = fixed_size + (fn->in_s ? (fn->in_s->flat ? 0 : 128) : 0);

    // Try zero-copy buffer for shared memory transport
    oc << "  auto session = "
          "::nprpc::impl::g_rpc->get_session(this->get_endpoint());\n"
          "  if "
          "(!::nprpc::impl::g_rpc->prepare_zero_copy_buffer(session->ctx(), buf, "
       << capacity
       << "))\n"
          "    buf.prepare("
       << capacity
       << ");\n"
          "  {\n"
          "    buf.commit("
       << fixed_size
       << ");\n"
          "    "
          "static_cast<::nprpc::impl::Header*>(buf.data().data())->msg_id = "
          "::nprpc::impl::MessageId::FunctionCall;\n"
          "  static_cast<::nprpc::impl::Header*>(buf.data().data())->msg_type ="
          "::nprpc::impl::MessageType::Request;\n"
          "  }\n"
          "  ::nprpc::impl::flat::CallHeader_Direct __ch(buf, sizeof(::nprpc::impl::Header));\n"
          "  __ch.object_id() = this->object_id();\n"
          "  __ch.poa_idx() = this->poa_idx();\n"
          "  __ch.interface_idx() = interface_idx_;\n"
          "  __ch.function_idx() = "
       << fn->idx << ";\n";

    if (fn->in_s) {
      oc << "  " << fn->in_s->name << "_Direct _(buf," << get_arguments_offset() << ");\n";
    }

    int ix = 0;
    for (auto in : fn->args) {
      if (in->modifier == ArgumentModifier::Out)
        continue;
      bd = 1;
      assign_from_cpp_type(in->type, "_._" + std::to_string(++ix), in->name, oc);
    }

    oc << "  static_cast<::nprpc::impl::Header*>(buf.data().data())->size "
          "= "
          "static_cast<uint32_t>(buf.size());\n";

    // Choose the call method based on interface/function attributes
    if (!fn->is_reliable) {
      // Unreliable (e.g., QUIC DATAGRAM) - fire-and-forget
      proxy_unreliable_call(fn);
    } else if (!fn->is_async) {
      proxy_call(fn);
    } else {
      proxy_async_call(fn);
    }

    oc << "}\n\n";

    // Emit coroutine (Async) variant for reliable, non-async, non-stream TCP methods
    if (!fn->is_async && !fn->is_stream && fn->is_reliable) {
      oc << "::nprpc::Task<";
      emit_type(fn->ret_value, oc);
      oc << ">\n";
      oc << ns(ctx_->nm_cur()) << ifs->name << "::" << fn->name << "Async";
      {
        auto args = proxy_arguments(fn);
        oc << args.substr(0, args.size() - 1);
        if (args.size() > 2) oc << ", ";
        oc << "std::stop_token st) {\n";
      }
      // Early cancellation check — works for all transports; in-flight cancellation
      // is an additional capability of UringClientConnection.
      oc << "  if (st.stop_requested()) throw nprpc::OperationCancelled();\n";
      // Coro: plain heap buffer — no TLS arena (unsafe across co_await thread switch)
      oc << "  ::nprpc::flat_buffer buf;\n";
      oc << "  auto session = "
            "::nprpc::impl::g_rpc->get_session(this->get_endpoint());\n"
            "  if "
            "(!::nprpc::impl::g_rpc->prepare_zero_copy_buffer(session->ctx(), buf, "
         << capacity
         << "))\n"
            "    buf.prepare("
         << capacity
         << ");\n"
            "  {\n"
            "    buf.commit("
         << fixed_size
         << ");\n"
            "    "
            "static_cast<::nprpc::impl::Header*>(buf.data().data())->msg_id = "
            "::nprpc::impl::MessageId::FunctionCall;\n"
            "  static_cast<::nprpc::impl::Header*>(buf.data().data())->msg_type ="
            "::nprpc::impl::MessageType::Request;\n"
            "  }\n"
            "  ::nprpc::impl::flat::CallHeader_Direct __ch(buf, sizeof(::nprpc::impl::Header));\n"
            "  __ch.object_id() = this->object_id();\n"
            "  __ch.poa_idx() = this->poa_idx();\n"
            "  __ch.interface_idx() = interface_idx_;\n"
            "  __ch.function_idx() = "
         << fn->idx << ";\n";
      if (fn->in_s) {
        oc << "  " << fn->in_s->name << "_Direct _(buf," << get_arguments_offset() << ");\n";
      }
      {
        int ix = 0;
        for (auto in : fn->args) {
          if (in->modifier == ArgumentModifier::Out)
            continue;
          bd = 1;
          assign_from_cpp_type(in->type, "_._" + std::to_string(++ix), in->name, oc);
        }
      }
      oc << "  static_cast<::nprpc::impl::Header*>(buf.data().data())->size "
            "= "
            "static_cast<uint32_t>(buf.size());\n";
      proxy_call_coro(fn);
      oc << "}\n\n";
    }
  }

  // Servant dispatch
  oc << "void " << ns(ctx_->nm_cur()) << 'I' << ifs->name
     << "_Servant::dispatch(::nprpc::SessionContext& ctx, [[maybe_unused]] "
        "bool from_parent) {\n"
        "  assert(ctx.rx_buffer != nullptr);\n";

  oc << "  auto* header = "
        "static_cast<::nprpc::impl::Header*>(ctx.rx_buffer->data().data());\n";

  // Check for StreamInit
  oc << "  if (header->msg_id == "
        "::nprpc::impl::MessageId::StreamInitialization) {\n"
        "    ::nprpc::impl::flat::StreamInit_Direct init(*ctx.rx_buffer, "
        "sizeof(::nprpc::impl::Header));\n";

  // Dispatch based on func_idx from StreamInit
  oc << "    switch(init.func_idx()) {\n";
  for (auto fn : ifs->fns) {
    if (fn->is_stream) {
      oc << "      case " << fn->idx << ": {\n";

      // Unmarshal input arguments if any
      if (fn->in_s) {
        oc << "        " << fn->in_s->name << "_Direct ia(*ctx.rx_buffer, "
           << get_stream_init_arguments_offset() << ");\n";
        if (ifs->trusted == false) {
           oc << "        if ( !check_" << make_safety_check_name(fn->in_s)
             << "(*ctx.rx_buffer, ia) ) {\n"
                "          ::nprpc::impl::make_simple_answer(ctx, "
                "::nprpc::impl::MessageId::Error_BadInput);\n"
                "          break;\n"
                "        }\n";
        }
      }
  void emit_servant_stream_dispatch(AstInterfaceDecl* ifs, AstFunctionDecl* fn);

      if (fn->stream_kind == StreamKind::Server) {
        if (fn->ex)
          oc << "        try {\n";
        oc << "        auto writer = " << fn->name << "(";
        int in_ix = 0;
        for (auto arg : fn->args) {
          if (arg->modifier == ArgumentModifier::Out)
            continue;
          if (in_ix > 0)
            oc << ", ";
          oc << "ia._" << ++in_ix << "()";
        }
        oc << ");\n";

        oc << "        writer.set_manager(ctx.stream_manager, "
              "init.stream_id());\n";
        oc << "        ctx.stream_manager->register_stream(init.stream_id(), "
              "std::make_unique<::nprpc::StreamWriter<";
        emit_type(fn->stream_decl->stream_out_type(), oc);
        oc << ">>(std::move(writer)), " << (fn->is_reliable ? "false" : "true") << ");\n";
        oc << "        ctx.stream_manager->defer_stream_start(init.stream_id());\n";
        if (fn->ex)
          emit_declared_exception_reply(fn, oc, "        ");
      } else {
        int in_ix = 0;
        int stable_ix = 0;
        for (auto arg : fn->args) {
          if (arg->modifier == ArgumentModifier::Out || arg->type->id == FieldType::Stream)
            continue;
          oc << "        ";
          emit_type(arg->type, oc);
          oc << " __arg" << ++stable_ix << ";\n";
          bd = 1;
          assign_from_flat_type(arg->type,
                                "__arg" + std::to_string(stable_ix),
                                "ia._" + std::to_string(++in_ix),
                                oc);
          oc << "\n";
        }

        if (fn->stream_kind == StreamKind::Client) {
          if (fn->ex)
            oc << "        try {\n";
          oc << "        ";
          emit_stream_reader_type(*this, fn->stream_decl, oc,
            [this](AstTypeDecl* t, std::ostream& os){ emit_type(t, os); },
            [this](AstTypeDecl* t, std::ostream& os){ emit_direct_type(t, os); });
          oc << " __stream(ctx, init.stream_id());\n";
          oc << "        auto __task = this->" << fn->name << "(";
          bool first = true;
          for (int i = 1; i <= stable_ix; ++i) {
            if (!first)
              oc << ", ";
            first = false;
            oc << "std::move(__arg" << i << ")";
          }
          if (!first)
            oc << ", ";
          oc << "std::move(__stream));\n";
          oc << "        if (__task.done()) __task.rethrow_if_exception();\n";
          oc << "        ctx.stream_manager->start_task_after_reply(init.stream_id(), std::move(__task));\n";
          if (fn->ex)
            emit_declared_exception_reply(fn, oc, "        ");
        } else {
          if (fn->ex)
            oc << "        try {\n";
          oc << "        ";
          emit_stream_reader_value_type(fn->stream_decl->stream_in_type(), false, oc,
            [this](AstTypeDecl* t, std::ostream& os){ emit_type(t, os); },
            [this](AstTypeDecl* t, std::ostream& os){ emit_direct_type(t, os); });
          oc << " __reader(ctx, init.stream_id());\n";
          oc << "        ";
          emit_stream_writer_type(*this, fn->stream_decl->stream_out_type(), oc,
            [this](AstTypeDecl* t, std::ostream& os){ emit_type(t, os); });
          oc << " __writer(ctx, init.stream_id());\n";
          oc << "        ::nprpc::BidiStream<";
          emit_type(fn->stream_decl->stream_in_type(), oc);
          oc << ", ";
          emit_type(fn->stream_decl->stream_out_type(), oc);
          oc << "> __stream(std::move(__reader), std::move(__writer));\n";
          oc << "        auto __task = this->" << fn->name << "(";
          bool first = true;
          for (int i = 1; i <= stable_ix; ++i) {
            if (!first)
              oc << ", ";
            first = false;
            oc << "std::move(__arg" << i << ")";
          }
          if (!first)
            oc << ", ";
          oc << "std::move(__stream));\n";
          oc << "        if (__task.done()) __task.rethrow_if_exception();\n";
          oc << "        ctx.stream_manager->start_task_after_reply(init.stream_id(), std::move(__task));\n";
          if (fn->ex)
            emit_declared_exception_reply(fn, oc, "        ");
        }
      }

      oc << "        break;\n";
      oc << "      }\n";
    }
  }
  oc << "      default:\n"
      "        ::nprpc::impl::make_simple_answer(ctx, ::nprpc::impl::MessageId::Error_UnknownFunctionIdx);\n"
      "        break;\n"
        "    }\n"
        "    return;\n"
        "  }\n";

  oc << "  ::nprpc::impl::flat::CallHeader_Direct __ch(*ctx.rx_buffer, "
        "sizeof(::nprpc::impl::Header));\n";

  if (ifs->plist.empty()) {
    // ok
  } else {
    oc << "  if (from_parent == false) {\n"
          "    switch(__ch.interface_idx()) {\n"
          "      case 0:\n"
          "        break;\n";

    int ix = 1;
    auto select_interface = [&ix, this, ifs](AstInterfaceDecl* i) {
      if (i == ifs)
        return;
      oc << "      case " << ix
         << ":\n"
            "        I"
         << i->name
         << "_Servant::dispatch(ctx, true);\n"
            "        return;\n";
      ++ix;
    };

    dfs_interface(select_interface, ifs);

    oc << "      default:\n"
          //"        assert(false);\n"
          "        throw \"unknown interface\";\n"
          "    }\n"
          "  }\n";
  }

  oc << "  switch(__ch.function_idx()) {\n";

  for (auto fn : ifs->fns) {
    // Skip stream functions - they are handled by StreamInitialization above
    if (fn->is_stream) {
      continue;
    }

    oc << "    case " << fn->idx << ": {\n";

    if (fn->in_s) {
      oc << "      assert(ctx.rx_buffer != nullptr);\n"
            "      "
         << fn->in_s->name << "_Direct ia(*ctx.rx_buffer, " << get_arguments_offset() << ");\n";
      if (ifs->trusted == false) {
        // const auto fixed_size = get_arguments_offset() +
        // fn->in_s->size;
        oc << "      if ( !check_" << make_safety_check_name(fn->in_s)
           << "(*ctx.rx_buffer, ia) ) {\n"
              "        ::nprpc::impl::make_simple_answer(ctx, "
              "::nprpc::impl::MessageId::Error_BadInput);\n"
              "        break;\n"
              "      }\n";
      }
    }

    if (fn->out_s && !fn->out_s->flat) {
      const auto offset = size_of_header;
      const auto initial_size = offset + fn->out_s->size;
      oc << "      assert(ctx.tx_buffer != nullptr);\n"
            "      auto& obuf = *ctx.tx_buffer;\n"
            "      obuf.consume(obuf.size());\n" // Clear buffer
            "      if "
            "(!::nprpc::impl::g_rpc->prepare_zero_copy_buffer(ctx, "
            "obuf, "
         << initial_size + 128
         << "))\n"
            "         obuf.prepare("
         << initial_size + 128
         << ");\n"
            "      obuf.commit("
         << initial_size
         << ");\n"
            "      "
         << fn->out_s->name << "_Direct oa(obuf," << offset << ");\n";
    }
    auto bd0 = bd;
    bd = 3;
    if (!fn->is_void()) {
      oc << bd;
      emit_type(fn->ret_value, oc);
      oc << " __ret_val;\n";
    }

    // Create stack variables for output parameters BEFORE the try block
    // For flat output structs: ALL out parameters need stack variables
    // For non-flat output structs: only Vector/String/Struct need temporary
    // variables (for _d() access)
    size_t out_ix = fn->is_void() ? 0 : 1, out_temp_ix = 0;

    auto passed_as_direct = [](AstFunctionArgument* arg) {
      auto real_type = arg->type;
      if (real_type->id == FieldType::Alias)
        real_type = calias(real_type)->get_real_type();

      return arg->modifier == ArgumentModifier::Out &&
             (real_type->id == FieldType::Vector || real_type->id == FieldType::String ||
              real_type->id == FieldType::Struct);
    };

    // Helper to check if an argument is a Struct type (resolving aliases)
    auto is_struct_type = [](AstFunctionArgument* arg) {
      auto real_type = arg->type;
      if (real_type->id == FieldType::Alias)
        real_type = calias(real_type)->get_real_type();
      return real_type->id == FieldType::Struct || real_type->id == FieldType::Array;
    };

    // For flat output structs with Struct-type outputs, we need to prepare
    // the output buffer BEFORE calling the servant so we can pass _Direct types
    bool flat_has_struct_out = false;
    if (fn->out_s && fn->out_s->flat) {
      for (auto arg : fn->args) {
        if (arg->modifier == ArgumentModifier::Out && is_struct_type(arg)) {
          flat_has_struct_out = true;
          break;
        }
      }
    }

    if (flat_has_struct_out) {
      // Prepare output buffer early for flat structs with Struct-type outputs
      const auto offset = size_of_header;
      const auto initial_size = offset + fn->out_s->size;
      oc << bd << "assert(ctx.tx_buffer != nullptr);\n"
         << bd << "auto& obuf = *ctx.tx_buffer;\n"
         << bd << "obuf.consume(obuf.size());\n"
         << bd << "if (!::nprpc::impl::g_rpc->prepare_zero_copy_buffer(ctx, obuf, "
         << initial_size << "))\n"
         << bd << "  obuf.prepare(" << initial_size << ");\n"
         << bd << "obuf.commit(" << initial_size << ");\n"
         << bd << fn->out_s->name << "_Direct oa(obuf," << offset << ");\n";
    }

    if (fn->out_s && fn->out_s->flat) {
      // For flat output structs, create stack variables for non-Struct output
      // parameters. Struct outputs use _Direct accessors from the output buffer.
      for (auto arg : fn->args) {
        if (arg->modifier != ArgumentModifier::Out)
          continue;
        ++out_ix;
        if (!is_struct_type(arg)) {
          oc << bd;
          emit_type(arg->type, oc);
          oc << " _out_" << out_ix << ";\n";
        }
      }
    } else if (fn->out_s) {
      // For non-flat output structs, create temporary variables only for
      // complex types
      for (auto arg : fn->args) {
        if (arg->modifier != ArgumentModifier::Out)
          continue;

        if (!passed_as_direct(arg)) {
          ++out_ix;
          continue;
        }

        auto real_type = arg->type;
        if (real_type->id == FieldType::Alias)
          real_type = calias(real_type)->get_real_type();

        oc << bd << "auto oa_" << ++out_temp_ix << " = oa._" << ++out_ix
           << ((real_type->id == FieldType::Vector || real_type->id == FieldType::String)
                   ? "_d();\n"
                   : "();\n");
      }
    }

    if (fn->ex)
      oc << bd++ << "try {\n";

    oc << bd << (fn->is_void() ? "" : "__ret_val = ") << fn->name << "(";

    size_t in_ix = 0, idx = 0;
    out_ix = fn->is_void() ? 0 : 1;
    out_temp_ix = 0;
    for (auto arg : fn->args) {
      if (arg->modifier == ArgumentModifier::Out) {
        assert(fn->out_s);
        if (fn->out_s->flat) {
          ++out_ix;
          if (is_struct_type(arg)) {
            // For Struct types in flat output structs, pass _Direct accessor
            oc << "oa._" << out_ix << "()";
          } else {
            // For non-Struct types, pass stack variable reference
            oc << "_out_" << out_ix;
          }
        } else if (passed_as_direct(arg)) {
          // For non-flat structs with complex types, pass temporary
          // variable
          oc << "oa_" << ++out_temp_ix;
          ++out_ix;
        } else {
          // For non-flat structs with simple types, pass direct
          // reference
          oc << "oa._" << ++out_ix << "()";
        }
      } else {
        if (arg->type->id == FieldType::Object) {
          oc << "::nprpc::impl::create_object_from_flat(ia._" << ++in_ix
             << "(), ctx.remote_endpoint)";
        } else {
          oc << "ia._" << ++in_ix << "()";
        }
      }
      if (++idx != fn->args.size())
        oc << ", ";
    }
    oc << ");\n";

    /*
    out_ix = fn->is_void() ? 0 : 1;
    for (auto arg : fn->args) {
      if (arg->modifier == ArgumentModifier::Out) {
        ++out_ix;
        if (arg->type->id == FieldType::Object) {
          oc <<
            "{\n"
            "  auto obj = impl::g_rpc->get_object(" << "oa._" << out_ix <<
    "().poa_idx(), " << "oa._" << out_ix << "().object_id());\n" "  if (obj)
    if (auto real_obj = (*obj).get(); real_obj)
    ref_list.add_ref(real_obj);\n"
            "}\n"
            ;
        }
      }
    }
    */

     if (fn->is_throwing()) {
      auto declared_exceptions = fn->exceptions;
      if (declared_exceptions.empty() && fn->ex)
        declared_exceptions.push_back(fn->ex);

      const auto offset = size_of_header;
        oc << "      }\n";
      for (auto* ex : declared_exceptions) {
        const auto initial_size = offset + ex->size;

        always_full_namespace(true);
          oc << "      catch("
          << ns(ex->nm) << ex->name
          << "& e) {\n"
            "        assert(ctx.tx_buffer != nullptr);\n"
            "        auto& obuf = *ctx.tx_buffer;\n"
            "        obuf.consume(obuf.size());\n"
            "        if "
            "(!::nprpc::impl::g_rpc->prepare_zero_copy_buffer(ctx, "
            "obuf, "
          << initial_size
          << "))\n"
            "          obuf.prepare("
          << initial_size
          << ");\n"
            "        obuf.commit("
          << initial_size
          << ");\n"
            "        "
          << ns(ex->nm) << "flat::" << ex->name << "_Direct oa(obuf," << offset
          << ");\n"
            "        oa.__ex_id() = "
          << ex->exception_id << ";\n";
        always_full_namespace(false);

        for (size_t i = 1; i < ex->fields.size(); ++i) {
         auto mb = ex->fields[i];
         assign_from_cpp_type(mb->type, "oa." + mb->name, "e." + mb->name, oc);
        }

        oc << "        "
            "static_cast<::nprpc::impl::Header*>(obuf.data().data())->"
            "size = "
            "static_cast<uint32_t>(obuf.size());\n"
            "        "
            "static_cast<::nprpc::impl::Header*>(obuf.data().data())->"
            "msg_id = "
            "::nprpc::impl::MessageId::Exception;\n"
            "        "
            "static_cast<::nprpc::impl::Header*>(obuf.data().data())->"
            "msg_type "
            "= ::nprpc::impl::MessageType::Answer;\n"
            "        return;\n"
            "      }\n";
      }
    }

    if (!fn->out_s) {
      oc << "      ::nprpc::impl::make_simple_answer(ctx, "
            "nprpc::impl::MessageId::Success);\n";
    } else {
      if (fn->out_s->flat) { // it means that we are writing output data
                             // in the input buffer,
        // so we must pass stack variables first and then assign result
        // back to the buffer. For Struct types, we already wrote directly
        // via _Direct accessors.
        const auto offset = size_of_header;
        const auto initial_size = offset + fn->out_s->size;

        if (!flat_has_struct_out) {
          // Only prepare buffer here if we didn't already do it for Struct outputs
          oc << "      assert(ctx.tx_buffer != nullptr);\n"
                "      auto& obuf = *ctx.tx_buffer;\n"
                "      obuf.consume(obuf.size());\n"
                "      if "
                "(!::nprpc::impl::g_rpc->prepare_zero_copy_buffer(ctx, "
                "obuf, "
             << initial_size
             << "))\n"
                "        obuf.prepare("
             << initial_size
             << ");\n"
                "      obuf.commit("
             << initial_size
             << ");\n"
                "      "
             << fn->out_s->name << "_Direct oa(obuf," << offset << ");\n";
        }

        int ix;
        if (!fn->is_void()) {
          assign_from_cpp_type(fn->ret_value, "oa._1", "__ret_val", oc);
          ix = 1;
        } else {
          ix = 0;
        }

        for (auto out : fn->args) {
          if (out->modifier == ArgumentModifier::In)
            continue;
          ++ix;
          // Skip Struct types - they were written directly via _Direct
          if (!is_struct_type(out)) {
            auto n = std::to_string(ix);
            assign_from_cpp_type(out->type, "oa._" + n, "_out_" + n, oc);
          }
        }
      } else if (!fn->is_void()) {
        assign_from_cpp_type(fn->ret_value, "oa._1", "__ret_val", oc);
      }

      oc << "      "
            "static_cast<::nprpc::impl::Header*>(obuf.data().data())->"
            "size = "
            "static_cast<uint32_t>(obuf.size());\n"
            "      "
            "static_cast<::nprpc::impl::Header*>(obuf.data().data())->"
            "msg_id = "
            "::nprpc::impl::MessageId::BlockResponse;\n"
            "      "
            "static_cast<::nprpc::impl::Header*>(obuf.data().data())->"
            "msg_type "
            "= ::nprpc::impl::MessageType::Answer;\n";
    }

    oc << "      break;\n"
          "    }\n";

    bd = bd0;
  }

  oc << "    default:\n"
        "      ::nprpc::impl::make_simple_answer(ctx, "
        "::nprpc::impl::MessageId::Error_UnknownFunctionIdx);\n"
        "  }\n"; // switch block
  ;

  oc << "}\n\n"; // dispatch
}

void CppBuilder::emit_using(AstAliasDecl* u)
{
  oh << "using " << u->name << " = ";
  emit_type(u->type, oh);
  oh << ";\n";
}

void CppBuilder::emit_variant(AstVariantDecl* v)
{
  // Regular type: struct with Kind enum + std::variant
  oh << "struct " << v->name << " {\n";
  oh << "  enum class Kind : std::uint32_t {\n";
  for (size_t i = 0; i < v->arms.size(); ++i) {
    oh << "    " << v->arms[i].name << " = " << i;
    if (i + 1 < v->arms.size()) oh << ",";
    oh << "\n";
  }
  oh << "  };\n";
  oh << "  using value_type = std::variant<";
  for (size_t i = 0; i < v->arms.size(); ++i) {
    emit_type(v->arms[i].type, oh);
    if (i + 1 < v->arms.size()) oh << ", ";
  }
  oh << ">;\n";
  oh << "  Kind kind;\n";
  oh << "  value_type value;\n";
  oh << "};\n\n";

  // Flat type: discriminant + arm_offset (used by Direct accessor layer)
  oh << "namespace flat {\n";
  oh << "struct " << v->name << " {\n";
  oh << "  std::uint32_t kind;\n";
  oh << "  std::uint32_t arm_offset;\n";
  oh << "};\n";

  // Direct accessor
  const std::string accessor_name = v->name + "_Direct";
  oh << "class " << accessor_name << " {\n"
        "  ::nprpc::flat_buffer& buffer_;\n"
        "  const std::uint32_t offset_;\n"
        "  auto& base() noexcept { return *reinterpret_cast<" << v->name
     << "*>(reinterpret_cast<std::byte*>(buffer_.data().data()) + offset_); }\n"
        "  auto const& base() const noexcept { return *reinterpret_cast<const "
     << v->name
     << "*>(reinterpret_cast<const std::byte*>(buffer_.data().data()) + offset_); }\n"
        "public:\n"
        "  uint32_t offset() const noexcept { return offset_; }\n"
        "  void* __data() noexcept { return (void*)&base(); }\n"
        "  " << accessor_name << "(::nprpc::flat_buffer& buffer, std::uint32_t offset)\n"
        "    : buffer_(buffer), offset_(offset) {}\n";

  // Accessors — kind()/set_kind() use std::uint32_t so they're valid inside
  // namespace flat without needing to resolve the outer Kind enum type.
  oh << "  std::uint32_t kind() const noexcept { return base().kind; }\n";
  oh << "  void set_kind(std::uint32_t k) noexcept { base().kind = k; }\n";
  oh << "  std::uint32_t arm_offset() const noexcept { return base().arm_offset; }\n";
  oh << "  void set_arm_offset(std::uint32_t v) noexcept { base().arm_offset = v; }\n";

  // alloc_arm: grows the flat buffer to hold one arm struct, zero-inits it,
  // and stores the relative offset from the arm_offset field (offset_+4).
  oh << "  void alloc_arm(std::uint32_t arm_size, std::uint32_t arm_align) noexcept {\n"
        "    auto cur = static_cast<std::uint32_t>(buffer_.data().size());\n"
        "    auto rem = cur % arm_align;\n"
        "    auto pad = rem ? arm_align - rem : 0;\n"
        "    auto off = cur + pad;\n"
        "    auto ptr = static_cast<std::byte*>(buffer_.prepare(arm_size + pad).data());\n"
        "    std::memset(ptr, 0, arm_size + pad);\n"
        "    buffer_.commit(arm_size + pad);\n"
        "    set_arm_offset(off - (offset_ + 4));\n"
        "  }\n";

  // Per-arm value accessors — mirror the Optional_Direct<T,TD>::value() pattern:
  // complex types (Struct/String/Vector/Array) → Direct wrapper at arm_offset
  // simple types (Fundamental/Enum)            → T& at arm_offset
  for (auto& arm : v->arms) {
    auto* atype = arm.type;
    if (atype->id == FieldType::Alias) atype = calias(atype)->get_real_type();
    if (atype->id == FieldType::Fundamental) {
      const auto ftype = fundamental_to_flat(cft(atype)->token_id);
      oh << "  " << ftype << "& value_" << arm.name << "() noexcept { return "
         << "*reinterpret_cast<" << ftype
         << "*>(reinterpret_cast<std::byte*>(buffer_.data().data()) + offset_ + 4 + base().arm_offset); }\n";
    } else if (atype->id == FieldType::Enum) {
      std::ostringstream tmp;
      emit_type(atype, tmp);
      oh << "  " << tmp.str() << "& value_" << arm.name << "() noexcept { return "
         << "*reinterpret_cast<" << tmp.str()
         << "*>(reinterpret_cast<std::byte*>(buffer_.data().data()) + offset_ + 4 + base().arm_offset); }\n";
    } else {
      // Struct, String, Vector, Array — emit_direct_type covers all
      oh << "  auto value_" << arm.name << "() noexcept { return ";
      emit_direct_type(atype, oh);
      oh << "(buffer_, offset_ + 4 + base().arm_offset); }\n";
    }
  }

  oh << "};\n} // namespace flat\n\n";
}

void CppBuilder::emit_enum(AstEnumDecl* e)
{
  oh << "enum class " << e->name << " : " << fundamental_to_cpp(e->token_id) << " {\n";
  int64_t ix = 0;
  for (size_t i = 0; i < e->items.size(); ++i) {
    oh << "  " << e->items[i].first;
    auto const n = e->items[i].second;
    if (n.second || ix != n.first) { // explicit
      oh << " = " << n.first;
      ix = n.first.decimal() + 1;
    } else {
      ++ix;
    }
    if (i != e->items.size() - 1)
      oh << ",\n";
  }
  oh << "\n};\n";
}

CppBuilder::CppBuilder(Context* ctx, std::filesystem::path out_path)
    : Builder(ctx)
    , out_path_(out_path)
{
  if (!ctx)
    return;
  // Initialize export macro name early so it's available during
  // emit_interface
  auto make_guard = [](const std::string& file) {
    std::string r(file);
    std::transform(r.begin(), r.end(), r.begin(),
                   [](char c) { return c == '.' ? '_' : ::toupper(c); });
    return r;
  };
  auto module_upper = make_guard(ctx_->current_file());
  export_macro_name_ = module_upper + "_API";
}

} // namespace npidl::builders

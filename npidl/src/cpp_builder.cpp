// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

#include "cpp_builder.hpp"
#include <cassert>
#include <ios>
#include <string_view>
#include <algorithm>
#include <sstream>
#include "utils.hpp"
#include <iostream>
#include <set>
#include <boost/container/small_vector.hpp>

namespace npidl::builders {

using std::placeholders::_1;
using std::placeholders::_2;

void Builder::emit_arguments_structs(std::function<void(AstStructDecl*)> emitter) {
  always_full_namespace(true);
  for (auto& [unused, s] : ctx_->affa_list) emitter(s);
  always_full_namespace(false);
}

void Builder::make_arguments_structs(AstFunctionDecl* fn) {
  if (fn->arguments_structs_have_been_made) return;

  auto& in_args = fn->in_args;
  auto& out_args = fn->out_args;

  if (!fn->is_void()) {
    auto ret = new AstFunctionArgument();
    ret->name = "ret_val";
    ret->modifier = ArgumentModifier::Out;
    ret->type = fn->ret_value;
    out_args.push_back(ret);
  }

  for (auto arg : fn->args) {
    if (arg->modifier == ArgumentModifier::In) in_args.push_back(arg);
    else out_args.push_back(arg);
  }

  auto make_struct = [this, fn](std::vector<AstFunctionArgument*>& args) -> AstStructDecl* {
    if (args.empty()) return nullptr;

    auto s = new AstStructDecl();
    s->name = ctx_->current_file() + "_M" + std::to_string(++ctx_->m_struct_n_);
    s->exception_id = -1; // Mark as non-exception

    std::transform(args.begin(), args.end(), std::back_inserter(s->fields), [ix = 0, s, fn](AstFunctionArgument* arg) mutable {
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
  };

  fn->in_s = make_struct(in_args);
  fn->out_s = make_struct(out_args);

  fn->arguments_structs_have_been_made = true;
}

std::ostream& operator<<(std::ostream& os, const CppBuilder::_ns& ns) {
  if (ns.builder.always_full_namespace_) {
    os << "::" << ns.nm->to_cpp17_namespace() << "::";
    return os;
  }
  auto [sub, level] = Namespace::substract(ns.builder.ctx_->nm_cur(), ns.nm);

  if (sub)
    os << sub->to_cpp17_namespace(level) << "::";
  return os;
}

static std::string_view fundamental_to_cpp(TokenId id) {
  using namespace std::string_view_literals;
  switch (id) {
  case TokenId::Boolean: return "bool"sv;
  case TokenId::Int8: return "int8_t"sv;
  case TokenId::UInt8: return "uint8_t"sv;
  case TokenId::Int16: return "int16_t"sv;
  case TokenId::UInt16: return "uint16_t"sv;
  case TokenId::Int32: return "int32_t"sv;
  case TokenId::UInt32: return "uint32_t"sv;
  case TokenId::Int64: return "int64_t"sv;
  case TokenId::UInt64: return "uint64_t"sv;
  case TokenId::Float32: return "float"sv;
  case TokenId::Float64: return "double"sv;
  default: assert(false); return ""sv;
  }
}

static std::string_view fundamental_to_flat(TokenId id) {
  using namespace std::string_view_literals;
  switch (id) {
    case TokenId::Boolean: return "::nprpc::flat::Boolean"sv;
    default: return fundamental_to_cpp(id);
  }
}

void CppBuilder::emit_type(AstTypeDecl* type, std::ostream& os) {
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
  default:
    assert(false);
  }
}

void CppBuilder::emit_flat_type(AstTypeDecl* type, std::ostream& os) {
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
  default:
    assert(false);
  }
}

CppBuilder::_ns CppBuilder::ns(Namespace* nm) {
  return { *this, nm };
}

void CppBuilder::emit_parameter_type_for_proxy_call_r(AstTypeDecl* type, std::ostream& os, bool input) {
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
    if (input) os << "ObjectId";
    else os << "Object*";
    break;
  default:
    assert(false);
  }
}

void CppBuilder::emit_parameter_type_for_proxy_call(AstFunctionArgument* arg, std::ostream& os) {
  const bool input = (arg->modifier == ArgumentModifier::In);

  if (input && arg->type->id != FieldType::Fundamental && arg->type->id != FieldType::Vector) {
    os << "const ";
  }

  emit_parameter_type_for_proxy_call_r(arg->type, os, input);

  if (!input || (arg->type->id != FieldType::Fundamental && arg->type->id != FieldType::Vector)) {
    os << '&';
  }
}

void CppBuilder::emit_parameter_type_for_servant_callback_r(AstTypeDecl* type, std::ostream& os, const bool input) {
  switch (type->id) {
  case FieldType::Fundamental:
    os << fundamental_to_flat(cft(type)->token_id);
    break;
  case FieldType::Struct:
    emit_flat_type(type, os); os << "_Direct";
    break;
  case FieldType::Array:
  case FieldType::Vector: {
    auto wt = cwt(type)->real_type();
    if (input || type->id == FieldType::Array) {
      if (wt->id == FieldType::Fundamental || wt->id == FieldType::Enum) {
        os << "::nprpc::flat::Span<"; emit_flat_type(wt, os); os << ">";
      } else if (wt->id == FieldType::Struct) {
        os << "::nprpc::flat::Span_ref<"; emit_flat_type(wt, os); os << ", "; emit_flat_type(wt, os); os << "_Direct>";
      } 
    } else {
      if (wt->id == FieldType::Fundamental || wt->id == FieldType::Enum) {
        os << "/*out*/::nprpc::flat::Vector_Direct1<"; emit_parameter_type_for_servant_callback_r(wt, os, input); os << ">";
      } else if (wt->id == FieldType::Struct) {
        os << "/*out*/::nprpc::flat::Vector_Direct2<"; emit_flat_type(wt, os); os << ", ";
        emit_parameter_type_for_servant_callback_r(wt, os, input); os << ">";
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
      os << "::nprpc::flat::Optional_Direct<" <<
        emit_flat_type(copt(type)->type) << ", " <<
        emit_flat_type(copt(type)->type) << "_Direct>";
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
  case FieldType::Interface:
    assert(false);
    break;
  default:
    assert(false);
    break;
  }
}

void CppBuilder::emit_parameter_type_for_servant_callback(AstFunctionArgument* arg, std::ostream& os) {
  auto const input = (arg->modifier == ArgumentModifier::In);
  emit_parameter_type_for_servant_callback_r(arg->type, os, input);
  if (!input &&
    (arg->type->id != FieldType::Vector
      && arg->type->id != FieldType::Object
      && arg->type->id != FieldType::String
      && arg->type->id != FieldType::Optional)) {
    os << '&';
  }
}

void CppBuilder::emit_direct_type(AstTypeDecl* type, std::ostream& os) {
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

    auto const is_primitive = (
      wt->id == FieldType::Fundamental || 
      wt->id == FieldType::Enum);

    os << (is_primitive ? '1' : '2');
    os << '<';
    emit_flat_type(wt, os); 
    if (!is_primitive) {
      os << ','; emit_direct_type(wt, os);
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
    emit_flat_type(wt, os); os << ','; emit_direct_type(wt, os); os << '>';
    break;
  }

  default:
    assert(false);
    break;
  }
}

void CppBuilder::emit_accessors(const std::string& flat_name, AstFieldDecl* f, std::ostream& os) {
  switch (f->type->id) {
  case FieldType::Fundamental: {
    auto type_name = fundamental_to_flat(cft(f->type)->token_id);
    os << "  const " << type_name << "& " << f->name << "() const noexcept { return base()."
      << f->name << ";}\n";
    os << "  " << type_name << "& " << f->name << "() noexcept { return base()."
      << f->name << ";}\n";
    break;
  }

  case FieldType::Struct:
    os << "  auto " << f->name << "() noexcept { return " << ns(cflat(f->type)->nm) << "flat::" << cflat(f->type)->name
      << "_Direct(buffer_, offset_ + offsetof(" << flat_name << ", " << f->name << ")); }\n";
    break;

  case FieldType::Vector: 
    os << "  void " << f->name << "(std::uint32_t elements_size) { new (&base()." << f->name << ") ::nprpc::flat::Vector<";
    emit_flat_type(static_cast<AstWrapType*>(f->type)->type, os); os << ">(buffer_, elements_size); }\n";

    os << "  auto " << f->name << "_d() noexcept { return "; 
    emit_direct_type(f->type, os);
    os << "(buffer_, offset_ + offsetof(" << flat_name << ", " << f->name << ")); }\n";
  [[fallthrough]];
  case FieldType::Array: {
    auto wt = cwt(f->type)->real_type();
    if (wt->id == FieldType::Fundamental || wt->id == FieldType::Enum) {
      os << "  auto " << f->name << "() noexcept { return (::nprpc::flat::Span<";
      emit_flat_type(wt, os); os << ">)base()." << f->name << "; }\n";
      os << "  const auto " << f->name << "() const noexcept { return (::nprpc::flat::Span<const ";
      emit_flat_type(wt, os); os << ">)base()." << f->name << "; }\n";
    } else if (wt->id == FieldType::Struct) {
      os << "  auto " << f->name << "() noexcept { return ::nprpc::flat::Span_ref<";
      emit_flat_type(wt, os); os << ", "; emit_direct_type(wt, os);
      os << ">(buffer_, base()." << f->name << ".range(buffer_.data().data())); }\n";
    } 
    break;
  }

  case FieldType::String:
    os << "  void " << f->name << "(const char* str) { new (&base()." << f->name << ") ::nprpc::flat::String(buffer_, str); }\n";
    os << "  void " << f->name << "(const std::string& str) { new (&base()." << f->name << ") ::nprpc::flat::String(buffer_, str); }\n";
    os << "  auto " << f->name << "() noexcept { return (::nprpc::flat::Span<char>)base()." << f->name << "; }\n";
    os << "  auto " << f->name << "() const noexcept { return (::nprpc::flat::Span<const char>)base()." << f->name << "; }\n";
    os << 
      "  auto " << f->name << "_d() noexcept { "
      "    return ::nprpc::flat::String_Direct1(buffer_, offset_ + offsetof(" << flat_name << ", " << f->name << "));"
      "  }\n";
    break;

  case FieldType::Optional: {
    os <<
      "  auto " << f->name << "() noexcept { return "; emit_direct_type(f->type, os);
      os << "(buffer_, offset_ + offsetof(" << flat_name << ", " << f->name << "));"
        "  }\n";
    break;
  }

  case FieldType::Object:
    os << "  auto " << f->name << "() noexcept { return "
      "::nprpc::detail::flat::ObjectId_Direct(buffer_, offset_ + offsetof(" << flat_name << ", " << f->name << ")); }\n";
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
    os << "  const " << ns(e->nm) << e->name << "& " << f->name << "() const noexcept { return base()."
      << f->name << ";}\n";
    os << "  " << ns(e->nm) << e->name << "& " << f->name << "() noexcept { return base()."
      << f->name << ";}\n";
    break;
  }
  default:
    assert(false);
    break;
  }
}

void CppBuilder::assign_from_cpp_type(
  AstTypeDecl* type,
  std::string op1,
  std::string op2,
  std::ostream& os,
  bool from_iterator,
  bool top_type,
  bool/* direct_type */)
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
      os << bd << "memcpy(" << op1 << (top_type ? "" : "()") << ".__data(), &" << op2 << ", " << s->size << ");\n";
    } else {
      for (auto field : s->fields) {
        assign_from_cpp_type(field->type, 
          op1 + (top_type ? "." : (from_iterator ? "." : "().")) + field->name, 
          op2 + (from_iterator ? "->" : ".") + field->name, 
          os);
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
    if (is_flat(wt)) {
      auto [size, align] = get_type_size_align(wt);
      os << bd << "memcpy(" << op1 << "().data(), " << op2 << ".data(), " << op2 << ".size() * " << size << ");\n";
    } else if (wt->id == FieldType::String) {
      os << bd << "{\n" <<
        (bd += 1) << "auto vdir = " << op1 << "_d();\n" <<
        bd << "auto it = " << op2 << ".begin();\n" <<
        bd << "auto span = vdir();\n" <<
        bd << "for (auto e : span) {\n";
        os << (bd += 1) << "e.length(it->size());\n";
        os << "e = *it;\n";

      os << 
        bd << "++it;\n" << 
        (bd -= 1) << "}\n" <<
        (bd -= 1) << "}\n";
    } else {
      os <<
        bd << "{\n" <<
        (bd += 1) << "auto span = " << op1 << "();\n" <<
        bd << "auto it = " << op2 << ".begin();\n" <<
        bd << "for (auto e : span) {\n";
      
        os << (bd += 1) << "auto __ptr = ::nprpc::make_wrapper1(*it);\n"
        ;

      assign_from_cpp_type(wt, "  e", "__ptr", os, true);

      os << 
        bd << "++it;\n" << 
        (bd -= 1) << "}\n" <<
        (bd -= 1) << "}\n";
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
    assert(top_type == false);
    os << bd << op1 << '(' << op2 << ");\n";
    break;

  case FieldType::Optional: {
    auto bd0 = bd++;

    os << bd0 << "if (" << op2 << ") {\n";
    os << bd << op1 << accessor << "alloc();\n";

    auto wt = copt(type)->real_type();
    if (wt->id == FieldType::Struct || wt->id == FieldType::Vector || wt->id == FieldType::Array) {
      os << bd << "auto value = " << op1 << accessor << "value();\n";
      assign_from_cpp_type(wt, "value", op2 + ".value()", os, false, true, true);
    } else {
      assign_from_cpp_type(wt, op1 + std::string(accessor) + "value", op2 + ".value()", os, false, false);
    }

    os << bd0 << "} else { \n";
    os << bd << op1 << accessor << "set_nullopt();\n";
    os << bd0 << "}\n"
      ;

    bd = bd0;

    break;
  }

  case FieldType::Object:
    assert(top_type == false);
    os << bd << "{\n";
    os << bd << "  " << "auto tmp = " << op1 << "();\n";
    os << bd << "  " << "::nprpc::detail::helpers::assign_from_cpp_ObjectId("
      "tmp, " << op2 << ".get_data()"
    ");\n";
    os << bd << "}\n";
    break;

  default:
    assert(false);
    break;
  }
}

void CppBuilder::assign_from_flat_type(AstTypeDecl* type, std::string op1, std::string op2, std::ostream& os, bool from_iterator, bool top_object) {
  switch (type->id) {
  case FieldType::Fundamental: {
    //assert(top_object == false);
    auto ft = cft(type);
    os << bd << op1 << " = " << (ft->token_id == TokenId::Boolean ? "(bool)" : "") << op2 << "();\n";
    break;
  }

  case FieldType::Struct: {
    auto s = cflat(type);
    if (s->flat) {
      os << bd <<"memcpy(&" << op1 << ", " << op2 << (top_object ? "." : "().") << "__data(), " << s->size << ");\n";
    } else {
      for (auto field : s->fields) {
        assign_from_flat_type(field->type, 
          op1 + '.' + field->name, 
          op2 + (top_object ? "." : (from_iterator ? "." : "().")) + field->name,
          os);
      }
    }
    break;
  }

  case FieldType::Array:
  case FieldType::Vector: {
    auto wt = static_cast<AstWrapType*>(type)->type;
    auto const size = std::get<0>(get_type_size_align(wt));

    bool is_string = (wt->id == FieldType::String);

    os << bd << "{\n"
      << bd + 1 <<"auto span = " << op2 << (is_string ? "_d()();\n" : "();\n");
    
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
      assign_from_flat_type(wt, "(*"+ its +")", "e", os, true, false);
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
    // auto wtr = copt(type)->real_type();

    auto const bd0 = bd;

    os << bd0 << "{\n";

    os << bd + 1 << "auto opt = " << op2 << "();\n";
    os << bd + 1 << "if (opt.has_value()) {\n";
    os << bd + 2 << op1 << " = std::decay<decltype(" << op1 << ")>::type::value_type{};\n";
    os << bd + 2 << "auto& value_to = " << op1 << ".value();\n";

    bd = bd + 2;
    if (wt->id == FieldType::Struct || wt->id == FieldType::Vector || wt->id == FieldType::Array) {
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
      os << bd << op1 << " = ::nprpc::impl::create_object_from_flat(" << op2 << "(), this->get_endpoint());\n";
    } else {
      os << bd << op1 << ".assign_from_direct(" << op2 << "());\n";
    }
    break;

  default:
    assert(false);
    break;
  }
}

void CppBuilder::emit_struct2(AstStructDecl* s, std::ostream& os, Target target) {
  auto make_struct = [s, this, &os]<typename T>(T && fn) {
    os << "struct " << s->name << " {\n";
    for (auto const f : s->fields) {
      os << "  ";
      fn(f->type, os);
      os << ' ' << f->name << ";\n";
    }
    os << "};\n\n";
  };

  if (target == Target::Regular) {
    make_struct(std::bind(static_cast<void(CppBuilder::*)(AstTypeDecl*, std::ostream&)>(&CppBuilder::emit_type), this, _1, _2));
  } else if (target == Target::Exception) {
    os << "class " << s->name << " : public ::nprpc::Exception {\n"
      "public:\n"
      ;

    if (s->fields.size() > 1) {
      std::for_each(next(begin(s->fields)), end(s->fields),
        [this, &os](auto f) {
          os << "  "; emit_type(f->type, os); os << " " << f->name << ";\n";
        });
      os << '\n';
    }

    os <<
      "  " << s->name << "() : ::nprpc::Exception(\"" << s->name << "\") {} \n"
      ;

    if (s->fields.size() > 1) {
      os << "  " << s->name << '('; std::for_each(next(begin(s->fields)), end(s->fields), 
        [this, &os, ix = 1ul, size = s->fields.size()](auto f) mutable {
        emit_type(f->type, os); os << " _" << f->name;
        if (++ix < size) os << ", ";
      });
    

    os << ")\n"
      "    : ::nprpc::Exception(\"" << s->name << "\")\n"
      ;

      std::for_each(next(begin(s->fields)),
        end(s->fields), [this, &os](auto f) mutable {
          os << "    , " << f->name << "(_" << f->name << ")\n";
        });
    
      os << 
      "  {\n"
      "  }\n"
      ;
    }
    
    os << "};\n\n";
  }
  
  if (target != Target::FunctionArgument)
    os << "namespace flat {\n";
  
  make_struct(std::bind(static_cast<void(CppBuilder::*)(AstTypeDecl*, std::ostream&)>(&CppBuilder::emit_flat_type), this, _1, _2));

  auto const accessor_name = s->name + "_Direct";

  os << "class " << accessor_name << " {\n"
    "  ::nprpc::flat_buffer& buffer_;\n"
    "  const std::uint32_t offset_;\n\n"
    "  auto& base() noexcept { return *reinterpret_cast<" << s->name <<
    "*>(reinterpret_cast<std::byte*>(buffer_.data().data()) + offset_); }\n"
    "  auto const& base() const noexcept { return *reinterpret_cast<const " << s->name <<
    "*>(reinterpret_cast<const std::byte*>(buffer_.data().data()) + offset_); }\n"
    "public:\n"
    "  uint32_t offset() const noexcept { return offset_; }\n"
    "  void* __data() noexcept { return (void*)&base(); }\n"
    "  " << accessor_name << "(::nprpc::flat_buffer& buffer, std::uint32_t offset)\n"
    "    : buffer_(buffer)\n"
    "    , offset_(offset)\n"
    "  {\n"
    "  }\n"
    ;

  for (auto& f : s->fields) {
    emit_accessors(s->name, f, os);
  }

  os << "};\n";

  if (target != Target::FunctionArgument)
    os << "} // namespace flat\n";

  os << '\n';
}

void CppBuilder::emit_constant(const std::string& name, AstNumber* number) {
  oh << "constexpr auto " << name << " = ";
  std::visit(overloaded{
  [&](int64_t x) { 
    oh << x;
  },
  [&](float x) { oh << x << 'f'; },
  [&](double x) { oh << x; },
  [&](bool x) { oh << std::ios::boolalpha << x << std::ios::dec; },
  }, number->value);
  oh << ";\n";
}

void CppBuilder::emit_struct(AstStructDecl* s) {
  emit_struct2(s, oh, Target::Regular);
}

void CppBuilder::emit_exception(AstStructDecl* s) {
  assert(s->is_exception());
  emit_struct2(s, oh, Target::Exception);
}

void CppBuilder::finalize() {
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
    std::transform(r.begin(), r.end(), r.begin(), [](char c) {
      return c == '.' ? '_' : ::toupper(c);
      });
    return r;
  };

  auto h1 = make_guard("__NPRPC_" + ctx_->current_file() + "_HPP__");

  ofs_hpp <<
    "#ifndef " << h1 << "\n"
    "#define " << h1 << "\n\n"
    "#include <nprpc/flat.hpp>\n";

  if (!ctx_->is_nprpc_base()) {
    ofs_hpp << "#include <nprpc/nprpc.hpp>\n\n";
  } else {
    ofs_hpp << "#include <nprpc/exception.hpp>\n";
    ofs_hpp << "#include <string_view>\n\n";
  }

  // Generate module-specific export macro
  auto module_upper = make_guard(ctx_->current_file());
  export_macro_name_ = module_upper + "_API";
  
  ofs_hpp <<
    "// Module export macro\n"
    "#ifdef NPRPC_EXPORTS\n"
    "#  define " << export_macro_name_ << " NPRPC_EXPORT_ATTR\n"
    "#else\n"
    "#  define " << export_macro_name_ << " NPRPC_IMPORT_ATTR\n"
    "#endif\n\n";

  const bool is_module = !ctx_->module().empty();

  ofs_cpp <<
    "#include \"";
  
  ofs_cpp << ctx_->current_file() << ".hpp\"\n";

  ofs_cpp <<
    "#include <nprpc/impl/nprpc_impl.hpp>\n\n"
    "void " << ctx_->current_file() << "_throw_exception(::nprpc::flat_buffer& buf);\n\n"
    ;

  if (is_module) {
    ofs_hpp << "namespace " << ctx_->nm_root()->to_cpp17_namespace() << " {\n\n";
    ofs_cpp << "namespace " << ctx_->nm_root()->to_cpp17_namespace() << " {\n\n";
  }

 

  emit_helpers();
  emit_struct_helpers();

  std::stringstream ss;

  ss << "namespace {\n";
  emit_arguments_structs(std::bind(&CppBuilder::emit_struct2, this, _1, std::ref(ss), Target::FunctionArgument));
  ocpp << ss.str() << "\n";
  emit_safety_checks();
  ocpp << "} // \n\n" << oc.str();

  if (is_module) {
    oh << "} // module " << ctx_->nm_root()->to_cpp17_namespace() << "\n";
    ocpp << "} // module " << ctx_->nm_root()->to_cpp17_namespace() << "\n";
  }

  oh << "\n#endif";

  // Emit exception throwing function
  auto& exs = ctx_->exceptions;
  if (!exs.empty()) {
    ocpp <<
      "\n"
      "void " << ctx_->current_file() << "_throw_exception(::nprpc::flat_buffer& buf) { \n"
      "  switch(*(uint32_t*)( (char*)buf.data().data() + sizeof(::nprpc::impl::Header)) ) {\n"
      ;

    always_full_namespace(true);

    for (auto ex : exs) {
      ocpp <<
        "  case " << ex->exception_id << ":\n"
        "  {\n"
        "    " << ns(ex->nm) << "flat::" <<ex->name << "_Direct ex_flat(buf, sizeof(::nprpc::impl::Header));\n"
        "    " << ns(ex->nm) << ex->name << " ex;\n"
        ;

      for (size_t i = 1; i < ex->fields.size(); ++i) {
        auto f = ex->fields[i];
        assign_from_flat_type(f->type, "ex." + f->name, "ex_flat." + f->name, ocpp, false, true);
      }

      ocpp <<
        "    throw ex;\n"
        "  }\n"
        ;
    }

    always_full_namespace(false);

    ocpp <<
      "  default:\n"
      "    throw std::runtime_error(\"unknown rpc exception\");\n"
      "  }\n"
      "}\n"
      ;
  }

  ofs_hpp << oh.str();
  ofs_cpp << ocpp.str();
}

void CppBuilder::emit_safety_checks_r(AstTypeDecl* type, std::string op, std::ostream& os, bool /* from_iterator */, bool top_type) {
  switch (type->id) {
  case FieldType::Struct: {
    auto s = cflat(type);
    
    if (top_type) {
      os <<
        "  if (static_cast<std::uint32_t>(buf.size()) < " << op << ".offset() + " << s->size << ") goto check_failed;\n"
        ;
    }

    if (s->flat) break;

    for (auto field : s->fields) {
      auto ftr = field->type;
      if (ftr->id == FieldType::Alias) ftr = calias(ftr)->get_real_type();
      if (
        ftr->id == FieldType::Vector ||
        ftr->id == FieldType::Optional ||
        ftr->id == FieldType::Struct ||
        ftr->id == FieldType::String
        ) {
        os << bd << "{\n";
        auto str = op + "." + field->name + (ftr->id == FieldType::Vector || ftr->id == FieldType::String ? "_d()" : "()");
        bd += 1;
        emit_safety_checks_r(field->type, str, os, false, ftr->id != FieldType::Struct);
        bd -= 1;
        os << bd << "}\n";
      }
      else if (
        ftr->id == FieldType::Array
        ) {
        if ( is_flat(car(ftr)->type) ) break;
      }
    }

    break;
  }

  case FieldType::String:
    os << bd << "if(!" << op << "._check_size_align(static_cast<std::uint32_t>(buf.size()))) goto check_failed;\n";
    break;

  case FieldType::Vector: {
    auto wt = cwt(type)->type;
    
    os << bd << "if(!" << op << "._check_size_align(static_cast<std::uint32_t>(buf.size()))) goto check_failed;\n";
    
    if (is_flat(wt)) break;

    os << bd << "{\n"
      << bd + 1 << "auto span = " << op << "();\n"
      << bd + 1 << "for (auto e : span) {\n";
    emit_safety_checks_r(wt, "e", os, true, true);
    os << bd + 1 << "}\n"
      << bd << "}\n";

    break;
  }

  case FieldType::Optional: {
    auto wt = cwt(type)->type;
    os << bd << "if(!" << op << "._check_size_align(static_cast<std::uint32_t>(buf.size()))) goto check_failed;\n";
    
    if (is_flat(wt)) break;

    os << bd << "if ( " << op << ".has_value() ) {\n"
      << bd + 1 << "auto value = " << op << ".value();\n";
    emit_safety_checks_r(wt, "value", os, true, true);
    os << bd << "}\n";

    break;
  }

  case FieldType::Array:
    break;

  default:
    break;
    //assert(false);
  }
}

void CppBuilder::emit_safety_checks() {
  std::set<struct_id_t> set;

  for (auto ifs : ctx_->interfaces) {
    if (ifs->trusted) continue;

    for (auto fn : ifs->fns) {
      auto s = fn->in_s;
      
      if (!s) continue;
      
      auto const name = s->get_function_struct_id();
      if (set.find(name) != set.end()) continue;
      set.emplace(name);

      ocpp << "bool check_" << name << "(::nprpc::flat_buffer& buf, " << fn->in_s->name << "_Direct& ia" << ") {\n";

      emit_safety_checks_r(s, "ia", ocpp, false, true);
    
      ocpp <<
          "  return true;\n"
          "check_failed:\n"
          "  ::nprpc::impl::make_simple_answer(buf, ::nprpc::impl::MessageId::Error_BadInput);\n"
          "  return false;\n"
          "}\n"
          ;

    }
  }
}

void CppBuilder::emit_namespace_begin() {
  oh << "namespace " << ctx_->nm_cur()->name() << " { \n";
  oc << "namespace " << ctx_->nm_cur()->name() << " { \n";
}

void CppBuilder::emit_namespace_end() {
  oh << "} // namespace " << ctx_->nm_cur()->name() << "\n\n";
  oc << "} // namespace " << ctx_->nm_cur()->name() << "\n\n";
}

void CppBuilder::emit_helpers() {
  always_full_namespace(true);
  oh << "namespace " /*<< ctx_->current_file()*/ << "helper {\n";
  for (auto& [unused, s] : ctx_->affa_list) {
    for (auto f : s->fields) {
      assert(f->function_argument);
      if (is_fundamental(f->type) || f->type->id == FieldType::String || f->type->id == FieldType::Object) continue;

      if (f->input_function_argument) {
        if (f->type->id == FieldType::Struct) {
          oh <<
            "inline void assign_from_flat_" << f->function_name << "_" << f->function_argument_name << "("; emit_parameter_type_for_servant_callback_r(f->type, oh, false); oh << "& src, "; emit_parameter_type_for_proxy_call_r(f->type, oh, false); oh << "& dest) {\n"
            ;
          assign_from_flat_type(f->type, "dest", "src", oh, false, true);
          oh << "}\n";
        }
      
      } else {
        if (f->function_argument_name == "ret_val") continue;
        if (f->type->id == FieldType::Struct) {
          oh <<
            "inline void assign_from_cpp_" << f->function_name << "_" << f->function_argument_name << "("; emit_parameter_type_for_servant_callback_r(f->type, oh, false); oh << "& dest, const "; emit_parameter_type_for_proxy_call_r(f->type, oh, false); oh << "& src) {\n"
            ;
          assign_from_cpp_type(f->type, "dest", "src", oh, false, true);
          oh << "}\n";
        } else { // Itearable
          oh <<
            "template<::nprpc::IterableCollection T>\n"
            "void assign_from_cpp_" << f->function_name << "_" << f->function_argument_name << "("; emit_parameter_type_for_servant_callback_r(f->type, oh, false); oh << "& dest, const T & src) {\n"
            ;
          assign_from_cpp_type(f->type, "dest", "src", oh, false, true);
          oh << "}\n";
        }
      }
    }
  }

  oh << "} // namespace " << ctx_->current_file() << "::helper\n";
  always_full_namespace(false);
}

void CppBuilder::emit_struct_helpers() {

  for (auto s : ctx_->get_structs_with_helpers()) {
    oh << "namespace " << ns(s->nm) << "helpers {\n";
    always_full_namespace(true);
    auto saved_namespace = ctx_->set_namespace(s->nm);
    oh << "inline void assign_from_flat_" << s->name << "(";
    emit_parameter_type_for_servant_callback_r(s, oh, false); 
    oh << "& src, "; 
    emit_parameter_type_for_proxy_call_r(s, oh, false); oh << "& dest) {\n";
    assign_from_flat_type(s, "dest", "src", oh, false, true);
    oh << "}\n";
      
    oh << "inline void assign_from_cpp_" << s->name << "(";
    emit_parameter_type_for_servant_callback_r(s, oh, false);
    oh << "& dest, const ";
    emit_parameter_type_for_proxy_call_r(s, oh, false); 
    oh << "& src) {\n";
    assign_from_cpp_type(s, "dest", "src", oh, false, true);
    oh << "}\n";
    ctx_->set_namespace(saved_namespace);
    oh << "} // namespace " << s->nm->name() << "::flat\n";
    always_full_namespace(false);
  }

}

void CppBuilder::emit_function_arguments(
  AstFunctionDecl* fn,
  std::ostream& os,
  std::function<void(AstFunctionArgument*, std::ostream& os)> emitter
) {
  os << "(";
  size_t ix = 0;
  for (auto arg : fn->args) {
    emitter(arg, os);
    os << " " << arg->name;
    if (++ix != fn->args.size()) os << ", ";
  }
  os << ')';
};

void CppBuilder::proxy_call(AstFunctionDecl* fn) {
  oc <<
    "  ::nprpc::impl::g_rpc->call(this->get_endpoint(), buf, this->get_timeout());\n"
    "  auto std_reply = ::nprpc::impl::handle_standart_reply(buf);\n"
    ;

  if (fn->ex) 
    oc << "  if (std_reply == 1) " << ctx_->current_file() << "_throw_exception(buf);\n";
  
  if (!fn->out_s) {
    oc <<
      "  if (std_reply != 0) {\n"
      "    throw ::nprpc::Exception(\"Unknown Error\");\n"
      "  }\n"
      ;
  } else {
    oc <<
      "  if (std_reply != -1) {\n"
      "    throw ::nprpc::Exception(\"Unknown Error\");\n"
      "  }\n"
      ;

    oc << "  " << fn->out_s->name << "_Direct out(buf, sizeof(::nprpc::impl::Header));\n";

    int ix = fn->is_void() ? 0 : 1;
    bd = 2;
    for (auto out : fn->args) {
      if (out->modifier == ArgumentModifier::In) continue;
      assign_from_flat_type(out->type, out->name, "out._" + std::to_string(++ix), oc, false,
        out->type->id == FieldType::Object && out->direct == false);
    }

    if (!fn->is_void()) {
      oc << bd; emit_type(fn->ret_value, oc); oc << " __ret_value;\n";
      assign_from_flat_type(fn->ret_value, "__ret_value", "out._1", oc, false,
        fn->ret_value->id == FieldType::Object);
      oc << "  return __ret_value;\n";
    }
  }
}

void CppBuilder::proxy_udp_call(AstFunctionDecl* fn) {
  // Fire-and-forget UDP call - send and don't wait for reply
  oc <<
    "  ::nprpc::impl::g_rpc->send_udp(this->get_endpoint(), std::move(buf));\n"
    ;
}

void CppBuilder::proxy_unreliable_call(AstFunctionDecl* fn) {
  // Fire-and-forget call for non-UDP transports (e.g., QUIC DATAGRAM)
  oc <<
    "  ::nprpc::impl::g_rpc->send_unreliable(this->get_endpoint(), std::move(buf));\n"
    ;
}
void CppBuilder::proxy_udp_reliable_call(AstFunctionDecl* fn) {
  // Reliable UDP call - wait for reply with retransmit
  oc <<
    "  ::nprpc::impl::g_rpc->call_udp_reliable(this->get_endpoint(), buf);\n"
    "  auto std_reply = ::nprpc::impl::handle_standart_reply(buf);\n"
    ;

  if (fn->ex) 
    oc << "  if (std_reply == 1) " << ctx_->current_file() << "_throw_exception(buf);\n";
  
  if (!fn->out_s) {
    oc <<
      "  if (std_reply != 0) {\n"
      "    throw ::nprpc::Exception(\"Unknown Error\");\n"
      "  }\n"
      ;
  } else {
    oc <<
      "  if (std_reply != -1) {\n"
      "    throw ::nprpc::Exception(\"Unknown Error\");\n"
      "  }\n"
      ;

    oc << "  " << fn->out_s->name << "_Direct out(buf, sizeof(::nprpc::impl::Header));\n";

    int ix = fn->is_void() ? 0 : 1;
    bd = 2;
    for (auto out : fn->args) {
      if (out->modifier == ArgumentModifier::In) continue;
      assign_from_flat_type(out->type, out->name, "out._" + std::to_string(++ix), oc, false,
        out->type->id == FieldType::Object && out->direct == false);
    }

    if (!fn->is_void()) {
      oc << bd; emit_type(fn->ret_value, oc); oc << " __ret_value;\n";
      assign_from_flat_type(fn->ret_value, "__ret_value", "out._1", oc, false,
        fn->ret_value->id == FieldType::Object);
      oc << "  return __ret_value;\n";
    }
  }
}

void CppBuilder::proxy_udp_reliable_async_call(AstFunctionDecl* fn) {
  // Async reliable UDP call - buffer is moved, handler called on completion
  oc <<
    "  ::nprpc::impl::g_rpc->call_udp_reliable_async(this->get_endpoint(), std::move(buf), "
    "!handler ? std::nullopt : std::make_optional([handler = move(handler)] (\n"
    "    const boost::system::error_code& ec, ::nprpc::flat_buffer& buf) {\n"
    "      if (ec) {\n"
    "        // Error occurred - just call handler with no args or handle error\n"
    "        // For now, we don't propagate errors through async handler\n"
    "      }\n"
    "      (*handler)();\n"
    "  }));\n"
    ;
}

void CppBuilder::proxy_async_call(AstFunctionDecl* fn) {
  oc <<
    "  ::nprpc::impl::g_rpc->call_async(this->get_endpoint(), std::move(buf), !handler ? std::nullopt : std::make_optional([handler = move(handler)] (\n"
    "    const boost::system::error_code& ec, ::nprpc::flat_buffer& buf) {\n"
    ;

    if (fn->out_args.empty() == false) {
      // not implemented for now...
      assert(false);

      bd = 4;
  
      oc << 
        "    " << fn->out_s->name << "_Direct out(buf, sizeof(::nprpc::impl::Header));\n"
        ;
  
      for (auto arg : fn->out_args) {
        oc << bd; emit_parameter_type_for_proxy_call_r(arg->type, oc, false); oc << " " << arg->name << ";\n";
      }
  
      size_t ix = 0;
      for (auto out : fn->out_args) {
        assign_from_flat_type(out->type, out->name, "out._" + std::to_string(++ix), oc, false,
          out->type->id == FieldType::Object && out->direct == false);
      }
    } else {
      oc << bd << "(*handler)();\n";
    }

    oc << "}), get_timeout());\n";
}

std::string_view CppBuilder::proxy_arguments(AstFunctionDecl* fn) {
  if (auto it = proxy_arguments_.find(fn); it != proxy_arguments_.end()) 
    return it->second;

  std::stringstream ss;
  if (!fn->is_async || !fn->is_reliable) {
    emit_function_arguments(fn, ss,
      std::bind(&CppBuilder::emit_parameter_type_for_proxy_call, this, _1, _2)
    );
  } else {
    size_t out_args_size = fn->out_args.size();
    ss << "(std::optional<std::function<void(";
    for (auto arg : fn->out_args) {
      emit_parameter_type_for_proxy_call(arg, ss);
      if (--out_args_size) ss << ", ";
    }
    ss << ")>> handler";
    
    size_t in_args_size = fn->in_args.size();
    if (in_args_size) ss << ", ";
    for (auto arg : fn->in_args) {
      emit_parameter_type_for_proxy_call(arg, ss);
      ss << ' ' << arg->name;
      if (--in_args_size) ss << ", ";
    }
    ss << ')';
  }

  return proxy_arguments_.emplace(fn, ss.str()).first->second;
}

void CppBuilder::emit_interface(AstInterfaceDecl* ifs) {
  // Servant definition
  oh <<
    "class " << export_macro_name_ << " I" << ifs->name << "_Servant\n";
  if (ifs->plist.size()) {
    oh << "  : public I" << ifs->plist[0]->name << "_Servant\n";
    for (size_t i = 1; i < ifs->plist.size(); ++i) {
      oh << "  , public I" << ifs->plist[i]->name << "_Servant\n";
    }
    oh << "{\n";
  } else {
    oh << "  : public virtual ::nprpc::ObjectServant\n{\n";
  }

  oh <<
    "public:\n"
    "  static std::string_view _get_class() noexcept { return \"" << ctx_->current_file() << '/' << ctx_->nm_cur()->to_ts_namespace() << '.' << ifs->name << "\"; }\n"
    "  std::string_view get_class() const noexcept override { return I" << ifs->name << "_Servant::_get_class(); }\n"
    "  void dispatch(::nprpc::Buffers& bufs, [[maybe_unused]] ::nprpc::SessionContext& ctx, [[maybe_unused]] bool from_parent) override;\n"
    ;

  for (auto fn : ifs->fns) {
    oh << "  virtual "; emit_type(fn->ret_value, oh);
    oh << ' ' << fn->name << " ";
    emit_function_arguments(fn, oh,
      std::bind(&CppBuilder::emit_parameter_type_for_servant_callback, this, _1, _2)
    );
    oh << " = 0;\n";
  }

  oh << "};\n\n";

  // Proxy definition
  oh <<
    "class " << export_macro_name_ << " " << ifs->name << "\n";

  if (ifs->plist.size()) {
    oh << "  : public " << ifs->plist[0]->name << "\n";
    for (size_t i = 1; i < ifs->plist.size(); ++i) {
      oh << "  , public " << ifs->plist[i]->name << "\n";
    }
    oh << "{\n";
  } else {
    oh << "  : public virtual ::nprpc::Object\n{\n";
  }

  oh <<
    "  const uint8_t interface_idx_;\n"
    "public:\n"
    "  using servant_t = I" << ifs->name << "_Servant;\n\n"
    ;

  if (ifs->plist.empty()) {
    oh << "  " << ifs->name << "(uint8_t interface_idx) : interface_idx_(interface_idx) {}\n";
  } else {
    oh <<"  " << ifs->name << "(uint8_t interface_idx)\n";

    auto count_all = [](AstInterfaceDecl* /* ifs_inherited */, int& n) { ++n; };

    int n = 1;
    for (auto parent : ifs->plist) {
      oh << (n == 1 ? "    : " : "    , ") << parent->name << "(interface_idx + " << n << ")\n";
      dfs_interface(std::bind(count_all, _1, std::ref(n)), parent);
    }

    oh << "    , interface_idx_(interface_idx)\n"
      "  {\n"
      "  }\n"
      ;
  }

  // functions definitions
  for (auto& fn : ifs->fns) {
    make_arguments_structs(fn);
    oh << "  "; emit_type(fn->ret_value, oh);
    oh << ' ' << fn->name << " ";
    oh << proxy_arguments(fn) << ";\n";
  }

  oh << "};\n\n";


  // .CPP file marshall/unmarshall stuff below
  // auto const nm = ctx_->nm_cur()->to_cpp17_namespace();

  for (auto fn : ifs->fns) {
    emit_type(fn->ret_value, oc);
    oc << ' ' << ns(ctx_->nm_cur()) << ifs->name << "::" << fn->name;
    oc << proxy_arguments(fn) << " {\n";
    oc << "  ::nprpc::flat_buffer buf;\n";

    const auto fixed_size = get_arguments_offset() + (fn->in_s ? fn->in_s->size : 0);
    const auto capacity = fixed_size + (fn->in_s ? (fn->in_s->flat ? 0 : 128) : 0);

    // Try zero-copy buffer for shared memory transport
    oc <<
      "  if (!::nprpc::impl::g_rpc->prepare_zero_copy_buffer(this->get_endpoint(), buf, " << capacity << ")) {\n"
      "    buf.prepare(" << capacity << ");\n"
      "  }\n"
      "  {\n"
      "    buf.commit(" << fixed_size << ");\n"
      "    static_cast<::nprpc::impl::Header*>(buf.data().data())->msg_id = ::nprpc::impl::MessageId::FunctionCall;\n"
      "    static_cast<::nprpc::impl::Header*>(buf.data().data())->msg_type = ::nprpc::impl::MessageType::Request;\n"
      "  }\n"
      "  ::nprpc::impl::flat::CallHeader_Direct __ch(buf, sizeof(::nprpc::impl::Header));\n"
      "  __ch.object_id() = this->object_id();\n"
      "  __ch.poa_idx() = this->poa_idx();\n"
      "  __ch.interface_idx() = interface_idx_;\n"
      "  __ch.function_idx() = " << fn->idx << ";\n"
      ;

    if (fn->in_s) {
      oc <<
        "  " << fn->in_s->name << "_Direct _(buf," << get_arguments_offset() << ");\n"
        ;
    }

    int ix = 0;
    for (auto in : fn->args) {
      if (in->modifier == ArgumentModifier::Out) continue;
      bd = 1;
      assign_from_cpp_type(in->type, "_._" + std::to_string(++ix), in->name, oc);
    }

    oc << "  static_cast<::nprpc::impl::Header*>(buf.data().data())->size = static_cast<uint32_t>(buf.size() - 4);\n";

    // Choose the call method based on interface/function attributes
    if (ifs->is_udp && !fn->is_reliable) {
      // UDP fire-and-forget - no reply expected
      proxy_udp_call(fn);
    } else if (ifs->is_udp && fn->is_reliable && fn->is_async) {
      // UDP reliable + async - buffer moved, handler called on completion
      proxy_udp_reliable_async_call(fn);
    } else if (ifs->is_udp && fn->is_reliable) {
      // UDP reliable blocking - caller blocks, no buffer copy needed
      proxy_udp_reliable_call(fn);
    } else if (!fn->is_reliable) {
      // Non-UDP unreliable (e.g., QUIC DATAGRAM) - fire-and-forget
      proxy_unreliable_call(fn);
    } else if (!fn->is_async) {
      proxy_call(fn);
    } else {
      proxy_async_call(fn);
    }

    oc << "}\n\n";
  }

  // Servant dispatch
  oc << "void " << ns(ctx_->nm_cur()) << 'I' << ifs->name << "_Servant::dispatch(::nprpc::Buffers& bufs, [[maybe_unused]] ::nprpc::SessionContext& ctx, [[maybe_unused]] bool from_parent) {\n"
    "  ::nprpc::impl::flat::CallHeader_Direct __ch(bufs(), sizeof(::nprpc::impl::Header));\n"
    ;
    
  if (ifs->plist.empty()) {
    // ok
  } else {
    oc <<
      "  if (from_parent == false) {\n"
      "    switch(__ch.interface_idx()) {\n"
      "      case 0:\n"
      "        break;\n"
      ;
  
    int ix = 1;
    auto select_interface = [&ix, this, ifs](AstInterfaceDecl* i) {
      if (i == ifs) return;
      oc <<
        "      case "<< ix << ":\n"
        "        I" << i->name << "_Servant::dispatch(bufs, ctx, true);\n"
        "        return;\n"
      ;
      ++ix;
    };
    
    dfs_interface(select_interface, ifs);
    
    oc <<
      "      default:\n"
      //"        assert(false);\n"
      "        throw \"unknown interface\";\n"
      "    }\n"
      "  }\n"
      ;
  }

  oc <<
      "  switch(__ch.function_idx()) {\n"
      ;


  for (auto fn : ifs->fns) {
    oc << "    case " << fn->idx << ": {\n";

    if (fn->in_s) {
      oc <<
        "      " << fn->in_s->name << "_Direct ia(bufs(), " << get_arguments_offset() << ");\n"
        ;
      if (ifs->trusted == false) {
        // const auto fixed_size = get_arguments_offset() + fn->in_s->size;
        oc <<
          "      if ( !check_" << fn->in_s->get_function_struct_id() << "(bufs(), ia) ) break;\n"
          ;
      }
    }

    if (fn->out_s && !fn->out_s->flat) {
      const auto offset = size_of_header;
      const auto initial_size = offset + fn->out_s->size;
      oc <<
        "      auto& obuf = bufs.flip();\n"
        "      obuf.consume(obuf.size());\n"
        "      obuf.prepare(" << initial_size + 128 << ");\n"
        "      obuf.commit(" << initial_size << ");\n"
        "      " << fn->out_s->name << "_Direct oa(obuf," << offset << ");\n"
        ;
    }
    auto bd0 = bd;
    bd = 3;
    if (!fn->is_void()) {
      oc << bd; emit_type(fn->ret_value, oc); oc << " __ret_val;\n";
    }

    // Create stack variables for output parameters BEFORE the try block
    // For flat output structs: ALL out parameters need stack variables
    // For non-flat output structs: only Vector/String/Struct need temporary variables (for _d() access)
    size_t out_ix = fn->is_void() ? 0 : 1, out_temp_ix = 0;

    auto passed_as_direct = [](AstFunctionArgument* arg) {
      auto real_type = arg->type;
        if (real_type->id == FieldType::Alias)
          real_type = calias(real_type)->get_real_type();

      return arg->modifier == ArgumentModifier::Out &&
        (real_type->id == FieldType::Vector || real_type->id == FieldType::String || real_type->id == FieldType::Struct);
    };

    if (fn->out_s && fn->out_s->flat) {
      // For flat output structs, create stack variables for ALL output parameters
      for (auto arg : fn->args) {
        if (arg->modifier != ArgumentModifier::Out) continue;
        ++out_ix;
        oc << bd; emit_type(arg->type, oc); oc << " _out_" << out_ix << ";\n";
      }
    } else if (fn->out_s) {
      // For non-flat output structs, create temporary variables only for complex types
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

        oc << bd << "auto oa_" << ++out_temp_ix << " = oa._" << ++out_ix <<
          ((real_type->id == FieldType::Vector || real_type->id == FieldType::String) ? "_d();\n" : "();\n");
      }
    }

    if (fn->ex) oc << bd++ << "try {\n";

    oc << bd <<
      (fn->is_void() ? "" : "__ret_val = ") << fn->name << "("
      ;

    size_t in_ix = 0, idx = 0;
    out_ix = fn->is_void() ? 0 : 1;
    out_temp_ix = 0;
    for (auto arg : fn->args) {
      if (arg->modifier == ArgumentModifier::Out) {
        assert(fn->out_s);
        if (fn->out_s->flat) {
          // For flat structs, pass stack variable reference
          oc << "_out_" << ++out_ix;
        } else if (passed_as_direct(arg)) {
          // For non-flat structs with complex types, pass temporary variable
          oc << "oa_" << ++out_temp_ix;
        } else {
          // For non-flat structs with simple types, pass direct reference
          oc << "oa._" << ++out_ix << "()";
        }
      } else {
        if (arg->type->id == FieldType::Object) {
          oc << "::nprpc::impl::create_object_from_flat(ia._" << ++in_ix << "(), ctx.remote_endpoint)";
        } else {
          oc << "ia._" << ++in_ix << "()";
        }
      }
      if (++idx != fn->args.size()) oc << ", ";
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
            "  auto obj = impl::g_rpc->get_object(" << "oa._" << out_ix << "().poa_idx(), " << "oa._" << out_ix << "().object_id());\n"
            "  if (obj) if (auto real_obj = (*obj).get(); real_obj) ref_list.add_ref(real_obj);\n"
            "}\n"
            ;
        }
      }
    }
    */

    if (fn->ex) {
      const auto offset = size_of_header;
      const auto initial_size = offset + fn->ex->size;
      
      always_full_namespace(true);
      oc <<
        "      }\n"
        "      catch(" << ns(fn->ex->nm) << fn->ex->name << "& e) {\n"
        "        auto& obuf = bufs();\n"
        "        obuf.consume(obuf.size());\n"
        "        obuf.prepare(" << initial_size << ");\n"
        "        obuf.commit(" << initial_size << ");\n"
        "        "<< ns(fn->ex->nm) << "flat::" <<fn->ex->name << "_Direct oa(obuf," << offset << ");\n"
        "        oa.__ex_id() = " << fn->ex->exception_id << ";\n"
        ;
      always_full_namespace(false);

      for (size_t i = 1; i < fn->ex->fields.size(); ++i) {
        auto mb = fn->ex->fields[i];
        assign_from_cpp_type(mb->type, "oa." + mb->name, "e." + mb->name, oc);
      }

      oc << 
        "        static_cast<::nprpc::impl::Header*>(obuf.data().data())->size = static_cast<uint32_t>(obuf.size() - 4);\n"
        "        static_cast<::nprpc::impl::Header*>(obuf.data().data())->msg_id = ::nprpc::impl::MessageId::Exception;\n"
        "        static_cast<::nprpc::impl::Header*>(obuf.data().data())->msg_type = ::nprpc::impl::MessageType::Answer;\n"
        "        return;\n"
        "      }\n"
        ;
    }

    if (!fn->out_s) {
      oc << "      ::nprpc::impl::make_simple_answer(bufs(), nprpc::impl::MessageId::Success);\n";
    } else {
      if (fn->out_s->flat) { // it means that we are writing output data in the input buffer, 
        // so we must pass stack variables first and then assign result back to the buffer
        const auto offset = size_of_header;
        const auto initial_size = offset + fn->out_s->size;

        oc <<
          "      auto& obuf = bufs();\n"
          "      obuf.consume(obuf.size());\n"
          "      obuf.prepare(" << initial_size << ");\n"
          "      obuf.commit(" << initial_size << ");\n"
          "      " << fn->out_s->name << "_Direct oa(obuf," << offset << ");\n"
          ;
        
        int ix;
        if (!fn->is_void()) {
          assign_from_cpp_type(fn->ret_value, "oa._1", "__ret_val", oc);
          ix = 1;
        } else {
          ix = 0;
        }

        for (auto out : fn->args) {
          if (out->modifier == ArgumentModifier::In) continue;
          auto n = std::to_string(++ix);
          assign_from_cpp_type(out->type, "oa._" + n, "_out_" + n, oc);
        }
      } else if (!fn->is_void()) {
        assign_from_cpp_type(fn->ret_value, "oa._1", "__ret_val", oc);
      }

      oc <<
        "      static_cast<::nprpc::impl::Header*>(obuf.data().data())->size = static_cast<uint32_t>(obuf.size() - 4);\n"
        "      static_cast<::nprpc::impl::Header*>(obuf.data().data())->msg_id = ::nprpc::impl::MessageId::BlockResponse;\n"
        "      static_cast<::nprpc::impl::Header*>(obuf.data().data())->msg_type = ::nprpc::impl::MessageType::Answer;\n"
        ;
    }

    oc <<
      "      break;\n"
      "    }\n"
      ;

    bd = bd0;
  }

  oc <<
    "    default:\n"
    "      ::nprpc::impl::make_simple_answer(bufs(), ::nprpc::impl::MessageId::Error_UnknownFunctionIdx);\n"
    "  }\n"; // switch block
  ;

  oc << "}\n\n"; // dispatch
}

void CppBuilder::emit_using(AstAliasDecl* u) {
  oh << "using " << u->name << " = "; emit_type(u->type, oh); oh << ";\n";
}

void CppBuilder::emit_enum(AstEnumDecl* e) {
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
    if (i != e->items.size() - 1) oh << ",\n";
  }
  oh << "\n};\n";
}

CppBuilder::CppBuilder(
  Context* ctx,
  std::filesystem::path out_path
) : Builder(ctx)
  , out_path_(out_path)
{
  if (!ctx) return;
  // Initialize export macro name early so it's available during emit_interface
  auto make_guard = [](const std::string& file) {
    std::string r(file);
    std::transform(r.begin(), r.end(), r.begin(), [](char c) {
      return c == '.' ? '_' : ::toupper(c);
    });
    return r;
  };
  auto module_upper = make_guard(ctx_->current_file());
  export_macro_name_ = module_upper + "_API";
}

} // namespace npidl::builders
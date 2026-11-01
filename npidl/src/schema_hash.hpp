// A hash of everything about an interface that has to match between a client and
// a server: its methods, and the layout of every type they use, through aliases
// and imports. It goes in the class ID, so a mismatch fails at narrow() and not
// as a misread message.
//
// Comments and formatting don't change it, and neither does an alias's name, but
// field and method names do: swapping two fields of one type changes no sizes, yet
// is a break, and a hash can't tell a rename from a swap.
#pragma once

#include <cstdint>
#include <cstdio>
#include <sstream>
#include <string>
#include <unordered_set>

#include "ast.hpp"

namespace npidl {

class SchemaHasher
{
  std::ostringstream text_;
  std::unordered_set<const AstTypeDecl*> seen_;

  void type(AstTypeDecl* t)
  {
    if (!t) {
      text_ << "none;";
      return;
    }
    switch (t->id) {
    case FieldType::Fundamental:
      text_ << "f" << static_cast<int>(cft(t)->token_id) << ";";
      break;
    case FieldType::Enum: {
      auto* e = cenum(t);
      text_ << "e" << static_cast<int>(e->token_id) << "{";
      for (const auto& [name, value] : e->items) {
        text_ << name << "=";
        if (value.first.is_decimal()) text_ << value.first.decimal();
        text_ << ",";
      }
      text_ << "};";
      break;
    }
    case FieldType::Alias:
      type(calias(t)->get_real_type());
      break;
    case FieldType::Array:
      text_ << "a" << car(t)->length << "[";
      type(car(t)->type);
      text_ << "];";
      break;
    case FieldType::Vector:
      text_ << "v[";
      type(cvec(t)->type);
      text_ << "];";
      break;
    case FieldType::Optional:
      text_ << "?[";
      type(copt(t)->type);
      text_ << "];";
      break;
    case FieldType::String:
      text_ << "s;";
      break;
    case FieldType::Void:
      text_ << "void;";
      break;
    case FieldType::Object:
      text_ << "o;";
      break;
    case FieldType::Fd:
      text_ << "fd;";
      break;
    case FieldType::Interface:
      // A reference carries its own class ID, which is checked when it's narrowed.
      text_ << "i:" << cifs(t)->name << ";";
      break;
    case FieldType::Stream: {
      auto* s = static_cast<AstStreamDecl*>(t);
      text_ << "stream" << static_cast<int>(s->kind) << (s->direct ? "d" : "") << "[";
      type(s->type);
      type(s->input_type);
      text_ << "];";
      break;
    }
    case FieldType::Struct: {
      auto* s = cflat(t);
      text_ << "m:" << s->name;
      // Once each: types can refer to themselves, and to each other.
      if (!seen_.insert(s).second) {
        text_ << ";";
        break;
      }
      text_ << "#" << s->exception_id << "{";
      for (auto* field : s->fields) {
        text_ << field->name << ":";
        type(field->type);
      }
      text_ << "};";
      break;
    }
    case FieldType::Variant: {
      auto* v = cvar(t);
      text_ << "u:" << v->name;
      if (!seen_.insert(v).second) {
        text_ << ";";
        break;
      }
      text_ << "{";
      for (const auto& arm : v->arms) {
        text_ << arm.name << ":";
        type(arm.type);
      }
      text_ << "};";
      break;
    }
    }
  }

  void function(const AstFunctionDecl* fn)
  {
    text_ << "fn" << fn->idx << ":" << fn->name << (fn->is_async ? " async" : "")
          << (fn->is_reliable ? "" : " unreliable") << "(";
    for (auto* arg : fn->args) {
      text_ << (arg->modifier == ArgumentModifier::In ? "in " : "out ")
            << (arg->direct ? "direct " : "") << arg->name << ":";
      type(arg->type);
    }
    text_ << ")->";
    type(fn->ret_value);
    type(fn->stream_decl);
    text_ << "raises(";
    for (auto* exception : fn->exceptions) type(exception);
    text_ << ");";
  }

  void interface(const AstInterfaceDecl* ifs)
  {
    text_ << "if:" << ifs->name << (ifs->trusted ? " trusted" : "") << "{";
    for (const auto* parent : ifs->plist) interface(parent);
    for (const auto* fn : ifs->fns) function(fn);
    text_ << "};";
  }

public:
  // 16 hex digits. It guards against mistakes, not attackers, so FNV-1a will do.
  static std::string hash(const AstInterfaceDecl* ifs)
  {
    SchemaHasher hasher;
    hasher.interface(ifs);
    std::uint64_t value = 0xcbf29ce484222325ull;
    for (const unsigned char c : hasher.text_.str()) {
      value ^= c;
      value *= 0x100000001b3ull;
    }
    char hex[17];
    std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(value));
    return hex;
  }
};

} // namespace npidl

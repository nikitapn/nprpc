// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include "doc_json_builder.hpp"
#include "utils.hpp"

#include <cstdio>
#include <fstream>

namespace npidl::builders {

namespace {

struct Str {
  std::string_view s;
};

std::ostream& operator<<(std::ostream& os, Str str)
{
  os << '"';
  for (const char c : str.s) {
    switch (c) {
    case '"':  os << "\\\""; break;
    case '\\': os << "\\\\"; break;
    case '\n': os << "\\n"; break;
    case '\r': os << "\\r"; break;
    case '\t': os << "\\t"; break;
    default:
      if (static_cast<unsigned char>(c) < 0x20) {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "\\u%04x", c);
        os << buf;
      } else {
        os << c;
      }
    }
  }
  return os << '"';
}

std::string_view stream_kind_name(StreamKind kind)
{
  switch (kind) {
  case StreamKind::Server: return "server";
  case StreamKind::Client: return "client";
  case StreamKind::Bidi:   return "bidi";
  default:                 return "";
  }
}

// Synthesized fields (the exception id) are marshalling detail, not API.
bool is_internal_field(const AstFieldDecl* f)
{
  return f->name.starts_with("__");
}

void write_fields(std::ostream& os, const std::vector<AstFieldDecl*>& fields)
{
  os << "\"fields\":[";
  bool first = true;
  for (const auto* f : fields) {
    if (is_internal_field(f))
      continue;
    if (!first)
      os << ',';
    first = false;
    os << "{\"name\":" << Str{f->name}
       << ",\"type\":" << Str{idl_type_string(f->type)}
       << ",\"line\":" << f->name_range.start.line
       << ",\"doc\":" << Str{f->doc} << '}';
  }
  os << ']';
}

} // namespace

void DocJsonBuilder::begin_decl(std::string_view kind,
                                const AstNodeWithPosition& node)
{
  if (!first_decl_)
    decls_ << ",\n";
  first_decl_ = false;
  decls_ << "{\"kind\":" << Str{kind}
         << ",\"name\":" << Str{node.name}
         << ",\"namespace\":" << Str{ctx_->nm_cur()->full_idl_namespace()}
         << ",\"file\":" << Str{ctx_->get_file_path().filename().string()}
         << ",\"line\":" << node.name_range.start.line
         << ",\"doc\":" << Str{node.doc};
}

void DocJsonBuilder::emit_constant(const std::string& name, AstNumber* number)
{
  if (!first_decl_)
    decls_ << ",\n";
  first_decl_ = false;
  std::ostringstream value;
  value << *number;
  decls_ << "{\"kind\":\"const\",\"name\":" << Str{name}
         << ",\"namespace\":" << Str{ctx_->nm_cur()->full_idl_namespace()}
         << ",\"file\":" << Str{ctx_->get_file_path().filename().string()}
         << ",\"value\":" << Str{value.str()} << '}';
}

void DocJsonBuilder::emit_struct(AstStructDecl* s)
{
  begin_decl("message", *s);
  decls_ << ',';
  write_fields(decls_, s->fields);
  decls_ << '}';
}

void DocJsonBuilder::emit_exception(AstStructDecl* s)
{
  begin_decl("exception", *s);
  decls_ << ',';
  write_fields(decls_, s->fields);
  decls_ << '}';
}

void DocJsonBuilder::emit_interface(AstInterfaceDecl* ifs)
{
  begin_decl("interface", *ifs);
  decls_ << ",\"trusted\":" << (ifs->trusted ? "true" : "false")
         << ",\"bases\":[";
  for (size_t i = 0; i < ifs->plist.size(); ++i)
    decls_ << (i ? "," : "") << Str{ifs->plist[i]->name};
  decls_ << "],\"methods\":[";

  for (size_t i = 0; i < ifs->fns.size(); ++i) {
    const auto* fn = ifs->fns[i];
    if (i)
      decls_ << ',';
    decls_ << "{\"name\":" << Str{fn->name}
           << ",\"line\":" << fn->name_range.start.line
           << ",\"doc\":" << Str{fn->doc}
           << ",\"returns\":" << Str{idl_type_string(fn->ret_value)}
           << ",\"stream\":" << Str{stream_kind_name(fn->stream_kind)}
           << ",\"unreliable\":" << (fn->is_reliable ? "false" : "true")
           << ",\"params\":[";
    for (size_t j = 0; j < fn->args.size(); ++j) {
      const auto* arg = fn->args[j];
      if (j)
        decls_ << ',';
      decls_ << "{\"name\":" << Str{arg->name}
             << ",\"direction\":"
             << (arg->modifier == ArgumentModifier::Out ? "\"out\"" : "\"in\"")
             << ",\"direct\":" << (arg->direct ? "true" : "false")
             << ",\"type\":" << Str{idl_type_string(arg->type)}
             << ",\"doc\":" << Str{arg->doc} << '}';
    }
    decls_ << "],\"raises\":[";
    for (size_t j = 0; j < fn->exceptions.size(); ++j)
      decls_ << (j ? "," : "") << Str{fn->exceptions[j]->name};
    decls_ << "]}";
  }
  decls_ << "]}";
}

void DocJsonBuilder::emit_using(AstAliasDecl* u)
{
  begin_decl("alias", *u);
  decls_ << ",\"target\":" << Str{idl_type_string(u->type)} << '}';
}

void DocJsonBuilder::emit_enum(AstEnumDecl* e)
{
  begin_decl("enum", *e);
  const AstFundamentalType underlying(e->token_id);
  decls_ << ",\"underlying\":" << Str{idl_type_string(&underlying)}
         << ",\"items\":[";
  for (size_t i = 0; i < e->items.size(); ++i) {
    // The parser resolves implicit items to their running value, so this is
    // the numeric value whether or not the source spelled it out.
    const auto& [name, value] = e->items[i];
    decls_ << (i ? "," : "") << "{\"name\":" << Str{name}
           << ",\"value\":" << value.first.decimal()
           << ",\"doc\":" << Str{e->item_docs[i]} << '}';
  }
  decls_ << "]}";
}

void DocJsonBuilder::emit_variant(AstVariantDecl* v)
{
  begin_decl("variant", *v);
  decls_ << ",\"arms\":[";
  for (size_t i = 0; i < v->arms.size(); ++i) {
    decls_ << (i ? "," : "") << "{\"name\":" << Str{v->arms[i].name}
           << ",\"type\":" << Str{idl_type_string(v->arms[i].type)} << '}';
  }
  decls_ << "]}";
}

void DocJsonBuilder::finalize()
{
  auto filename = ctx_->get_file_path().filename();
  filename.replace_extension(".doc.json");
  std::ofstream ofs(out_dir_ / filename, std::ios::binary);
  ofs << "{\"format\":1"
      << ",\"file\":" << Str{ctx_->get_file_path().filename().string()}
      << ",\"module\":" << Str{ctx_->module()}
      << ",\"declarations\":[\n"
      << decls_.str() << "\n]}\n";
}

} // namespace npidl::builders
